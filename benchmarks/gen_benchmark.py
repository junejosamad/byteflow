"""
Generate a structural Verilog netlist for analytical placer benchmarking.
Produces a balanced binary tree of AND2/OR2/XOR2/NAND2 gates feeding
a register chain, giving realistic fanout and timing structure.

Usage:
  python benchmarks/gen_benchmark.py --cells 200 --out benchmarks/bench_200.v
  python benchmarks/gen_benchmark.py --cells 500 --out benchmarks/bench_500.v
"""
import argparse
import random
import math

def generate(num_cells: int, seed: int = 42) -> str:
    random.seed(seed)

    COMB_CELLS  = ["AND2", "OR2", "XOR2", "NAND2", "NOR2", "BUF", "NOT"]
    COMB_WEIGHT = [3,      3,     2,      2,       2,      1,     1   ]
    SEQ_RATIO   = 0.15  # ~15% flip-flops

    num_ff   = max(2, int(num_cells * SEQ_RATIO))
    num_comb = num_cells - num_ff

    lines = []
    lines.append(f"// Auto-generated {num_cells}-cell structural benchmark")
    lines.append(f"// {num_comb} combinational + {num_ff} flip-flops")
    lines.append(f"module bench_{num_cells}(")
    lines.append(f"    input  wire clk,")

    num_pi = max(4, int(math.sqrt(num_cells)))
    num_po = max(2, num_pi // 2)

    for i in range(num_pi):
        lines.append(f"    input  wire pi{i},")
    for i in range(num_po - 1):
        lines.append(f"    output wire po{i},")
    lines.append(f"    output wire po{num_po-1}")
    lines.append(");")
    lines.append("")

    # Internal wires
    net_idx = 0
    def new_net():
        nonlocal net_idx
        n = f"n{net_idx}"
        net_idx += 1
        return n

    # Available driver nets (start with primary inputs)
    available = [f"pi{i}" for i in range(num_pi)]
    all_nets  = list(available)

    inst_lines = []

    # Place combinational cells
    for i in range(num_comb):
        ctype = random.choices(COMB_CELLS, weights=COMB_WEIGHT)[0]
        out   = new_net()
        all_nets.append(out)

        if ctype in ("BUF", "NOT"):
            a = random.choice(available)
            inst_lines.append(f"  {ctype} u{i} ( .A({a}), .Y({out}) );")
        else:
            a = random.choice(available)
            b = random.choice(available)
            inst_lines.append(
                f"  {ctype} u{i} ( .A({a}), .B({b}), .Y({out}) );")

        available.append(out)
        # Keep pool bounded so later cells still see good fanin variety
        if len(available) > num_pi + 40:
            available.pop(0)

    # Place flip-flops
    ff_q_nets = []
    base = num_comb
    for i in range(num_ff):
        d   = random.choice(available)
        q   = new_net()
        all_nets.append(q)
        ff_q_nets.append(q)
        inst_lines.append(
            f"  DFF ff{i} ( .C(clk), .D({d}), .Q({q}) );")
        available.append(q)

    # Connect primary outputs to last available nets
    drivers = available[-num_po:]
    for i in range(num_po):
        inst_lines.append(
            f"  BUF po_buf{i} ( .A({drivers[i % len(drivers)]}), .Y(po{i}) );")

    # Declare all internal wires
    lines.append(f"  wire {', '.join(all_nets)};")
    lines.append("")
    lines.extend(inst_lines)
    lines.append("")
    lines.append("endmodule")

    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cells", type=int, default=200)
    parser.add_argument("--out",   type=str, default=None)
    parser.add_argument("--seed",  type=int, default=42)
    args = parser.parse_args()

    verilog = generate(args.cells, args.seed)

    out_path = args.out or f"benchmarks/bench_{args.cells}.v"
    with open(out_path, "w") as f:
        f.write(verilog)
    print(f"Generated {args.cells}-cell benchmark -> {out_path}")
