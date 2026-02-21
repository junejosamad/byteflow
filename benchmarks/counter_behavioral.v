module counter(
    input clk,
    input reset,
    output [3:0] count
);

    reg [3:0] cnt;

    always @(posedge clk) begin
        if (reset)
            cnt <= 4'b0000;
        else
            cnt <= cnt + 1;
    end

    assign count = cnt;

endmodule
