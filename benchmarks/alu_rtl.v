// Behavioral ALU — 4-bit, 2-operation (AND/ADD)
// Used as synthesis input for test_synthesis.py
module alu_rtl (
    input  [3:0] A,
    input  [3:0] B,
    input        op,    // 0 = AND, 1 = ADD
    output [3:0] Y
);
    assign Y = op ? (A + B) : (A & B);
endmodule
