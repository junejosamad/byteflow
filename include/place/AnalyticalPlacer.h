#pragma once
#include "db/Design.h"
#include "timer/Timer.h"
#include <vector>

// ============================================================
// Analytical Placer — Phase 1.1
//
// Algorithm: Bound-to-Bound (B2B) quadratic net model
//   + Preconditioned Conjugate Gradient solver
//   + Bin-based density spreading
//
// Outer loop (default 30 iterations):
//   1. Build sparse Laplacian from B2B springs
//   2. Solve Ax=bx and Ay=by independently with PCG
//   3. Spread density: push cells out of overcrowded bins
//   4. Recompute B2B bounds from new positions
//   5. Every 5 iterations: update timing-driven net weights
//
// Hands off to the existing Legalizer for row-snapping.
// Falls back to SA (via PlaceEngine) for designs < SA_THRESHOLD.
// ============================================================

class AnalyticalPlacer {
public:
    static constexpr int SA_THRESHOLD = 100; // cells; below this SA is faster

    AnalyticalPlacer(Design* design, Timer* timer = nullptr);

    // Main entry: run full analytical flow
    void run(double coreW, double coreH, int outerIter = 30);

private:
    // ---- Sparse matrix (CSR format) ----
    struct CsrMatrix {
        int n = 0;
        std::vector<double> val;
        std::vector<int>    col;
        std::vector<int>    rowPtr; // size n+1

        // y = A * x
        void matvec(const std::vector<double>& x,
                    std::vector<double>& y) const;
    };

    // ---- Internal stages ----
    void buildMovableIndex();
    void initPositions(double coreW, double coreH);

    // Build B2B Laplacian; returns system matrix A, RHS bx, by
    CsrMatrix buildLaplacian(std::vector<double>& bx,
                              std::vector<double>& by) const;

    // Jacobi-preconditioned CG — solves A*x = b in place
    void pcg(const CsrMatrix& A,
             const std::vector<double>& b,
             std::vector<double>& x,
             int maxIter = 300,
             double tol  = 1e-5) const;

    // Push cells out of overcrowded bins
    void spreadDensity(double coreW, double coreH);

    // Scale net weights by timing criticality
    void updateTimingWeights();

    // Copy posX/posY back into design->instances
    void writeBack() const;

    // ---- State ----
    Design* design;
    Timer*  timer;

    std::vector<int>    movable;     // indices into design->instances
    std::vector<double> posX, posY;  // current analytical positions
    std::vector<double> netWeight;   // per-net (design->nets order)
};
