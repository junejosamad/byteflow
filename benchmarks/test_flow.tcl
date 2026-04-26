# OpenEDA example flow script
# Run from project root: open_eda.exe -script benchmarks/test_flow.tcl

puts "OpenEDA TCL Flow — sky130 inverter chain"

# Load process design kit
set lib_file pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib
set lef_file pdk/sky130/sky130_fd_sc_hd_merged.lef

read_liberty $lib_file
read_lef     $lef_file

# Load netlist
set design_file benchmarks/sky130_inv_chain.v
read_verilog $design_file

# Physical implementation
place_design
route_design

# Sign-off
report_timing -period 2000
check_drc
check_lvs

# Export results
write_gds  outputs/sky130_inv_chain.gds
write_spef outputs/sky130_inv_chain.spef

puts "Flow complete."
