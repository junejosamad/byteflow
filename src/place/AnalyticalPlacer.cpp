#include "place/AnalyticalPlacer.h"
#include "util/Logger.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// ============================================================
// Constructor
// ============================================================
AnalyticalPlacer::AnalyticalPlacer(Design* d, Timer* t)
    : design(d), timer(t) {}

// ============================================================
// CsrMatrix::matvec  y = A * x
// ============================================================
void AnalyticalPlacer::CsrMatrix::matvec(
        const std::vector<double>& x,
        std::vector<double>& y) const {
    y.assign(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int k = rowPtr[i]; k < rowPtr[i + 1]; ++k)
            s += val[k] * x[col[k]];
        y[i] = s;
    }
}

// ============================================================
// Build movable-cell index
// ============================================================
void AnalyticalPlacer::buildMovableIndex() {
    movable.clear();
    for (int i = 0; i < (int)design->instances.size(); ++i)
        if (!design->instances[i]->isFixed)
            movable.push_back(i);
}

// ============================================================
// Random initial spread
// ============================================================
void AnalyticalPlacer::initPositions(double coreW, double coreH) {
    posX.resize(movable.size());
    posY.resize(movable.size());
    for (int mi = 0; mi < (int)movable.size(); ++mi) {
        GateInstance* inst = design->instances[movable[mi]];
        if (inst->isPlaced) {
            posX[mi] = inst->x;
            posY[mi] = inst->y;
        } else {
            posX[mi] = 2.0 + (std::rand() / (double)RAND_MAX) * (coreW - 4.0);
            posY[mi] = 2.0 + (std::rand() / (double)RAND_MAX) * (coreH - 4.0);
        }
    }
}

// ============================================================
// Timing-driven net weights
// ============================================================
void AnalyticalPlacer::updateTimingWeights() {
    netWeight.assign(design->nets.size(), 1.0);
    if (!timer) return;
    const double alpha = 5.0;
    for (int ni = 0; ni < (int)design->nets.size(); ++ni) {
        double worst = std::numeric_limits<double>::infinity();
        for (Pin* p : design->nets[ni]->connectedPins)
            worst = std::min(worst, p->slack);
        if (worst < 0.0)
            netWeight[ni] = 1.0 + alpha * std::min(10.0, -worst / 1000.0);
    }
}

// ============================================================
// Build SEPARATE B2B Laplacians for X and Y
//
// Bug fix from v1: the single-matrix approach gave HPWL=0 because
// Y-span springs had wx=0 → nothing added to the shared matrix.
// Now Ax is built from X-span weights and Ay from Y-span weights.
//
// Tikhonov regularisation (alpha*I) prevents null-space collapse
// for all-movable designs: A_reg = A + alpha*I, b_reg = alpha*x_init.
// ============================================================
void AnalyticalPlacer::buildLaplacians(
        CsrMatrix& Ax, std::vector<double>& bx,
        CsrMatrix& Ay, std::vector<double>& by) const {

    const int    N     = (int)movable.size();
    const double EPS   = 1.0;  // Minimum distance — prevents infinite spring forces on coincident cells
    const double ALPHA = 0.5;  // Tikhonov regularisation weight — anchors cells to current positions

    // solver index: global inst idx → solver row (-1 if fixed)
    std::vector<int> s2g(N);                // solver → global
    std::vector<int> g2s(design->instances.size(), -1);
    for (int mi = 0; mi < N; ++mi) {
        s2g[mi] = movable[mi];
        g2s[movable[mi]] = mi;
    }

    // Flat COO triplets — avoids std::map tree-node heap fragmentation
    using Triplet = std::tuple<int,int,double>;
    std::vector<Triplet> tripX, tripY;
    const int estNNZ = N + (int)design->nets.size() * 8;
    tripX.reserve(estNNZ);
    tripY.reserve(estNNZ);
    bx.assign(N, 0.0);
    by.assign(N, 0.0);

    // Tikhonov: A += alpha*I, b += alpha * current_pos
    for (int mi = 0; mi < N; ++mi) {
        tripX.push_back({mi, mi, ALPHA});
        tripY.push_back({mi, mi, ALPHA});
        bx[mi] += ALPHA * posX[mi];
        by[mi] += ALPHA * posY[mi];
    }

    auto cur_x = [&](int gi) {
        int si = g2s[gi];
        return (si >= 0) ? posX[si] : design->instances[gi]->x;
    };
    auto cur_y = [&](int gi) {
        int si = g2s[gi];
        return (si >= 0) ? posY[si] : design->instances[gi]->y;
    };

    // Add spring in X between cells gi and gj with weight w
    auto springX = [&](int gi, int gj, double w) {
        int si = g2s[gi];
        if (si < 0) return;
        tripX.push_back({si, si, w});
        int sj = (gj >= 0 && gj < (int)g2s.size()) ? g2s[gj] : -1;
        if (sj >= 0) {
            tripX.push_back({si, sj, -w});
            tripX.push_back({sj, si, -w});
            tripX.push_back({sj, sj,  w});
        } else if (gj >= 0) {
            bx[si] += w * design->instances[gj]->x;
        }
    };

    // Add spring in Y between cells gi and gj with weight w
    auto springY = [&](int gi, int gj, double w) {
        int si = g2s[gi];
        if (si < 0) return;
        tripY.push_back({si, si, w});
        int sj = (gj >= 0 && gj < (int)g2s.size()) ? g2s[gj] : -1;
        if (sj >= 0) {
            tripY.push_back({si, sj, -w});
            tripY.push_back({sj, si, -w});
            tripY.push_back({sj, sj,  w});
        } else if (gj >= 0) {
            by[si] += w * design->instances[gj]->y;
        }
    };

    // Map pin → global instance index
    auto pinGI = [&](Pin* p) -> int {
        for (int i = 0; i < (int)design->instances.size(); ++i)
            if (design->instances[i] == p->inst) return i;
        return -1;
    };

    for (int ni = 0; ni < (int)design->nets.size(); ++ni) {
        Net* net = design->nets[ni];
        if (net->connectedPins.size() < 2) continue;

        double w = (ni < (int)netWeight.size()) ? netWeight[ni] : 1.0;

        // Find X bounds
        int giLx = -1, giRx = -1;
        double lx = 1e18, rx = -1e18;
        for (Pin* p : net->connectedPins) {
            int gi = pinGI(p);
            if (gi < 0) continue;
            double px = cur_x(gi);
            if (px < lx) { lx = px; giLx = gi; }
            if (px > rx) { rx = px; giRx = gi; }
        }

        // Find Y bounds
        int giBy = -1, giTy = -1;
        double yb = 1e18, yt = -1e18;
        for (Pin* p : net->connectedPins) {
            int gi = pinGI(p);
            if (gi < 0) continue;
            double py = cur_y(gi);
            if (py < yb) { yb = py; giBy = gi; }
            if (py > yt) { yt = py; giTy = gi; }
        }

        if (giLx < 0 || giRx < 0 || giBy < 0 || giTy < 0) continue;

        // X: spring between left and right bounds
        double wxLR = w / std::max(rx - lx, EPS);
        springX(giLx, giRx, wxLR);

        // Y: spring between bottom and top bounds
        double wyBT = w / std::max(yt - yb, EPS);
        springY(giBy, giTy, wyBT);

        // Interior pins → nearest X and Y bounds
        for (Pin* p : net->connectedPins) {
            int gi = pinGI(p);
            if (gi < 0) continue;
            double px = cur_x(gi), py = cur_y(gi);

            if (gi != giLx && gi != giRx) {
                springX(gi, giLx, w / std::max(std::abs(px - lx), EPS));
                springX(gi, giRx, w / std::max(std::abs(rx - px), EPS));
            }
            if (gi != giBy && gi != giTy) {
                springY(gi, giBy, w / std::max(std::abs(py - yb), EPS));
                springY(gi, giTy, w / std::max(std::abs(yt - py), EPS));
            }
        }
    }

    // Convert COO triplets → CSR (sort by row,col; merge duplicate entries)
    auto toCSR = [&](std::vector<Triplet>& trips, CsrMatrix& A) {
        A.n = N;
        std::sort(trips.begin(), trips.end(), [](const Triplet& a, const Triplet& b) {
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) < std::get<1>(b);
        });
        // Merge duplicates
        std::vector<Triplet> merged;
        merged.reserve(trips.size());
        for (auto& t : trips) {
            if (!merged.empty()
                    && std::get<0>(merged.back()) == std::get<0>(t)
                    && std::get<1>(merged.back()) == std::get<1>(t))
                std::get<2>(merged.back()) += std::get<2>(t);
            else
                merged.push_back(t);
        }
        A.rowPtr.assign(N + 1, 0);
        A.val.clear(); A.col.clear();
        A.val.reserve(merged.size());
        A.col.reserve(merged.size());
        for (auto& [r, c, v] : merged) {
            A.col.push_back(c);
            A.val.push_back(v);
            A.rowPtr[r + 1]++;
        }
        for (int i = 0; i < N; ++i)
            A.rowPtr[i + 1] += A.rowPtr[i];
    };
    toCSR(tripX, Ax);
    toCSR(tripY, Ay);
}

// ============================================================
// Jacobi-preconditioned CG
// ============================================================
void AnalyticalPlacer::pcg(
        const CsrMatrix& A,
        const std::vector<double>& b,
        std::vector<double>& x,
        int maxIter,
        double tol) const {

    const int N = A.n;
    if (N == 0) return;

    // Jacobi preconditioner: M^{-1} = 1/diag(A)
    std::vector<double> invD(N, 1.0);
    for (int i = 0; i < N; ++i)
        for (int k = A.rowPtr[i]; k < A.rowPtr[i+1]; ++k)
            if (A.col[k] == i && A.val[k] > 1e-12) {
                invD[i] = 1.0 / A.val[k];
                break;
            }

    std::vector<double> Ax_v(N), r(N), z(N), p(N), Ap(N);
    A.matvec(x, Ax_v);
    for (int i = 0; i < N; ++i) { r[i] = b[i] - Ax_v[i]; z[i] = invD[i]*r[i]; }
    p = z;
    double rz = std::inner_product(r.begin(), r.end(), z.begin(), 0.0);
    const double tol2 = tol * std::max(
        std::sqrt(std::inner_product(b.begin(), b.end(), b.begin(), 0.0)), 1.0);

    for (int it = 0; it < maxIter; ++it) {
        A.matvec(p, Ap);
        double pAp = std::inner_product(p.begin(), p.end(), Ap.begin(), 0.0);
        if (std::abs(pAp) < 1e-18) break;
        double alpha = rz / pAp;
        double rz2 = 0.0;
        for (int i = 0; i < N; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
            z[i]  = invD[i] * r[i];
            rz2  += r[i] * z[i];
        }
        double resid = std::sqrt(
            std::inner_product(r.begin(), r.end(), r.begin(), 0.0));
        if (resid < tol2) break;
        double beta = rz2 / rz;
        for (int i = 0; i < N; ++i) p[i] = z[i] + beta * p[i];
        rz = rz2;
    }
}

// ============================================================
// Bin-based density spreading
// ============================================================
void AnalyticalPlacer::spreadDensity(double coreW, double coreH) {
    const int N    = (int)movable.size();
    const int bins = std::max(4, (int)std::sqrt((double)N));
    const double bw = coreW / bins;
    const double bh = coreH / bins;
    const double targetArea = (coreW * coreH / (bins * bins)) * 0.7;

    std::vector<double> binArea(bins * bins, 0.0);
    for (int mi = 0; mi < N; ++mi) {
        GateInstance* inst = design->instances[movable[mi]];
        double ca = inst->type ? inst->type->width * inst->type->height : 1.0;
        int bx = std::clamp((int)(posX[mi] / bw), 0, bins-1);
        int by = std::clamp((int)(posY[mi] / bh), 0, bins-1);
        binArea[by * bins + bx] += ca;
    }

    const double step = std::min(bw, bh) * 0.3;
    for (int mi = 0; mi < N; ++mi) {
        int bx = std::clamp((int)(posX[mi] / bw), 0, bins-1);
        int by = std::clamp((int)(posY[mi] / bh), 0, bins-1);
        double area = binArea[by * bins + bx];
        if (area <= targetArea) continue;

        double ovf  = area / targetArea - 1.0;
        double bcx  = (bx + 0.5) * bw;
        double bcy  = (by + 0.5) * bh;
        double dx   = posX[mi] - bcx;
        double dy   = posY[mi] - bcy;
        double dist = std::sqrt(dx*dx + dy*dy) + 1e-4;

        posX[mi] = std::clamp(posX[mi] + step * ovf * (dx / dist), 2.0, coreW - 2.0);
        posY[mi] = std::clamp(posY[mi] + step * ovf * (dy / dist), 2.0, coreH - 2.0);
    }
}

// ============================================================
// Write back to design instances
// ============================================================
void AnalyticalPlacer::writeBack() const {
    for (int mi = 0; mi < (int)movable.size(); ++mi) {
        GateInstance* inst = design->instances[movable[mi]];
        inst->x = posX[mi];
        inst->y = posY[mi];
        inst->isPlaced = true;
    }
}

// ============================================================
// Compute HPWL from current posX/posY
// ============================================================
double AnalyticalPlacer::computeHpwl() const {
    std::vector<int> g2s(design->instances.size(), -1);
    for (int mi = 0; mi < (int)movable.size(); ++mi)
        g2s[movable[mi]] = mi;

    auto cx = [&](Pin* p) {
        for (int i = 0; i < (int)design->instances.size(); ++i)
            if (design->instances[i] == p->inst) {
                int si = g2s[i];
                return (si >= 0) ? posX[si] : p->inst->x;
            }
        return 0.0;
    };
    auto cy = [&](Pin* p) {
        for (int i = 0; i < (int)design->instances.size(); ++i)
            if (design->instances[i] == p->inst) {
                int si = g2s[i];
                return (si >= 0) ? posY[si] : p->inst->y;
            }
        return 0.0;
    };

    double hpwl = 0.0;
    for (Net* net : design->nets) {
        if (net->connectedPins.size() < 2) continue;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (Pin* p : net->connectedPins) {
            double px = cx(p), py = cy(p);
            minX = std::min(minX, px); maxX = std::max(maxX, px);
            minY = std::min(minY, py); maxY = std::max(maxY, py);
        }
        hpwl += (maxX - minX) + (maxY - minY);
    }
    return hpwl;
}

// ============================================================
// Main entry point
// ============================================================
void AnalyticalPlacer::run(double coreW, double coreH, int outerIter) {
    Logger::info(Logger::fmt()
        << "AnalyticalPlacer: starting ("
        << design->instances.size() << " cells, "
        << outerIter << " outer iterations)");

    buildMovableIndex();
    if (movable.empty()) {
        Logger::warn("AnalyticalPlacer: no movable cells");
        return;
    }

    netWeight.assign(design->nets.size(), 1.0);
    if (timer) {
        timer->buildGraph();
        timer->updateTiming();
        updateTimingWeights();
    }

    initPositions(coreW, coreH);

    double prevHpwl = 1e18;
    double bestHpwl = 1e18;
    std::vector<double> bestPosX = posX;
    std::vector<double> bestPosY = posY;

    for (int iter = 0; iter < outerIter; ++iter) {
        if (timer && iter > 0 && iter % 5 == 0) {
            writeBack();
            timer->updateTiming();
            updateTimingWeights();
        }

        // Build separate X and Y Laplacians
        CsrMatrix Ax, Ay;
        std::vector<double> bx, by;
        buildLaplacians(Ax, bx, Ay, by);

        // Solve X and Y independently
        pcg(Ax, bx, posX);
        pcg(Ay, by, posY);

        // Density spreading
        spreadDensity(coreW, coreH);

        // Clamp to die
        for (int mi = 0; mi < (int)movable.size(); ++mi) {
            posX[mi] = std::clamp(posX[mi], 2.0, coreW - 2.0);
            posY[mi] = std::clamp(posY[mi], 2.0, coreH - 2.0);
        }

        // Convergence check every 5 iterations
        if ((iter + 1) % 5 == 0 || iter == outerIter - 1) {
            double hpwl = computeHpwl();
            Logger::info(Logger::fmt()
                << "  iter " << (iter+1) << "/" << outerIter
                << "  HPWL=" << (int)hpwl);

            // Track best spread solution to restore if placement collapses
            if (hpwl > 1.0 && hpwl < bestHpwl) {
                bestHpwl = hpwl;
                bestPosX = posX;
                bestPosY = posY;
            }

            // Stop if collapsed (HPWL dropped to <5% of best) — restore best
            if (bestHpwl < 1e17 && hpwl < bestHpwl * 0.05) {
                Logger::info("  Restoring best spread solution (collapse detected)");
                posX = bestPosX;
                posY = bestPosY;
                break;
            }

            if (std::abs(prevHpwl - hpwl) / std::max(prevHpwl, 1.0) < 0.001) {
                Logger::info("  Converged (HPWL delta < 0.1%)");
                break;
            }
            prevHpwl = hpwl;
        }
    }

    writeBack();
    Logger::info("AnalyticalPlacer: done");
}
