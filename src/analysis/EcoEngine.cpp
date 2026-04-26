#include "analysis/EcoEngine.h"
#include "place/Legalizer.h"
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <climits>

// ============================================================
// Helpers: drive-strength parsing for sky130 cell names
// ============================================================

int EcoEngine::getDriveStrength(const std::string& name) {
    // sky130: sky130_fd_sc_hd__inv_2  →  2
    // generic: BUF, NOT, etc          →  1
    auto pos = name.rfind('_');
    if (pos == std::string::npos || pos + 1 >= name.size()) return 1;
    try { return std::stoi(name.substr(pos + 1)); }
    catch (...) { return 1; }
}

std::string EcoEngine::getCellBase(const std::string& name) {
    // sky130_fd_sc_hd__inv_2  →  "sky130_fd_sc_hd__inv_"
    auto pos = name.rfind('_');
    if (pos == std::string::npos) return name;
    try {
        std::stoi(name.substr(pos + 1));
        return name.substr(0, pos + 1);  // include trailing '_'
    } catch (...) {
        return name;
    }
}

CellDef* EcoEngine::findUpsizedCell(Library* lib, CellDef* current) {
    if (!lib || !current) return nullptr;
    int curStrength = getDriveStrength(current->name);
    std::string base = getCellBase(current->name);

    // Try next power-of-two drive strengths
    static const int strengths[] = {1, 2, 4, 6, 8, 12, 16};
    bool found = false;
    for (int s : strengths) {
        if (s <= curStrength) continue;
        std::string candidate = base + std::to_string(s);
        CellDef* c = lib->getCell(candidate);
        if (c && !c->isSequential) return c;
    }
    return nullptr;
}

CellDef* EcoEngine::findBufferCell(Library* lib) {
    if (!lib) return nullptr;
    // Prefer sky130 buf cells in increasing drive order
    for (const std::string& name : {
            "sky130_fd_sc_hd__buf_2",
            "sky130_fd_sc_hd__buf_1",
            "sky130_fd_sc_hd__buf_4",
            "BUF", "buf" }) {
        CellDef* c = lib->getCell(name);
        if (c) return c;
    }
    // Fallback: any cell named buf* that is not sequential
    for (auto& [n, c] : lib->cells) {
        if (!c->isSequential && n.find("buf") != std::string::npos) return c;
    }
    return nullptr;
}

// ============================================================
// Buffer insertion
// ============================================================

GateInstance* EcoEngine::insertBuffer(Design* chip, Pin* targetPin,
                                       CellDef* bufDef, int& seqId) {
    if (!targetPin || !targetPin->net || !targetPin->inst) return nullptr;

    Net* originalNet = targetPin->net;
    std::string bufName  = "eco_buf_" + std::to_string(seqId);
    std::string newNetName = "eco_net_" + std::to_string(seqId);
    seqId++;

    // GateInstance constructor creates all pins from CellDef::pins automatically
    GateInstance* bufInst = new GateInstance(bufName, bufDef);
    bufInst->x = targetPin->inst->x + (bufDef->width + 0.5);
    bufInst->y = targetPin->inst->y;
    bufInst->isPlaced = true;
    chip->addInstance(bufInst);

    // Find the input and output pins created by the constructor
    Pin* bufIn  = nullptr;
    Pin* bufOut = nullptr;
    for (Pin* p : bufInst->pins) {
        if (p->type == PinType::INPUT  && !bufIn)  bufIn  = p;
        if (p->type == PinType::OUTPUT && !bufOut) bufOut = p;
    }
    if (!bufIn || !bufOut) return nullptr;

    // New net: buf_output → original FF D-pin
    Net* newNet = chip->createNet(newNetName);

    // Disconnect targetPin from originalNet
    originalNet->connectedPins.erase(
        std::remove(originalNet->connectedPins.begin(),
                    originalNet->connectedPins.end(), targetPin),
        originalNet->connectedPins.end());

    // Wire up: originalNet → bufIn → newNet → targetPin
    bufIn->net  = originalNet;
    originalNet->connectedPins.push_back(bufIn);

    bufOut->net = newNet;
    newNet->connectedPins.push_back(bufOut);

    targetPin->net = newNet;
    newNet->connectedPins.push_back(targetPin);

    return bufInst;
}

// ============================================================
// Single-pass setup fix: upsize worst gate on each crit path
// ============================================================

int EcoEngine::fixSetupViolations(Design* chip, Timer& timer) {
    int resized = 0;
    if (!chip->cellLibrary) return 0;

    // Collect setup-violating endpoints from Pin slack (written back by STA)
    // For each violation, find the gate with max gateDelay on the path
    // and try to upsize it.

    // Build a map: pin → instance for fast lookup
    // Walk all instances; for each, check its output pins for worst slack
    struct Candidate { GateInstance* inst; double slack; };
    std::vector<Candidate> violators;

    for (GateInstance* inst : chip->instances) {
        if (!inst->type || inst->type->isSequential) continue;
        for (Pin* p : inst->pins) {
            if (p->type == PinType::OUTPUT && p->slack < 0.0) {
                violators.push_back({inst, p->slack});
                break;
            }
        }
    }

    // Sort by worst slack first
    std::sort(violators.begin(), violators.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.slack < b.slack;
              });

    // Try to upsize each violating gate (up to 20 per pass)
    int attempts = std::min((int)violators.size(), 20);
    for (int i = 0; i < attempts; i++) {
        GateInstance* inst = violators[i].inst;
        CellDef* larger = findUpsizedCell(chip->cellLibrary, inst->type);
        if (!larger) continue;

        std::cout << "  [ECO] Upsize " << inst->name
                  << ": " << inst->type->name
                  << " -> " << larger->name
                  << "  (setup_slack=" << std::fixed << std::setprecision(1)
                  << violators[i].slack << " ps)\n";

        inst->type = larger;
        resized++;
    }
    return resized;
}

// ============================================================
// Single-pass hold fix: insert buffers on hold-violating paths
// ============================================================

int EcoEngine::fixHoldViolations(Design* chip, Timer& timer) {
    int inserted = 0;
    if (!chip->cellLibrary) return 0;

    CellDef* bufDef = findBufferCell(chip->cellLibrary);
    if (!bufDef) {
        std::cout << "  [ECO] No buffer cell found in library — skipping hold fix\n";
        return 0;
    }

    // Collect hold-violating D-pins BEFORE modifying chip->instances.
    // Inserting buffers inside the loop would invalidate the iterator.
    std::vector<Pin*> targets;
    for (GateInstance* inst : chip->instances) {
        if (!inst->type || !inst->type->isSequential) continue;
        for (Pin* p : inst->pins) {
            if (p->name == "D" && p->holdSlack < 0.0 && p->holdSlack < 1e29)
                targets.push_back(p);
        }
    }

    for (Pin* p : targets) {
        double deficit = -p->holdSlack + 10.0;
        std::cout << "  [ECO] Insert buffer before " << p->inst->name
                  << "/D  (hold_slack=" << std::fixed << std::setprecision(1)
                  << p->holdSlack << " ps, need +" << deficit << " ps)\n";
        GateInstance* buf = insertBuffer(chip, p, bufDef, ecoSeq);
        if (buf) {
            inserted++;
            lastInserted_.push_back(buf);  // tracked for patchGraph
        }
    }

    // Legalize after all insertions to eliminate cell-cell overlaps.
    // Buffers are placed initially at (FF.x + offset, FF.y) which can
    // collide with adjacent cells; Legalizer snaps them to legal row slots.
    if (inserted > 0 && chip->coreWidth > 0 && chip->coreHeight > 0) {
        Legalizer leg(chip, chip->coreWidth, chip->coreHeight);
        leg.run();
        std::cout << "  [ECO] Legalized " << inserted
                  << " inserted buffer(s) — cell overlaps resolved\n";
    }

    return inserted;
}

// ============================================================
// Timing closure loop — Phase 3.4: incremental STA
// ============================================================
//
// Previous implementation: called buildGraph() + updateTiming() on every
// iteration = O(N) node allocation per iteration.
//
// New implementation:
//   - Initial build: buildGraph() + updateTiming()  (once)
//   - After gate resize: updateTimingSkipBuild()     (no node allocation)
//   - After buffer insert: patchGraph(buf) + updateTimingSkipBuild()
//   - Final verification: buildGraph() + updateTiming() (once, for accuracy)
//
// Total full rebuilds: 2 regardless of maxIter.

EcoResult EcoEngine::runTimingClosure(Design* chip, Timer& timer, int maxIter) {
    EcoResult result;

    std::cout << "\n=== ECO TIMING CLOSURE ===\n";

    // Initial full build
    timer.buildGraph();
    timer.updateTiming();
    ++result.fullRebuildCount;

    for (int iter = 0; iter < maxIter; iter++) {
        result.iterations++;

        double setupWns = timer.getWNS();
        double holdWns  = timer.getHoldWNS();
        int    setupV   = timer.getViolationCount();
        int    holdV    = timer.getHoldViolationCount();

        std::cout << "  Iter " << iter + 1
                  << ": setup_wns=" << std::fixed << std::setprecision(1) << setupWns
                  << " ps (" << setupV << " viols)"
                  << "  hold_wns=" << holdWns
                  << " ps (" << holdV << " viols)\n";

        if (setupV == 0 && holdV == 0) {
            std::cout << "  Timing closure achieved.\n";
            break;
        }

        int changed = 0;

        if (setupV > 0) {
            int resized = fixSetupViolations(chip, timer);
            if (resized > 0) {
                // Gate resize only: topology unchanged → skip graph rebuild
                timer.updateTimingSkipBuild();
                ++result.incrUpdateCount;
                changed += resized;
            }
        }

        if (holdV > 0) {
            lastInserted_.clear();
            int inserted = fixHoldViolations(chip, timer);
            if (inserted > 0) {
                // Buffer insertion: patch newly-added nodes, then re-propagate
                for (GateInstance* buf : lastInserted_)
                    timer.patchGraph(buf);
                timer.updateTimingSkipBuild();
                ++result.incrUpdateCount;
                changed += inserted;
            }
        }

        result.setupFixed += changed;

        if (changed == 0) {
            std::cout << "  No further ECO changes possible — stopping.\n";
            break;
        }
    }

    // Final full rebuild for verified result (catches any edge-case drift)
    timer.buildGraph();
    timer.updateTiming();
    ++result.fullRebuildCount;

    result.finalSetupWns   = timer.getWNS();
    result.finalHoldWns    = timer.getHoldWNS();
    result.finalSetupViols = timer.getViolationCount();
    result.finalHoldViols  = timer.getHoldViolationCount();

    std::cout << "=== ECO COMPLETE: setup_wns=" << result.finalSetupWns
              << " ps  hold_wns=" << result.finalHoldWns << " ps"
              << "  total_changes=" << result.setupFixed + result.holdFixed
              << "  rebuilds=" << result.fullRebuildCount
              << "  incr_updates=" << result.incrUpdateCount
              << " ===\n";

    return result;
}
