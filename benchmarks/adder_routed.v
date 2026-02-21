module full_adder (
    input clk,
    input A,
    input B,
    input Cin,
    output Sum,
    output Cout
);

// Wires
wire n1;
wire n2;
wire n3;
wire n1_opt;
wire n3_opt;

// Gates
XOR2 g1 (.A(A), .B(B), .Y(n1));
XOR2 g2 (.A(n1_opt), .B(Cin), .Y(Sum));
AND2 g3 (.A(A), .B(B), .Y(n2));
AND2 g4 (.A(n1), .B(Cin), .Y(n3));
OR2 g5 (.A(n2), .B(n3_opt), .Y(Cout));
BUF buf_0 (.A(n1), .Y(n1_opt));
BUF buf_1 (.A(n3), .Y(n3_opt));

endmodule
