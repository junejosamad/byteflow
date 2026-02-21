module full_adder_structural ( A, B, Cin, Sum, Cout );
  // This is a 1-bit Full Adder using standard cells
  XOR2  g1 ( .A(A), .B(B), .Y(n1) );
  XOR2  g2 ( .A(n1), .B(Cin), .Y(Sum) );
  AND2  g3 ( .A(A), .B(B), .Y(n2) );
  AND2  g4 ( .A(n1), .B(Cin), .Y(n3) );
  OR2   g5 ( .A(n2), .B(n3), .Y(Cout) );
endmodule