# Read the behavioral Verilog
read_verilog benchmarks/alu_32bit.v

# Elaborate the design
hierarchy -check -top alu_32bit

# Synthesize the design to generic logic
synth -top alu_32bit

# Map flip-flops to our simple DFF cell
dfflibmap -liberty benchmarks/simple.lib

# Map combinatorial logic to our standard cells (AND2, OR2, XOR2, etc.)
abc -liberty benchmarks/simple.lib

# Clean up unused wires
opt_clean

# Write the final structural netlist that OpenEDA will consume
write_verilog -noattr benchmarks/alu_32bit_structural.v
