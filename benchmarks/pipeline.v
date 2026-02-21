module pipeline_test ( input clk, input Din, output Qout );
  
  // 1. First Flip-Flop (Launches signal)
  DFF  reg1 ( .C(clk), .D(Din), .Q(n1) );
  
  // 2. Combinational Cloud (Adds Delay)
  // Logic Depth = 3 gates (approx 2.5 + 1.8 + 1.8 = 6.1ns)
  XOR2 g1 ( .A(n1), .B(n1), .Y(n2) );
  AND2 g2 ( .A(n2), .B(n2), .Y(n3) );
  OR2  g3 ( .A(n3), .B(n3), .Y(n4) );
  
  // 3. Second Flip-Flop (Captures signal)
  DFF  reg2 ( .C(clk), .D(n4), .Q(Qout) );

endmodule