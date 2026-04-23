#include "analysis/TimingReporter.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <stack>
#include <set>

// ============================================================
TimingReporter::TimingReporter(Timer& t, Design& d) : timer(t), design(d) {}

// ============================================================
// Internal: trace a path backwards from endpoint to source
// ============================================================
PathReport TimingReporter::traceBack(TimingNode* endpoint) const {
    PathReport report;
    report.slack        = endpoint->slack;
    report.arrivalTime  = endpoint->arrivalTime;
    report.requiredTime = endpoint->requiredTime;
    report.type         = "SETUP";

    // Walk highest-AT parent chain  endpoint → source
    std::stack<TimingNode*> stack;
    TimingNode* trace = endpoint;
    for (int depth = 0; depth < 1000 && trace; depth++) {
        stack.push(trace);
        TimingNode* bestParent = nullptr;
        double maxAT = -1e30;
        for (TimingNode* p : trace->parents) {
            if (p->arrivalTime > maxAT) { maxAT = p->arrivalTime; bestParent = p; }
        }
        trace = (bestParent && bestParent->arrivalTime > 0.0) ? bestParent : nullptr;
    }

    // Record startpoint name
    if (!stack.empty()) {
        TimingNode* src = stack.top();
        report.startpoint = src->pin->inst ? src->pin->inst->name : "(pi)";
    }
    report.endpoint = endpoint->pin->inst ? endpoint->pin->inst->name + "/" + endpoint->pin->name
                                          : "(po)";

    // Build steps in forward order
    while (!stack.empty()) {
        TimingNode* n = stack.top(); stack.pop();
        PathStep step;
        step.instName   = n->pin->inst ? n->pin->inst->name : "(pi)";
        step.pinName    = n->pin->name;
        step.cellType   = (n->pin->inst && n->pin->inst->type) ? n->pin->inst->type->name : "?";
        step.gateDelay  = n->gateDelay;
        step.wireDelay  = n->wireDelay;
        step.arrivalTime= n->arrivalTime;
        step.slack      = n->slack;
        report.steps.push_back(step);
    }
    return report;
}

// ============================================================
// getTopPaths: return N worst-slack setup paths
// ============================================================
std::vector<PathReport> TimingReporter::getTopPaths(int n) const {
    const double INF = std::numeric_limits<double>::infinity();

    // Collect endpoints sorted by slack (worst first)
    std::vector<TimingNode*> endpoints;
    for (TimingNode* node : timer.nodes) {
        if (timer.isEndpoint(node) && node->requiredTime < INF)
            endpoints.push_back(node);
    }
    std::sort(endpoints.begin(), endpoints.end(),
              [](TimingNode* a, TimingNode* b) { return a->slack < b->slack; });

    int count = std::min(n, (int)endpoints.size());
    std::vector<PathReport> result;
    result.reserve(count);
    for (int i = 0; i < count; i++)
        result.push_back(traceBack(endpoints[i]));
    return result;
}

// ============================================================
// getSlackHistogram: bin all setup endpoints by slack
// ============================================================
std::vector<SlackBin> TimingReporter::getSlackHistogram(int bins) const {
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> slacks;
    for (TimingNode* node : timer.nodes) {
        if (timer.isEndpoint(node) && node->requiredTime < INF)
            slacks.push_back(node->slack);
    }

    if (slacks.empty()) return {};

    double lo = *std::min_element(slacks.begin(), slacks.end());
    double hi = *std::max_element(slacks.begin(), slacks.end());

    // Expand range slightly so hi boundary is inclusive
    if (lo == hi) { lo -= 1.0; hi += 1.0; }
    double width = (hi - lo) / bins;

    std::vector<SlackBin> result(bins);
    for (int i = 0; i < bins; i++) {
        result[i].lo    = lo + i * width;
        result[i].hi    = lo + (i + 1) * width;
        result[i].count = 0;

        std::ostringstream lbl;
        lbl << std::fixed << std::setprecision(0)
            << result[i].lo << " .. " << result[i].hi;
        result[i].label = lbl.str();
    }

    for (double s : slacks) {
        int idx = (int)std::floor((s - lo) / width);
        idx = std::max(0, std::min(bins - 1, idx));
        result[idx].count++;
    }
    return result;
}

// ============================================================
// formatSummary
// ============================================================
std::string TimingReporter::formatSummary() const {
    auto sum = timer.getSummary();
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "============================================================\n";
    os << "  TIMING SIGN-OFF SUMMARY\n";
    os << "============================================================\n";
    os << "  Clock Period   : " << timer.clockPeriod      << " ps\n";
    os << "  Uncertainty    : " << timer.clockUncertainty << " ps\n";
    os << "  Latency        : " << timer.clockLatency     << " ps\n";
    os << "  Delay Model    : " << (timer.library    ? "NLDM" : "Scalar") << "\n";
    os << "  Wire Model     : " << (timer.spefEngine ? "Elmore RC" : "HPWL") << "\n";
    os << "------------------------------------------------------------\n";
    os << "  Setup WNS      : " << sum.wns  << " ps\n";
    os << "  Setup TNS      : " << sum.tns  << " ps\n";
    os << "  Setup Violations: " << sum.violations << " / " << sum.endpoints << "\n";
    os << "  Hold  WNS      : " << sum.holdWns  << " ps\n";
    os << "  Hold  TNS      : " << sum.holdTns  << " ps\n";
    os << "  Hold  Violations: " << sum.holdViolations << "\n";
    bool ok = (sum.violations == 0 && sum.holdViolations == 0);
    os << "  Status         : " << (ok ? "TIMING MET" : "*** VIOLATIONS ***") << "\n";
    os << "============================================================\n";
    return os.str();
}

// ============================================================
// formatPath: per-step breakdown of one PathReport
// ============================================================
std::string TimingReporter::formatPath(const PathReport& p) const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "------------------------------------------------------------\n";
    os << "  Path Type    : " << p.type << "\n";
    os << "  Startpoint   : " << p.startpoint << "\n";
    os << "  Endpoint     : " << p.endpoint   << "\n";
    os << "  Slack        : " << p.slack       << " ps"
       << (p.slack < 0 ? "  *** VIOLATION ***" : "  (met)") << "\n";
    os << "  Arrival Time : " << p.arrivalTime  << " ps\n";
    os << "  Required Time: " << p.requiredTime << " ps\n";
    os << "\n";
    os << "  " << std::left
       << std::setw(24) << "Instance"
       << std::setw(8)  << "Pin"
       << std::setw(18) << "CellType"
       << std::setw(12) << "Gate(ps)"
       << std::setw(12) << "Wire(ps)"
       << std::setw(14) << "Arrival(ps)"
       << "Incr(ps)\n";
    os << "  " << std::string(100, '-') << "\n";

    double prevAT = 0.0;
    for (const PathStep& s : p.steps) {
        double incr = s.arrivalTime - prevAT;
        os << "  " << std::left
           << std::setw(24) << s.instName
           << std::setw(8)  << s.pinName
           << std::setw(18) << s.cellType
           << std::setw(12) << s.gateDelay
           << std::setw(12) << s.wireDelay
           << std::setw(14) << s.arrivalTime
           << incr << "\n";
        prevAT = s.arrivalTime;
    }
    os << "------------------------------------------------------------\n";
    return os.str();
}

// ============================================================
// formatSlackHistogram: ASCII bar chart
// ============================================================
std::string TimingReporter::formatSlackHistogram(int bins) const {
    auto histogram = getSlackHistogram(bins);
    if (histogram.empty()) return "  (no timing endpoints)\n";

    int maxCount = 0;
    for (const SlackBin& b : histogram) maxCount = std::max(maxCount, b.count);

    const int barWidth = 40;
    std::ostringstream os;
    os << "\n  Slack Histogram (ps):\n";
    os << "  " << std::string(70, '-') << "\n";
    for (const SlackBin& b : histogram) {
        int filled = (maxCount > 0) ? (b.count * barWidth / maxCount) : 0;
        std::string bar(filled, '#');
        os << "  " << std::right << std::setw(18) << b.label
           << " | " << std::left << std::setw(barWidth) << bar
           << "  " << b.count << "\n";
    }
    os << "  " << std::string(70, '-') << "\n";
    return os.str();
}

// ============================================================
// formatAllEndpoints: tabular endpoint list
// ============================================================
std::string TimingReporter::formatAllEndpoints() const {
    const double INF = std::numeric_limits<double>::infinity();
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "\n  Endpoint Slack Table:\n";
    os << "  " << std::left
       << std::setw(24) << "Instance"
       << std::setw(6)  << "Pin"
       << std::setw(14) << "Arrival(ps)"
       << std::setw(14) << "Required(ps)"
       << std::setw(14) << "SetupSlk(ps)"
       << std::setw(14) << "HoldSlk(ps)"
       << "Status\n";
    os << "  " << std::string(100, '-') << "\n";

    for (TimingNode* node : timer.nodes) {
        bool isSetupEP = timer.isEndpoint(node);
        bool isHoldEP  = (node->holdSlack < INF);
        if (!isSetupEP && !isHoldEP) continue;

        std::string inst = node->pin->inst ? node->pin->inst->name : "(po)";
        bool setupOk = node->slack >= 0.0;
        bool holdOk  = (node->holdSlack >= 0.0 || node->holdSlack == INF);

        std::string status;
        if (!setupOk && !holdOk) status = "SETUP+HOLD";
        else if (!setupOk)       status = "SETUP_VIOL";
        else if (!holdOk)        status = "HOLD_VIOL";
        else                     status = "MET";

        os << "  " << std::left
           << std::setw(24) << inst
           << std::setw(6)  << node->pin->name
           << std::setw(14) << node->arrivalTime
           << std::setw(14) << (node->requiredTime == INF ? 0.0 : node->requiredTime)
           << std::setw(14) << node->slack
           << std::setw(14) << (node->holdSlack == INF ? 0.0 : node->holdSlack)
           << status << "\n";
    }
    return os.str();
}

// ============================================================
// formatCdcReport: clock domain crossing analysis
// ============================================================
std::string TimingReporter::formatCdcReport() const {
    // Identify clock domains via FF clock pins (C-pin drivers)
    // Map each FF to its clock net name
    std::map<std::string, std::set<std::string>> domainFFs;  // clkNet → {ffNames}

    for (GateInstance* inst : design.instances) {
        if (!inst->type || !inst->type->isSequential) continue;
        for (Pin* p : inst->pins) {
            if (p->name == "C" || p->name == "CLK" || p->name == "CK") {
                std::string clkNet = p->net ? p->net->name : "unknown";
                domainFFs[clkNet].insert(inst->name);
            }
        }
    }

    std::ostringstream os;
    os << "\n  Clock Domain Crossing (CDC) Report:\n";
    os << "  " << std::string(60, '-') << "\n";

    if (domainFFs.empty()) {
        os << "  No sequential cells found — combinational design.\n";
        return os.str();
    }

    os << "  Clock Domains Found: " << domainFFs.size() << "\n\n";
    for (const auto& [clkNet, ffs] : domainFFs) {
        os << "  Domain [" << clkNet << "]  (" << ffs.size() << " FFs)\n";
        for (const std::string& ff : ffs)
            os << "    - " << ff << "\n";
    }

    if (domainFFs.size() == 1) {
        os << "\n  Single clock domain — no CDC crossings.\n";
    } else {
        // Detect paths: Q-pin of one domain feeds D-pin of another
        std::map<std::string, std::string> ffToDomain;
        for (const auto& [clkNet, ffs] : domainFFs)
            for (const std::string& ff : ffs)
                ffToDomain[ff] = clkNet;

        int crossings = 0;
        for (GateInstance* src : design.instances) {
            if (!src->type || !src->type->isSequential) continue;
            std::string srcDomain = ffToDomain.count(src->name) ? ffToDomain[src->name] : "";
            if (srcDomain.empty()) continue;

            // Follow Q-pin fanout
            for (Pin* qPin : src->pins) {
                if (qPin->type != PinType::OUTPUT) continue;
                if (!qPin->net) continue;
                for (Pin* load : qPin->net->connectedPins) {
                    if (!load->inst || load->inst == src) continue;
                    if (!load->inst->type || !load->inst->type->isSequential) continue;
                    std::string dstDomain = ffToDomain.count(load->inst->name)
                                           ? ffToDomain[load->inst->name] : "";
                    if (!dstDomain.empty() && dstDomain != srcDomain) {
                        os << "  *** CDC: " << src->name << " [" << srcDomain << "] -> "
                           << load->inst->name << " [" << dstDomain << "]\n";
                        crossings++;
                    }
                }
            }
        }
        if (crossings == 0) os << "\n  No direct FF-to-FF CDC crossings found.\n";
        else os << "\n  Total CDC crossings: " << crossings << " *** REVIEW REQUIRED ***\n";
    }
    os << "  " << std::string(60, '-') << "\n";
    return os.str();
}

// ============================================================
// writeTextReport: write all sections to .rpt file
// ============================================================
bool TimingReporter::writeTextReport(const std::string& filename) const {
    std::ofstream f(filename);
    if (!f) return false;

    f << formatSummary();
    f << "\n";

    auto paths = getTopPaths(5);
    if (!paths.empty()) {
        f << "Top " << paths.size() << " Worst Timing Paths:\n";
        for (size_t i = 0; i < paths.size(); i++) {
            f << "\nPath #" << (i + 1) << "\n";
            f << formatPath(paths[i]);
        }
    }

    f << formatSlackHistogram(10);
    f << formatAllEndpoints();
    f << formatCdcReport();
    return true;
}

// ============================================================
// writeHtmlReport: self-contained styled HTML sign-off page
// ============================================================
bool TimingReporter::writeHtmlReport(const std::string& filename) const {
    std::ofstream f(filename);
    if (!f) return false;

    auto sum   = timer.getSummary();
    auto paths = getTopPaths(5);
    auto hist  = getSlackHistogram(10);

    bool ok = (sum.violations == 0 && sum.holdViolations == 0);
    std::string statusColor = ok ? "#27ae60" : "#e74c3c";
    std::string statusText  = ok ? "TIMING MET" : "VIOLATIONS PRESENT";

    f << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>OpenEDA Timing Sign-Off Report</title>
<style>
  body { font-family: 'Courier New', monospace; background:#1a1a2e; color:#e0e0e0; margin:0; padding:20px; }
  h1 { color:#a78bfa; border-bottom:2px solid #a78bfa; padding-bottom:8px; }
  h2 { color:#7dd3fc; margin-top:32px; }
  .card { background:#16213e; border:1px solid #2d3561; border-radius:8px; padding:16px; margin:16px 0; }
  .status-ok   { color:#27ae60; font-weight:bold; font-size:1.2em; }
  .status-fail { color:#e74c3c; font-weight:bold; font-size:1.2em; }
  table { width:100%; border-collapse:collapse; font-size:0.85em; }
  th { background:#2d3561; color:#a78bfa; text-align:left; padding:6px 10px; }
  td { padding:5px 10px; border-bottom:1px solid #2d3561; }
  tr.met    td { color:#a3e4a3; }
  tr.viol   td { color:#f1948a; background:#2c1515; }
  tr.hold   td { color:#f9e79f; background:#2c2815; }
  .bar-wrap { background:#0d0d1a; border-radius:4px; height:18px; }
  .bar-fill  { background:#a78bfa; height:18px; border-radius:4px; display:inline-block; vertical-align:top; }
  .kv { display:grid; grid-template-columns:180px 1fr; gap:6px 0; }
  .kv .k { color:#7dd3fc; } .kv .v { color:#e0e0e0; }
  pre { background:#0d0d1a; padding:12px; border-radius:6px; font-size:0.8em; overflow-x:auto; }
</style>
</head>
<body>
<h1>OpenEDA — Timing Sign-Off Report</h1>
)";

    // Summary card
    f << "<div class=\"card\">\n";
    f << "<h2>Summary</h2>\n";
    f << "<div class=\"kv\">\n";
    f << "  <span class=\"k\">Clock Period</span><span class=\"v\">" << std::fixed << std::setprecision(1) << timer.clockPeriod << " ps</span>\n";
    f << "  <span class=\"k\">Uncertainty</span><span class=\"v\">" << timer.clockUncertainty << " ps</span>\n";
    f << "  <span class=\"k\">Latency</span><span class=\"v\">" << timer.clockLatency << " ps</span>\n";
    f << "  <span class=\"k\">Delay Model</span><span class=\"v\">" << (timer.library ? "NLDM" : "Scalar") << "</span>\n";
    f << "  <span class=\"k\">Wire Model</span><span class=\"v\">" << (timer.spefEngine ? "Elmore RC" : "HPWL") << "</span>\n";
    f << "  <span class=\"k\">Setup WNS</span><span class=\"v\">" << sum.wns << " ps</span>\n";
    f << "  <span class=\"k\">Setup TNS</span><span class=\"v\">" << sum.tns << " ps</span>\n";
    f << "  <span class=\"k\">Setup Violations</span><span class=\"v\">" << sum.violations << " / " << sum.endpoints << "</span>\n";
    f << "  <span class=\"k\">Hold WNS</span><span class=\"v\">" << sum.holdWns << " ps</span>\n";
    f << "  <span class=\"k\">Hold Violations</span><span class=\"v\">" << sum.holdViolations << "</span>\n";
    f << "  <span class=\"k\">Status</span><span class=\"v\" style=\"color:" << statusColor << ";font-weight:bold\">" << statusText << "</span>\n";
    f << "</div></div>\n";

    // Slack histogram
    if (!hist.empty()) {
        int maxCount = 0;
        for (const SlackBin& b : hist) maxCount = std::max(maxCount, b.count);
        f << "<div class=\"card\"><h2>Slack Histogram</h2>\n";
        f << "<table><tr><th>Slack Range (ps)</th><th>Count</th><th style=\"width:50%\">Distribution</th></tr>\n";
        for (const SlackBin& b : hist) {
            int pct = maxCount > 0 ? b.count * 100 / maxCount : 0;
            bool isViol = b.hi <= 0;
            f << "<tr class=\"" << (isViol ? "viol" : "met") << "\">"
              << "<td>" << b.label << "</td>"
              << "<td>" << b.count << "</td>"
              << "<td><div class=\"bar-wrap\"><div class=\"bar-fill\" style=\"width:" << pct << "%\"></div></div></td>"
              << "</tr>\n";
        }
        f << "</table></div>\n";
    }

    // Top paths
    if (!paths.empty()) {
        f << "<div class=\"card\"><h2>Top " << paths.size() << " Worst Paths</h2>\n";
        for (size_t i = 0; i < paths.size(); i++) {
            const PathReport& p = paths[i];
            bool pViol = p.slack < 0;
            f << "<details" << (i == 0 ? " open" : "") << ">\n";
            f << "<summary style=\"cursor:pointer;padding:6px;color:" << (pViol ? "#f1948a" : "#a3e4a3") << "\">"
              << "#" << (i+1) << "  " << p.startpoint << " &rarr; " << p.endpoint
              << "  &nbsp; slack=" << std::fixed << std::setprecision(1) << p.slack << " ps"
              << (pViol ? "  *** VIOLATION ***" : "  (met)")
              << "</summary>\n";
            f << "<table><tr><th>Instance</th><th>Pin</th><th>CellType</th>"
              << "<th>Gate(ps)</th><th>Wire(ps)</th><th>Arrival(ps)</th><th>Incr(ps)</th></tr>\n";
            double prevAT = 0.0;
            for (const PathStep& s : p.steps) {
                double incr = s.arrivalTime - prevAT;
                f << "<tr><td>" << s.instName << "</td><td>" << s.pinName << "</td>"
                  << "<td>" << s.cellType << "</td>"
                  << "<td>" << std::fixed << std::setprecision(2) << s.gateDelay << "</td>"
                  << "<td>" << s.wireDelay << "</td>"
                  << "<td>" << s.arrivalTime << "</td>"
                  << "<td>" << incr << "</td></tr>\n";
                prevAT = s.arrivalTime;
            }
            f << "</table></details>\n";
        }
        f << "</div>\n";
    }

    // All endpoints table
    {
        const double INF = std::numeric_limits<double>::infinity();
        f << "<div class=\"card\"><h2>All Endpoints</h2>\n";
        f << "<table><tr><th>Instance</th><th>Pin</th><th>Arrival(ps)</th>"
          << "<th>Required(ps)</th><th>SetupSlk(ps)</th><th>HoldSlk(ps)</th><th>Status</th></tr>\n";

        for (TimingNode* node : timer.nodes) {
            bool isSetupEP = timer.isEndpoint(node);
            bool isHoldEP  = (node->holdSlack < INF);
            if (!isSetupEP && !isHoldEP) continue;

            std::string inst = node->pin->inst ? node->pin->inst->name : "(po)";
            bool setupOk = node->slack >= 0.0;
            bool holdOk  = (node->holdSlack >= 0.0 || node->holdSlack == INF);

            std::string rowClass, status;
            if (!setupOk && !holdOk) { rowClass = "viol"; status = "SETUP+HOLD"; }
            else if (!setupOk)       { rowClass = "viol"; status = "SETUP_VIOL"; }
            else if (!holdOk)        { rowClass = "hold"; status = "HOLD_VIOL"; }
            else                     { rowClass = "met";  status = "MET"; }

            f << "<tr class=\"" << rowClass << "\">"
              << "<td>" << inst << "</td>"
              << "<td>" << node->pin->name << "</td>"
              << "<td>" << std::fixed << std::setprecision(2) << node->arrivalTime << "</td>"
              << "<td>" << (node->requiredTime == INF ? 0.0 : node->requiredTime) << "</td>"
              << "<td>" << node->slack << "</td>"
              << "<td>" << (node->holdSlack == INF ? 0.0 : node->holdSlack) << "</td>"
              << "<td>" << status << "</td></tr>\n";
        }
        f << "</table></div>\n";
    }

    // CDC section
    f << "<div class=\"card\"><h2>Clock Domain Crossing</h2><pre>"
      << formatCdcReport() << "</pre></div>\n";

    f << "</body></html>\n";
    return true;
}
