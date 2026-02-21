module shift_reg (
    input clk,
    input A,
    input B,
    input Cin,
    output Sum,
    output Cout
);

// Wires
wire in;
wire n1;
wire n2;
wire n3;
wire out;
wire clk_buf_1_out;
wire clk_buf_2_out;
wire clk_buf_0_out;
wire VDD;
wire VSS;
wire n1_opt;

// Gates
DFF f4 (.C(clk_buf_1_out), .D(n3), .Q(out));
DFF f3 (.C(clk_buf_1_out), .D(n2), .Q(n3));
DFF f2 (.C(clk_buf_2_out), .D(n1_opt), .Q(n2));
DFF f1 (.C(clk_buf_2_out), .D(in), .Q(n1));
CLKBUF clk_buf_0 (.A(clk), .Y(clk_buf_0_out));
CLKBUF clk_buf_1 (.A(clk_buf_0_out), .Y(clk_buf_1_out));
CLKBUF clk_buf_2 (.A(clk_buf_0_out), .Y(clk_buf_2_out));
BUF buf_0 (.A(n1), .Y(n1_opt));

endmodule
