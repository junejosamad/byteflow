#include "place/AnalyticalPlacer.h"
#include "util/Logger.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <map>

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
        double sum = 0.0;
        for (int k = rowPtr[i]; k < rowPtr[i + 1]; ++k)
            sum += val[k] * x[col[k]];
        y[i] = sum;
    }
}

// ============================================================
// Build movable-cell index (skip fixed / macros)
// ============================================================
void AnalyticalPlacer::buildMovableIndex() {
    movable.clear();
    for (int i = 0; i < (int)design->instances.size(); ++i) {
        GateInstance* inst = design->instances[i];
        if (!inst->isFixed)
            movable.push_back(i);
    }
}

// ============================================================
// Random initial spread across the die
// ============================================================
void AnalyticalPlacer::initPositions(double coreW, double coreH) {
    posX.resize(movable.size());
    posY.resize(movable.size());
    for (int mi = 0; mi < (int)movable.size(); ++mi) {
        GateInstance* inst = design->instances[movable[mi]];
        // Warm-start from existing position if already placed; else random
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
//   w = 1 + alpha * max(0, -slack / period)
// ============================================================
void AnalyticalPlacer::updateTimingWeights() {
    netWeight.assign(design->nets.size(), 1.0);
    if (!timer) return;

    const double alpha = 5.0; // criticality amplifier

    // Use the worst slack on any pin connected to this net as proxy
    for (int ni = 0; ni < (int)design->nets.size(); ++ni) {
        Net* net = design->nets[ni];
        double worstSlack = std::numeric_limits<double>::infinity();
        for (Pin* p : net->connectedPins) {
            if (p->slack < worstSlack) worstSlack = p->slack;
        }
        if (worstSlack < 0.0) {
            netWeight[ni] = 1.0 + alpha * std::min(10.0, -worstSlack / 1000.0);
        }
    }
}

// ============================================================
// Build B2B Laplacian matrix + RHS vectors bx, by
//
// For each net k with weight w_k:
//   Find leftmost (l) and rightmost (r) pins along X.
//   For every pin p != l, r:
//     Add spring p->l with weight  w / max(|px - lx|, eps)
//     Add spring p->r with weight  w / max(|rx - px|, eps)
//   Add spring l->r with weight    w / max(|rx - lx|, eps)
//
// Movable-movable springs go into the matrix A.
// Movable-fixed springs contribute to A diagonal and RHS b.
// ============================================================
AnalyticalPlacer::CsrMatrix AnalyticalPlacer::buildLaplacian(
        std::vector<double>& bx,
        std::vector<double>& by) const {

    const int N = (int)movable.size();
    const double EPS = 1e-4;

    // Map global instance index → solver index (-1 if fixed)
    std::vector<int> instToSolver(design->instances.size(), -1);
    for (int mi = 0; mi < N; ++mi)
        instToSolver[movable[mi]] = mi;

    // Accumulate triplets: (row, col) → value
    // Use std::map for automatic de-dup; will convert to CSR after
    std::vector<std::map<int, double>> Amap(N);
    bx.assign(N, 0.0);
    by.assign(N, 0.0);

    auto addSpring = [&](int gi, int gj, double wx, double wy) {
        // gi, gj = global instance indices
        int si = instToSolver[gi];
        int sj = (gj >= 0) ? instToSolver[gj] : -1;

        double fxj = (gj >= 0) ? design->instances[gj]->x : 0.0;
        double fyj = (gj >= 0) ? design->instances[gj]->y : 0.0;

        if (si < 0) return; // gi fixed — skip

        // diagonal
        Amap[si][si] += wx;

        if (sj >= 0) {
            // movable-movable off-diagonal
            Amap[si][sj] -= wx;
        } else {
            // movable-fixed → RHS
            bx[si] += wx * fxj;
            by[si] += wy * fyj;
        }

        // symmetric counterpart
        if (sj >= 0) {
            Amap[sj][sj] += wx;
            Amap[sj][si] -= wx;
            by[sj] += 0.0; // symmetric — handled when si processed
        }
    };

    // Helper: get X position of pin (fixed or from posX)
    auto getX = [&](Pin* p) -> double {
        int gi = -1;
        for (int i = 0; i < (int)design->instances.size(); ++i) {
            if (design->instances[i] == p->inst) { gi = i; break; }
        }
        if (gi < 0) return p->inst->x;
        int si = instToSolver[gi];
        return (si >= 0) ? posX[si] : p->inst->x;
    };
    auto getY = [&](Pin* p) -> double {
        int gi = -1;
        for (int i = 0; i < (int)design->instances.size(); ++i) {
            if (design->instances[i] == p->inst) { gi = i; break; }
        }
        if (gi < 0) return p->inst->y;
        int si = instToSolver[gi];
        return (si >= 0) ? posY[si] : p->inst->y;
    };
    auto getInstIdx = [&](Pin* p) -> int {
        for (int i = 0; i < (int)design->instances.size(); ++i)
            if (design->instances[i] == p->inst) return i;
        return -1;
    };

    for (int ni = 0; ni < (int)design->nets.size(); ++ni) {
        Net* net = design->nets[ni];
        if (net->connectedPins.size() < 2) continue;

        double w = (ni < (int)netWeight.size()) ? netWeight[ni] : 1.0;

        // Find bound pins along X and Y
        Pin* pinLx = nullptr, *pinRx = nullptr;
        Pin* pinBy = nullptr, *pinTy = nullptr;
        double lx = 1e18, rx = -1e18, by_ = 1e18, ty = -1e18;

        for (Pin* p : net->connectedPins) {
            double px = getX(p), py = getY(p);
            if (px < lx) { lx = px; pinLx = p; }
            if (px > rx) { rx = px; pinRx = p; }
            if (py < by_) { by_ = py; pinBy = p; }
            if (py > ty) { ty = py; pinTy = p; }
        }
        if (!pinLx || !pinRx || !pinBy || !pinTy) continue;

        int giLx = getInstIdx(pinLx), giRx = getInstIdx(pinRx);
        int giBy = getInstIdx(pinBy), giTy = getInstIdx(pinTy);

        // Add spring between bounds
        double wxLR = w / std::max(rx - lx, EPS);
        double wyBT = w / std::max(ty - by_, EPS);
        addSpring(giLx, giRx, wxLR, 0.0);
        addSpring(giBy, giTy, 0.0, wyBT);

        // Add springs from interior pins to their nearest bound
        for (Pin* p : net->connectedPins) {
            int gi = getInstIdx(p);
            if (gi < 0) continue;
            double px = getX(p), py = getY(p);

            if (gi != giLx && gi != giRx) {
                double wL = w / std::max(std::abs(px - lx), EPS);
                double wR = w / std::max(std::abs(rx - px), EPS);
                addSpring(gi, giLx, wL, 0.0);
                addSpring(gi, giRx, wR, 0.0);
            }
            if (gi != giBy && gi != giTy) {
                double wB = w / std::max(std::abs(py - by_), EPS);
                double wT = w / std::max(std::abs(ty - py), EPS);
                addSpring(gi, giBy, 0.0, wB);
                addSpring(gi, giTy, 0.0, wT);
            }
        }
    }

    // Convert Amap to CSR
    CsrMatrix A;
    A.n = N;
    A.rowPtr.resize(N + 1, 0);

    for (int i = 0; i < N; ++i)
        A.rowPtr[i + 1] = A.rowPtr[i] + (int)Amap[i].size();

    A.val.reserve(A.rowPtr[N]);
    A.col.reserve(A.rowPtr[N]);

    for (int i = 0; i < N; ++i) {
        for (auto& [c, v] : Amap[i]) {
            A.col.push_back(c);
            A.val.push_back(v);
        }
    }

    // Ensure diagonal is positive (regularisation for isolated cells)
    for (int i = 0; i < N; ++i) {
        bool hasDiag = false;
        for (int k = A.rowPtr[i]; k < A.rowPtr[i + 1]; ++k) {
            if (A.col[k] == i) {
                if (A.val[k] < 1e-8) A.val[k] = 1e-8;
                hasDiag = true;
                break;
            }
        }
        if (!hasDiag) {
            // Shouldn't happen with connected nets, but guard anyway
            A.col.push_back(i); A.val.push_back(1e-8);
            A.rowPtr[i + 1]++;
            // Shift all subsequent rowPtrs
            for (int j = i + 2; j <= N; ++j) A.rowPtr[j]++;
        }
    }

    return A;
}

// ============================================================
// Jacobi-preconditioned Conjugate Gradient
//   Solves A*x = b  (A must be SPD)
// ============================================================
void AnalyticalPlacer::pcg(
        const CsrMatrix& A,
        const std::vector<double>& b,
        std::vector<double>& x,
        int maxIter,
        double tol) const {

    const int N = A.n;
    if (N == 0) return;

    // Extract diagonal for Jacobi preconditioner M^{-1}
    std::vector<double> invDiag(N, 1.0);
    for (int i = 0; i < N; ++i) {
        for (int k = A.rowPtr[i]; k < A.rowPtr[i + 1]; ++k) {
            if (A.col[k] == i && A.val[k] > 1e-12) {
                invDiag[i] = 1.0 / A.val[k];
                break;
            }
        }
    }

    // r = b - A*x
    std::vector<double> Ax(N), r(N), z(N), p(N), Ap(N);
    A.matvec(x, Ax);
    for (int i = 0; i < N; ++i) r[i] = b[i] - Ax[i];

    // z = M^{-1} r
    for (int i = 0; i < N; ++i) z[i] = invDiag[i] * r[i];
    p = z;

    double rz = 0.0;
    for (int i = 0; i < N; ++i) rz += r[i] * z[i];

    const double b_norm = std::sqrt(
        std::inner_product(b.begin(), b.end(), b.begin(), 0.0));
    const double tol2 = tol * std::max(b_norm, 1.0);

    for (int iter = 0; iter < maxIter; ++iter) {
        A.matvec(p, Ap);
        double pAp = std::inner_product(p.begin(), p.end(), Ap.begin(), 0.0);
        if (std::abs(pAp) < 1e-18) break;

        double alpha = rz / pAp;
        double rz_new = 0.0;

        for (int i = 0; i < N; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
            z[i]  = invDiag[i] * r[i];
            rz_new += r[i] * z[i];
        }

        double resid = std::sqrt(
            std::inner_product(r.begin(), r.end(), r.begin(), 0.0));
        if (resid < tol2) break;

        double beta = rz_new / rz;
        for (int i = 0; i < N; ++i) p[i] = z[i] + beta * p[i];
        rz = rz_new;
    }
}

// ============================================================
// Bin-based density spreading
//   Divide die into sqrt(N) x sqrt(N) bins.
//   For each overcrowded bin, apply a repulsion gradient step
//   to cells inside it, pushing them toward the bin centroid.
// ============================================================
void AnalyticalPlacer::spreadDensity(double coreW, double coreH) {
    const int N    = (int)movable.size();
    const int bins = std::max(4, (int)std::sqrt((double)N));
    const double bw = coreW / bins;
    const double bh = coreH / bins;

    // Accumulate cell area per bin
    std::vector<double> binArea(bins * bins, 0.0);
    const double binTargetArea = (coreW * coreH / (bins * bins)) * 0.7;

    for (int mi = 0; mi < N; ++mi) {
        GateInstance* inst = design->instances[movable[mi]];
        double cellArea = inst->type ? inst->type->width * inst->type->height : 1.0;
        int bx = std::clamp((int)(posX[mi] / bw), 0, bins - 1);
        int by = std::clamp((int)(posY[mi] / bh), 0, bins - 1);
        binArea[by * bins + bx] += cellArea;
    }

    // Apply repulsion to cells in overcrowded bins
    const double stepSize = std::min(bw, bh) * 0.3;
    for (int mi = 0; mi < N; ++mi) {
        int bx = std::clamp((int)(posX[mi] / bw), 0, bins - 1);
        int by = std::clamp((int)(posY[mi] / bh), 0, bins - 1);
        double area = binArea[by * bins + bx];

        if (area <= binTargetArea) continue;

        double overflow = area / binTargetArea - 1.0;
        double binCx = (bx + 0.5) * bw;
        double binCy = (by + 0.5) * bh;

        // Push away from bin centre
        double dx = posX[mi] - binCx;
        double dy = posY[mi] - binCy;
        double dist = std::sqrt(dx * dx + dy * dy) + 1e-4;

        posX[mi] += stepSize * overflow * (dx / dist);
        posY[mi] += stepSize * overflow * (dy / dist);

        // Clamp to die
        posX[mi] = std::clamp(posX[mi], 2.0, coreW - 2.0);
        posY[mi] = std::clamp(posY[mi], 2.0, coreH - 2.0);
    }
}

// ============================================================
// Write solver positions back to design instances
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
// Main entry point
// ============================================================
void AnalyticalPlacer::run(double coreW, double coreH, int outerIter) {
    Logger::info(Logger::fmt()
        << "AnalyticalPlacer: starting ("
        << design->instances.size() << " cells, "
        << outerIter << " outer iterations)");

    buildMovableIndex();
    if (movable.empty()) {
        Logger::warn("AnalyticalPlacer: no movable cells — skipping");
        return;
    }

    // Initialise net weights
    netWeight.assign(design->nets.size(), 1.0);
    if (timer) {
        timer->buildGraph();
        timer->updateTiming();
        updateTimingWeights();
    }

    initPositions(coreW, coreH);

    double prevHpwl = 1e18;

    for (int iter = 0; iter < outerIter; ++iter) {
        // Update timing weights every 5 outer iters
        if (timer && iter > 0 && iter % 5 == 0) {
            writeBack();
            timer->updateTiming();
            updateTimingWeights();
        }

        // 1. Build B2B Laplacian
        std::vector<double> bx, by;
        CsrMatrix A = buildLaplacian(bx, by);

        // 2. Solve X  (warm-start from current posX)
        pcg(A, bx, posX);

        // 3. Solve Y
        pcg(A, by, posY);

        // 4. Density spreading
        spreadDensity(coreW, coreH);

        // 5. Clamp to die
        for (int mi = 0; mi < (int)movable.size(); ++mi) {
            posX[mi] = std::clamp(posX[mi], 2.0, coreW - 2.0);
            posY[mi] = std::clamp(posY[mi], 2.0, coreH - 2.0);
        }

        // Compute HPWL for convergence check
        if ((iter + 1) % 5 == 0 || iter == outerIter - 1) {
            writeBack();
            double hpwl = 0.0;
            for (Net* net : design->nets) {
                if (net->connectedPins.size() < 2) continue;
                double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
                for (Pin* p : net->connectedPins) {
                    minX = std::min(minX, p->inst->x);
                    maxX = std::max(maxX, p->inst->x);
                    minY = std::min(minY, p->inst->y);
                    maxY = std::max(maxY, p->inst->y);
                }
                hpwl += (maxX - minX) + (maxY - minY);
            }
            Logger::info(Logger::fmt()
                << "  iter " << (iter + 1) << "/" << outerIter
                << "  HPWL=" << (int)hpwl);

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
