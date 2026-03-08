module soc_sram (
    input clk,
    input A,
    input B,
    input Cin,
    output Sum,
    output Cout
);

// Wires
wire clk_buf;
wire en;
wire en_buf;
wire addr;
wire addr_buf;
wire din;
wire din_buf;
wire dout_raw;
wire dout_inv;
wire dout_final;
wire VDD;
wire VSS;

// Gates
BUF b_din (.A(din), .Y(din_buf));
BUF b_out (.A(dout_inv), .Y(dout_final));
NOT n_out (.A(dout_raw), .Y(dout_inv));
SRAM1024x32 mem (.clk(clk_buf), .en(en_buf), .addr(addr_buf), .din(din_buf), .dout(dout_raw));
BUF b_addr (.A(addr), .Y(addr_buf));
BUF b_en (.A(en), .Y(en_buf));
BUF b_clk (.A(clk), .Y(clk_buf));

endmodule
