// Behavioral DFF chain — 4 stages with sync reset
// Used as synthesis input for test_synthesis.py
module dff_chain_rtl (
    input  clk,
    input  reset,
    input  d,
    output q
);
    reg [3:0] pipe;

    always @(posedge clk) begin
        if (reset)
            pipe <= 4'b0;
        else begin
            pipe[0] <= d;
            pipe[1] <= pipe[0];
            pipe[2] <= pipe[1];
            pipe[3] <= pipe[2];
        end
    end

    assign q = pipe[3];
endmodule
