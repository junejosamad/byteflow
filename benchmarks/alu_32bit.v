module alu_32bit(
    input  wire        clk,
    input  wire [31:0] a,
    input  wire [31:0] b,
    input  wire [2:0]  opcode,
    output reg  [31:0] result
);

    always @(posedge clk) begin
        case (opcode)
            3'b000: result <= a + b;       // ADD
            3'b001: result <= a - b;       // SUB
            3'b010: result <= a & b;       // AND
            3'b011: result <= a | b;       // OR
            3'b100: result <= a ^ b;       // XOR
            3'b101: result <= a << 1;      // SHIFT LEFT
            3'b110: result <= a >> 1;      // SHIFT RIGHT
            default: result <= 32'b0;
        endcase
    end

endmodule
