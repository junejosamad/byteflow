#pragma once
#include <string>
#include <vector>

// ============================================================
// SDC Constraint Structures
// ============================================================

struct ClockDef {
    std::string name;
    double      period_ps   = 1000.0;  // clock period in picoseconds
    std::string port;                  // source port/pin name
    double      rise_ps     = 0.0;
    double      fall_ps     = 0.0;    // set to period/2 by default
    double      uncertainty_ps = 0.0; // set_clock_uncertainty override
    double      latency_ps    = 0.0;  // set_clock_latency override
};

struct InputDelaySdc {
    std::string port;       // empty = applies to all primary inputs
    std::string clock;      // clock name constraint is relative to
    double      delay_ps = 0.0;
    bool        is_max   = true;   // max = setup path, min = hold path
};

struct OutputDelaySdc {
    std::string port;
    std::string clock;
    double      delay_ps = 0.0;
    bool        is_max   = true;
};

struct FalsePathSdc {
    std::string from;  // port or cell name ("" = any)
    std::string to;
};

struct MulticyclePathSdc {
    std::string from;
    std::string to;
    int         multiplier = 1;
    bool        is_setup   = true;
};

struct SdcConstraints {
    std::vector<ClockDef>         clocks;
    std::vector<InputDelaySdc>    inputDelays;
    std::vector<OutputDelaySdc>   outputDelays;
    std::vector<FalsePathSdc>     falsePaths;
    std::vector<MulticyclePathSdc> multicyclePaths;

    double globalClockUncertainty_ps = 0.0;
    double globalClockLatency_ps     = 0.0;

    bool empty() const { return clocks.empty(); }

    // Return first clock (most designs have one)
    const ClockDef* primaryClock() const {
        return clocks.empty() ? nullptr : &clocks[0];
    }

    const ClockDef* findClock(const std::string& name) const {
        for (const auto& c : clocks)
            if (c.name == name) return &c;
        return nullptr;
    }

    // Effective clock period for a given clock name (or primary clock)
    double clockPeriod(const std::string& clkName = "") const {
        const ClockDef* c = clkName.empty() ? primaryClock() : findClock(clkName);
        return c ? c->period_ps : 1000.0;
    }

    // Effective input delay for a port (returns 0 if not constrained)
    double inputDelay(const std::string& port, bool max = true) const {
        for (const auto& d : inputDelays)
            if ((d.port.empty() || d.port == port) && d.is_max == max)
                return d.delay_ps;
        return 0.0;
    }

    // Effective output delay for a port (returns 0 if not constrained)
    double outputDelay(const std::string& port, bool max = true) const {
        for (const auto& d : outputDelays)
            if ((d.port.empty() || d.port == port) && d.is_max == max)
                return d.delay_ps;
        return 0.0;
    }

    // Check if a path from→to is a false path
    bool isFalsePath(const std::string& from, const std::string& to) const {
        for (const auto& fp : falsePaths) {
            bool fromMatch = fp.from.empty() || fp.from == from;
            bool toMatch   = fp.to.empty()   || fp.to   == to;
            if (fromMatch && toMatch) return true;
        }
        return false;
    }

    // Multicycle multiplier for a path (1 = normal single-cycle)
    int multicycleMultiplier(const std::string& from, const std::string& to,
                             bool setup = true) const {
        for (const auto& mc : multicyclePaths) {
            if (mc.is_setup != setup) continue;
            bool fromMatch = mc.from.empty() || mc.from == from;
            bool toMatch   = mc.to.empty()   || mc.to   == to;
            if (fromMatch && toMatch) return mc.multiplier;
        }
        return 1;
    }
};

// ============================================================
// SDC Parser — supports the most common synthesis/STA subset
// ============================================================
class SdcParser {
public:
    bool parse(const std::string& filename, SdcConstraints& out);

private:
    // Time unit detected from Liberty or assumed (ps by default)
    double timeUnitScale = 1000.0; // SDC default: ns → ps

    void parseLine(const std::string& line, SdcConstraints& out);
    void parseCreateClock(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetInputDelay(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetOutputDelay(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetFalsePath(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetMulticyclePath(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetClockUncertainty(const std::vector<std::string>& tok, SdcConstraints& out);
    void parseSetClockLatency(const std::vector<std::string>& tok, SdcConstraints& out);

    // Helpers
    std::vector<std::string> tokenize(const std::string& line) const;
    // Extract the first word inside [get_ports X], [get_pins X], [get_clocks X]
    std::string extractTarget(const std::string& arg) const;
    double toPs(double val) const { return val * timeUnitScale; }
};
