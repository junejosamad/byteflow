module full_adder_structural (
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
wire VDD;
wire VSS;

// Gates
XOR2 g2 (.A(n1), .B(Cin), .Y(Sum));
AND2 g4 (.A(n1), .B(Cin), .Y(n3));
XOR2 g1 (.A(A), .B(B), .Y(n1));
AND2 g3 (.A(A), .B(B), .Y(n2));
OR2 g5 (.A(n2), .B(n3), .Y(Cout));

endmodule
