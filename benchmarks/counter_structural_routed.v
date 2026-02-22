module counter (
    input clk,
    input A,
    input B,
    input Cin,
    output Sum,
    output Cout
);

// Wires
wire cnt[0];
wire reset;
wire _00_;
wire cnt[1];
wire _04_;
wire _05_;
wire _06_;
wire _01_;
wire cnt[2];
wire _07_;
wire cnt[3];
wire _08_;
wire _02_;
wire _09_;
wire _10_;
wire _03_;
wire clk_buf_1_out;
wire clk_buf_2_out;
wire clk_buf_0_out;
wire VDD;
wire VSS;

// Gates
OR2 _14_ (.A(reset), .B(_05_), .Y(_06_));
NOR2 _18_ (.A(reset), .Y(_02_));
NOR2 _13_ (.A(cnt[0]), .Y(_05_));
DFF _25_ (.C(clk_buf_1_out), .D(_02_), .Q(cnt[3]));
DFF _22_ (.C(clk_buf_1_out), .D(_00_), .Q(cnt[0]));
NOR2 _11_ (.A(cnt[0]), .Y(_00_));
NOR2 _20_ (.A(reset), .Y(_10_));
AND2 _12_ (.A(cnt[0]), .B(cnt[1]), .Y(_04_));
AND2 _21_ (.A(_07_), .B(_10_), .Y(_03_));
XOR2 _17_ (.A(cnt[3]), .B(_07_), .Y(_08_));
NAND2 _16_ (.A(cnt[2]), .Y(_07_));
DFF _24_ (.C(clk_buf_2_out), .D(_03_), .Q(cnt[2]));
DFF _23_ (.C(clk_buf_2_out), .D(_01_), .Q(cnt[1]));
NOR2 _19_ (.A(cnt[2]), .Y(_09_));
NOR2 _15_ (.A(_04_), .Y(_01_));
CLKBUF clk_buf_0 (.A(clk), .Y(clk_buf_0_out));
CLKBUF clk_buf_1 (.A(clk_buf_0_out), .Y(clk_buf_1_out));
CLKBUF clk_buf_2 (.A(clk_buf_0_out), .Y(clk_buf_2_out));

endmodule
