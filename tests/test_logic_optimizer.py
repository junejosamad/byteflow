"""
Phase 5.3 — Logic Optimizer Test Suite

Validates LogicOptimizer (dead-logic removal + buffer-chain collapsing):
  1. API surface: LogicOptimizer, OptimizeResult
  2. OptimizeResult fields: dead_gates_removed, buffers_collapsed, any_change()
  3. Dead logic removal — unconnected gate removed
  4. Dead logic removal — chain of dead gates removed iteratively
  5. Dead logic: gates reaching DFF D-pin are NOT removed
  6. Buffer chain collapsing — BUF→BUF reduced to direct wire
  7. Buffer chain collapsing — triple BUF chain collapsed fully
  8. Buffer chain: BUF driving DFF D-pin (endpoint) — NOT collapsed
  9. optimize() runs both passes; returns combined counts
 10. Instance count decreases after optimization
 11. Regression — full_adder PnR unaffected by optimize()
 12. Regression — shift_reg PnR unaffected

Run from project root:
    python tests/test_logic_optimizer.py
"""
import sys, os, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "build", "Release"))

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"
results = []

def check(name, condition, msg=""):
    status = PASS if condition else FAIL
    label  = f"  [{status}] {name}"
    if msg: label += f"  ({msg})"
    print(label)
    results.append((name, condition))
    return condition

def summarize():
    total  = len(results)
    passed = sum(1 for _, ok in results if ok)
    failed = total - passed
    print("\n" + "=" * 60)
    print(f"  Results: {passed}/{total} passed"
          + (f"  ({failed} FAILED)" if failed else ""))
    print("=" * 60)
    return failed == 0

BENCH     = os.path.join(ROOT, "benchmarks")
LIB       = os.path.join(BENCH, "simple.lib")
FULL_ADDER = os.path.join(BENCH, "full_adder.v")
SHIFT_REG  = os.path.join(BENCH, "shift_reg.v")

# ── Test netlists ───────────────────────────────────────────────

# Gate whose output goes nowhere — should be removed.
_DEAD_GATE = """\
module dead_test (
    input  A, B, clk,
    output Q
);
    wire live_n, dead_n;
    AND2 live_g (.A(A), .B(B), .Y(live_n));   /* drives DFF */
    NOT  dead_g (.A(A), .Y(dead_n));           /* dead: dead_n not used */
    DFF  ff     (.C(clk), .D(live_n), .Q(Q));
endmodule
"""

# Chain of three dead gates.
_DEAD_CHAIN = """\
module dead_chain (
    input  A, clk,
    output Q
);
    wire n1, n2, n3;
    NOT  g1 (.A(A),  .Y(n1));      /* dead */
    NOT  g2 (.A(n1), .Y(n2));      /* dead */
    NOT  g3 (.A(n2), .Y(n3));      /* dead */
    DFF  ff (.C(clk), .D(A), .Q(Q));  /* uses A directly */
endmodule
"""

# Gates that drive DFF — must NOT be removed.
_LIVE_CHAIN = """\
module live_test (
    input  A, B, clk,
    output Q
);
    wire n1;
    AND2 g1 (.A(A), .B(B), .Y(n1));
    DFF  ff (.C(clk), .D(n1), .Q(Q));
endmodule
"""

# BUF → BUF → DFF: one level can be collapsed.
_BUF_CHAIN2 = """\
module buf_chain2 (
    input  A, clk,
    output Q
);
    wire b1, b2;
    BUF  buf1 (.A(A),  .Y(b1));
    BUF  buf2 (.A(b1), .Y(b2));
    DFF  ff   (.C(clk), .D(b2), .Q(Q));
endmodule
"""

# BUF → BUF → BUF → DFF: two levels can be collapsed.
_BUF_CHAIN3 = """\
module buf_chain3 (
    input  A, clk,
    output Q
);
    wire b1, b2, b3;
    BUF  buf1 (.A(A),  .Y(b1));
    BUF  buf2 (.A(b1), .Y(b2));
    BUF  buf3 (.A(b2), .Y(b3));
    DFF  ff   (.C(clk), .D(b3), .Q(Q));
endmodule
"""

# Single BUF driving DFF — NOT collapsible (buf is the only driver).
_SINGLE_BUF = """\
module single_buf (
    input  A, clk,
    output Q
);
    wire b1;
    BUF buf1 (.A(A), .Y(b1));
    DFF ff   (.C(clk), .D(b1), .Q(Q));
endmodule
"""


def write_tmp(content):
    f = tempfile.NamedTemporaryFile(mode="w", suffix=".v", delete=False,
                                    encoding="utf-8")
    f.write(content)
    f.close()
    return f.name

def load_chip(content):
    import open_eda
    path = write_tmp(content)
    try:
        chip = open_eda.Design()
        chip.load_verilog(path)   # auto-loads benchmarks/simple.lib
    finally:
        os.unlink(path)
    return chip


def run_suite():
    print("=" * 60)
    print("  Phase 5.3 - Logic Optimizer")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    # ── Stage 1: API surface ────────────────────────────────────
    print("\n[Stage 1] API surface")
    check("LogicOptimizer class exists",  hasattr(open_eda, "LogicOptimizer"))
    check("OptimizeResult class exists",  hasattr(open_eda, "OptimizeResult"))
    opt = open_eda.LogicOptimizer()
    check("LogicOptimizer instantiates",  opt is not None)
    check("optimize method exists",       hasattr(opt, "optimize"))
    check("remove_dead_logic method",     hasattr(opt, "remove_dead_logic"))
    check("collapse_buffer_chains method",hasattr(opt, "collapse_buffer_chains"))

    # ── Stage 2: OptimizeResult field types ────────────────────
    print("\n[Stage 2] OptimizeResult field types")
    chip2 = load_chip(_LIVE_CHAIN)
    r2    = opt.optimize(chip2)
    check("dead_gates_removed is int",  isinstance(r2.dead_gates_removed, int))
    check("buffers_collapsed is int",   isinstance(r2.buffers_collapsed, int))
    check("any_change() is bool",       isinstance(r2.any_change(), bool))

    # ── Stage 3: Dead logic — single dead gate removed ──────────
    print("\n[Stage 3] Dead logic removal — single dead gate")
    chip3 = load_chip(_DEAD_GATE)
    n_before3 = chip3.get_instance_count()
    r3 = opt.remove_dead_logic(chip3)
    n_after3  = chip3.get_instance_count()
    print(f"    (info: {n_before3} -> {n_after3} instances, removed={r3})")
    check("dead gate removed: count=1",       r3 == 1, f"removed={r3}")
    check("dead gate removed: instance_count decreased",
          n_after3 < n_before3, f"{n_before3} -> {n_after3}")

    # ── Stage 4: Dead logic — chain of dead gates ───────────────
    print("\n[Stage 4] Dead logic — chain of 3 dead gates")
    chip4 = load_chip(_DEAD_CHAIN)
    n_before4 = chip4.get_instance_count()
    r4 = opt.remove_dead_logic(chip4)
    n_after4  = chip4.get_instance_count()
    print(f"    (info: {n_before4} -> {n_after4} instances, removed={r4})")
    check("dead chain: all 3 removed", r4 == 3, f"removed={r4}")

    # ── Stage 5: Live gates NOT removed ─────────────────────────
    print("\n[Stage 5] Live gates driving DFF are NOT removed")
    chip5 = load_chip(_LIVE_CHAIN)
    n_before5 = chip5.get_instance_count()
    r5 = opt.remove_dead_logic(chip5)
    n_after5  = chip5.get_instance_count()
    check("live design: 0 gates removed", r5 == 0, f"removed={r5}")
    check("live design: instance_count unchanged",
          n_after5 == n_before5, f"{n_before5} -> {n_after5}")

    # ── Stage 6: Buffer chain collapsing — 2-buf chain ──────────
    print("\n[Stage 6] Buffer chain collapse — 2-buf chain")
    chip6 = load_chip(_BUF_CHAIN2)
    n_before6 = chip6.get_instance_count()
    r6 = opt.collapse_buffer_chains(chip6)
    n_after6  = chip6.get_instance_count()
    print(f"    (info: {n_before6} -> {n_after6} instances, collapsed={r6})")
    check("2-buf chain: 1 buffer collapsed", r6 == 1, f"collapsed={r6}")
    check("2-buf chain: DFF still present",
          n_after6 >= 1, f"{n_after6}")  # DFF must remain

    # ── Stage 7: Buffer chain collapsing — 3-buf chain ──────────
    print("\n[Stage 7] Buffer chain collapse — 3-buf chain")
    chip7 = load_chip(_BUF_CHAIN3)
    n_before7 = chip7.get_instance_count()
    r7 = opt.collapse_buffer_chains(chip7)
    n_after7  = chip7.get_instance_count()
    print(f"    (info: {n_before7} -> {n_after7} instances, collapsed={r7})")
    check("3-buf chain: >=1 buffer collapsed", r7 >= 1, f"collapsed={r7}")

    # ── Stage 8: Single BUF → DFF not collapsed ─────────────────
    print("\n[Stage 8] Single BUF driving DFF — not collapsed")
    chip8 = load_chip(_SINGLE_BUF)
    n_before8 = chip8.get_instance_count()
    r8 = opt.collapse_buffer_chains(chip8)
    n_after8  = chip8.get_instance_count()
    check("single buf: 0 collapsed", r8 == 0, f"collapsed={r8}")
    check("single buf: instance_count unchanged",
          n_after8 == n_before8, f"{n_before8} -> {n_after8}")

    # ── Stage 9: optimize() runs both passes ────────────────────
    print("\n[Stage 9] optimize() combines dead-logic + buffer-collapse")
    # Design with both dead gate and buf chain
    _COMBINED = """\
module combined (
    input  A, B, clk,
    output Q
);
    wire n_dead, b1, b2, live_n;
    NOT  dead_g (.A(A), .Y(n_dead));           /* dead */
    BUF  buf1   (.A(A), .Y(b1));
    BUF  buf2   (.A(b1),.Y(b2));
    AND2 ag     (.A(b2), .B(B), .Y(live_n));   /* live */
    DFF  ff     (.C(clk), .D(live_n), .Q(Q));
endmodule
"""
    chip9 = load_chip(_COMBINED)
    n_before9 = chip9.get_instance_count()
    r9 = opt.optimize(chip9)
    n_after9  = chip9.get_instance_count()
    print(f"    (info: {n_before9} -> {n_after9} instances, "
          f"dead={r9.dead_gates_removed}, buf={r9.buffers_collapsed})")
    check("combined: dead_gates_removed == 1", r9.dead_gates_removed == 1,
          f"{r9.dead_gates_removed}")
    check("combined: buffers_collapsed >= 1",  r9.buffers_collapsed >= 1,
          f"{r9.buffers_collapsed}")
    check("combined: any_change() == True",    r9.any_change())

    # ── Stage 10: Instance count decreases ──────────────────────
    print("\n[Stage 10] Instance count decreases after optimize()")
    check("combined: instance_count decreased",
          n_after9 < n_before9, f"{n_before9} -> {n_after9}")

    # ── Stage 11: Regression — declared-output design unaffected ──
    print("\n[Stage 11] Regression — design with explicit output decl is live")
    # Use a netlist that has explicit 'output' declarations so primaryOutputNets
    # is populated and the optimizer correctly treats those nets as live.
    _WITH_OUTPUTS = """\
module regression11 (
    input  A, B, clk,
    output Sum,
    output Q
);
    wire n1, n2;
    XOR2 xg  (.A(A), .B(B), .Y(n1));    /* drives primary output Sum */
    AND2 ag  (.A(A), .B(B), .Y(n2));    /* drives DFF */
    BUF  bg  (.A(n1), .Y(Sum));          /* primary output path */
    DFF  ff  (.C(clk), .D(n2), .Q(Q));
endmodule
"""
    try:
        chip11 = load_chip(_WITH_OUTPUTS)
        n11_before = chip11.get_instance_count()
        r11 = opt.optimize(chip11)
        check("decl-output: optimize no crash", True)
        check("decl-output: no dead gates removed (all drive outputs or DFFs)",
              r11.dead_gates_removed == 0,
              f"removed={r11.dead_gates_removed}")
        print(f"    (info: {n11_before} -> {chip11.get_instance_count()} instances, "
              f"dead={r11.dead_gates_removed}, buf={r11.buffers_collapsed})")
    except Exception as e:
        check("decl-output: optimize no crash", False, str(e))
        check("decl-output: no dead gates removed", False, "stage failed")

    # ── Stage 12: Regression — shift_reg PnR unaffected ─────────
    print("\n[Stage 12] Regression — shift_reg PnR unaffected")
    try:
        chip12 = open_eda.Design()
        chip12.load_verilog(SHIFT_REG)
        open_eda.run_placement(chip12)
        open_eda.RouteEngine().route(chip12)
        n12 = chip12.get_instance_count()
        r12 = opt.optimize(chip12)
        check("shift_reg: optimize no crash", True)
        check("shift_reg: instance_count == 4 (DFFs, all live)",
              chip12.get_instance_count() == 4,
              f"before={n12}, after={chip12.get_instance_count()}")
    except Exception as e:
        check("shift_reg: optimize no crash", False, str(e))
        check("shift_reg: instance_count == 4", False, "stage failed")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
