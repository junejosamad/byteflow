# Read the behavioral Verilog
read_verilog benchmarks/counter_behavioral.v

# Convert high-level logic to generic gates
synth -top counter

# Map generic gates to our custom library (simple.lib)
dfflibmap -liberty benchmarks/simple.lib
abc -liberty benchmarks/simple.lib

# Cleanup
clean

# Write the structural netlist
write_verilog -noattr benchmarks/counter_structural.v
