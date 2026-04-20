# SDC constraints for sky130_inv_chain benchmark
# All time values in nanoseconds (standard SDC)

# 500 MHz clock (2 ns period) on port A (combinational, so virtual clock)
create_clock -period 2.0 -name clk [get_ports A]

# Primary inputs arrive 0.1 ns after clock edge
set_input_delay  0.1 -clock clk -max [get_ports A]
set_input_delay  0.1 -clock clk -max [get_ports B]
set_input_delay  0.1 -clock clk -max [get_ports C]

# Primary outputs must be stable 0.1 ns before clock edge
set_output_delay 0.1 -clock clk -max [get_ports Y]
set_output_delay 0.1 -clock clk -max [get_ports Z]

# Clock uncertainty (jitter + skew)
set_clock_uncertainty 0.05 [get_clocks clk]

# Clock source latency
set_clock_latency 0.02 [get_clocks clk]
