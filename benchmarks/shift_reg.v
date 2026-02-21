module shift_reg (
    input clk,
    input in,
    output out
);
    wire n1, n2, n3;
    
    // A chain of 4 Registers
    DFF f1 (.C(clk), .D(in), .Q(n1));
    DFF f2 (.C(clk), .D(n1), .Q(n2));
    DFF f3 (.C(clk), .D(n2), .Q(n3));
    DFF f4 (.C(clk), .D(n3), .Q(out));
endmodule
