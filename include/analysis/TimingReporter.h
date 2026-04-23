#pragma once
#include "timer/Timer.h"
#include "db/Design.h"
#include <string>
#include <vector>

// One cell/net stage on a timing path
struct PathStep {
    std::string instName;
    std::string pinName;
    std::string cellType;
    double      gateDelay   = 0.0;
    double      wireDelay   = 0.0;
    double      arrivalTime = 0.0;
    double      slack       = 0.0;
};

// A single traced timing path (setup or hold)
struct PathReport {
    std::string           startpoint;
    std::string           endpoint;
    std::string           type;          // "SETUP" or "HOLD"
    double                slack        = 0.0;
    double                arrivalTime  = 0.0;
    double                requiredTime = 0.0;
    std::vector<PathStep> steps;
};

// One bucket of the slack histogram
struct SlackBin {
    double      lo;
    double      hi;
    int         count = 0;
    std::string label;
};

// ============================================================
// TimingReporter — structured sign-off reports from a timed design
//
// Wraps a fully-updated Timer and exposes:
//   getTopPaths()          — worst-slack traced paths (setup)
//   getSlackHistogram()    — endpoint count per slack bucket
//   formatSummary()        — text sign-off summary block
//   formatPath()           — text breakdown for one PathReport
//   formatSlackHistogram() — ASCII bar chart
//   formatAllEndpoints()   — tabular endpoint list (text)
//   formatCdcReport()      — clock domain crossing summary
//   writeTextReport()      — single .rpt text file
//   writeHtmlReport()      — self-contained HTML sign-off page
// ============================================================
class TimingReporter {
public:
    TimingReporter(Timer& timer, Design& design);

    // --- Structured queries ---
    std::vector<PathReport> getTopPaths(int n = 5)          const;
    std::vector<SlackBin>   getSlackHistogram(int bins = 10) const;

    // --- Formatted text ---
    std::string formatSummary()                              const;
    std::string formatPath(const PathReport& p)              const;
    std::string formatSlackHistogram(int bins = 10)          const;
    std::string formatAllEndpoints()                         const;
    std::string formatCdcReport()                            const;

    // --- File export ---
    bool writeTextReport(const std::string& filename)        const;
    bool writeHtmlReport(const std::string& filename)        const;

private:
    Timer&   timer;
    Design&  design;

    PathReport traceBack(TimingNode* endpoint) const;
};
