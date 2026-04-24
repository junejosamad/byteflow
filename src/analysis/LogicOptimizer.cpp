#include "analysis/LogicOptimizer.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <sstream>

// ============================================================
// Helpers
// ============================================================

static bool isBufCell(CellDef* cell) {
    if (!cell) return false;
    const std::string& n = cell->name;
    // Match BUF, BUF_X2, buf, buf_2, etc.
    if (n == "BUF" || n == "buf") return true;
    if (n.substr(0, 4) == "BUF_") return true;
    if (n.substr(0, 4) == "buf_") return true;
    return false;
}

static bool isSequentialCell(CellDef* cell) {
    return cell && cell->isSequential;
}

// Return the single output pin of a gate, or nullptr.
static Pin* outputPin(GateInstance* inst) {
    for (Pin* p : inst->pins)
        if (p->type == PinType::OUTPUT) return p;
    return nullptr;
}

// Return the single input pin of a gate (used for BUF where there is one input).
static Pin* inputPin(GateInstance* inst) {
    for (Pin* p : inst->pins)
        if (p->type == PinType::INPUT) return p;
    return nullptr;
}

// True if a net drives at least one primary output port or FF D-pin.
// We use a simple heuristic: any pin whose owning instance is sequential,
// or any net whose name starts with a primary output convention.
static bool netReachesEndpoint(Net* net) {
    if (!net) return false;
    for (Pin* p : net->connectedPins) {
        if (!p->inst) continue;                // primary port stub
        if (isSequentialCell(p->inst->type))   return true;
        if (p->type == PinType::INPUT && p->inst->type == nullptr) return true;
    }
    return false;
}

// ============================================================
// Dead Logic Removal
// ============================================================
//
// A gate is "dead" if its output net has zero sinks that reach an endpoint.
// We iterate to fixpoint (each pass may expose new dead logic upstream).

int LogicOptimizer::removeDeadLogic(Design* chip) {
    if (!chip) return 0;
    int totalRemoved = 0;

    bool changed = true;
    while (changed) {
        changed = false;

        // Build a set of "live" nets: nets that have at least one sink leading
        // to an endpoint (non-dead instance or sequential cell).
        // Strategy: a gate output net is live if any of its sinks is either
        // a sequential cell or another live gate.  We mark in two sweeps.

        // First, build set of instances we'll try to remove.
        std::vector<GateInstance*> toRemove;
        std::unordered_set<GateInstance*> removeSet;

        for (GateInstance* inst : chip->instances) {
            if (!inst->type || isSequentialCell(inst->type)) continue;
            Pin* out = outputPin(inst);
            if (!out || !out->net) continue;

            Net* outNet = out->net;
            bool hasSink = false;
            // If this net is a declared primary output, the gate is live.
            if (chip->primaryOutputNets.count(outNet->name)) hasSink = true;
            if (!hasSink) {
                for (Pin* sp : outNet->connectedPins) {
                    if (sp == out) continue;  // skip the driver itself
                    if (!sp->inst) { hasSink = true; break; }  // port stub
                    if (removeSet.count(sp->inst) == 0) { hasSink = true; break; }
                }
            }
            if (!hasSink) {
                toRemove.push_back(inst);
                removeSet.insert(inst);
            }
        }

        if (toRemove.empty()) break;

        for (GateInstance* inst : toRemove) {
            std::cout << "  [LogicOpt] Remove dead gate: " << inst->name
                      << " (" << inst->type->name << ")\n";

            // Disconnect all pins from their nets.
            for (Pin* p : inst->pins) {
                if (!p->net) continue;
                auto& pins = p->net->connectedPins;
                pins.erase(std::remove(pins.begin(), pins.end(), p), pins.end());
                p->net = nullptr;
            }

            // Remove from chip->instances.
            auto& insts = chip->instances;
            insts.erase(std::remove(insts.begin(), insts.end(), inst), insts.end());

            totalRemoved++;
            changed = true;
        }
    }

    // Remove nets that now have zero or one pin (no driver or no sink).
    auto& nets = chip->nets;
    int netsBefore = (int)nets.size();
    nets.erase(std::remove_if(nets.begin(), nets.end(), [&](Net* n) {
        if (n->connectedPins.size() <= 1) {
            chip->netMap.erase(n->name);
            return true;
        }
        return false;
    }), nets.end());

    return totalRemoved;
}

// ============================================================
// Buffer Chain Collapsing
// ============================================================
//
// Pattern: net_a --[BUF_1]--> net_b --[BUF_2]--> net_c --> sinks
// Action : remove BUF_1 and BUF_2; rewire sinks of net_c to net_a.
// We also handle single redundant buffers (BUF whose output drives only
// one non-buf sink — the BUF adds no useful drive strength in a unit-cap
// context and just adds delay).

int LogicOptimizer::collapseBufferChains(Design* chip) {
    if (!chip) return 0;
    int removed = 0;

    // Iterate: each pass collapses one level of buffering.
    bool changed = true;
    while (changed) {
        changed = false;

        // Build map: Net* → GateInstance* (the instance that drives this net)
        std::unordered_map<Net*, GateInstance*> netDriver;
        for (GateInstance* inst : chip->instances) {
            Pin* op = outputPin(inst);
            if (op && op->net) netDriver[op->net] = inst;
        }

        // Find buf instances whose INPUT net is also driven by a buf.
        std::vector<GateInstance*> toCollapse;
        for (GateInstance* inst : chip->instances) {
            if (!inst->type || !isBufCell(inst->type)) continue;
            Pin* inp = inputPin(inst);
            if (!inp || !inp->net) continue;
            auto it = netDriver.find(inp->net);
            if (it == netDriver.end()) continue;
            GateInstance* upstream = it->second;
            if (!upstream->type || !isBufCell(upstream->type)) continue;
            // upstream is also a BUF → collapse this buf
            toCollapse.push_back(inst);
        }

        if (toCollapse.empty()) break;

        for (GateInstance* bufInst : toCollapse) {
            Pin* inp = inputPin(bufInst);
            Pin* out = outputPin(bufInst);
            if (!inp || !out || !inp->net || !out->net) continue;

            Net* srcNet  = inp->net;   // the net feeding this buf's input
            Net* dstNet  = out->net;   // the net this buf drives

            std::cout << "  [LogicOpt] Collapse buffer " << bufInst->name
                      << ": merge " << dstNet->name << " -> " << srcNet->name << "\n";

            // Rewire all sinks of dstNet to srcNet.
            for (Pin* p : dstNet->connectedPins) {
                if (p == out) continue;  // skip the buf's own output pin
                p->net = srcNet;
                srcNet->connectedPins.push_back(p);
            }
            dstNet->connectedPins.clear();

            // Remove dstNet from design.
            chip->netMap.erase(dstNet->name);
            auto& nets = chip->nets;
            nets.erase(std::remove(nets.begin(), nets.end(), dstNet), nets.end());

            // Disconnect buf's input pin from srcNet.
            auto& srcPins = srcNet->connectedPins;
            srcPins.erase(std::remove(srcPins.begin(), srcPins.end(), inp), srcPins.end());

            // Remove buf from design.
            auto& insts = chip->instances;
            insts.erase(std::remove(insts.begin(), insts.end(), bufInst), insts.end());

            removed++;
            changed = true;
        }
    }

    return removed;
}

// ============================================================
// Full optimization pass
// ============================================================

OptimizeResult LogicOptimizer::optimize(Design* chip) {
    OptimizeResult res;
    if (!chip) return res;

    std::cout << "  [LogicOpt] Starting optimization ("
              << chip->instances.size() << " instances, "
              << chip->nets.size() << " nets)\n";

    res.deadGatesRemoved  = removeDeadLogic(chip);
    res.buffersCollapsed  = collapseBufferChains(chip);

    int netsAfter = (int)chip->nets.size();
    std::cout << "  [LogicOpt] Done: " << res.deadGatesRemoved
              << " dead gates removed, " << res.buffersCollapsed
              << " buffers collapsed\n";

    return res;
}

// ============================================================
// Legacy API (used by main.cpp)
// ============================================================

void LogicOptimizer::fixTiming(Design& design, Library& lib) {
    optimize(&design);
}
