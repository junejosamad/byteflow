module full_adder (
    input A,
    input B,
    input Cin,
    output Sum,
    output Cout
);

wire n1, n2, n3;

// Sum Logic: A ^ B ^ Cin
XOR2 g1 (.A(A), .B(B), .Y(n1));
XOR2 g2 (.A(n1), .B(Cin), .Y(Sum));

// Carry Logic: (A & B) | (Cin & (A ^ B))
AND2 g3 (.A(A), .B(B), .Y(n2));
AND2 g4 (.A(n1), .B(Cin), .Y(n3));
OR2  g5 (.A(n2), .B(n3), .Y(Cout));

endmodule