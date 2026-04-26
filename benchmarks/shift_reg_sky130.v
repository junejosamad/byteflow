module shift_reg (
    input CLK,
    input in,
    output out
);
    wire n1, n2, n3;

    // 4-stage shift register using sky130 D flip-flops
    sky130_fd_sc_hd__dfxtp_1 f1 (.CLK(CLK), .D(in), .Q(n1));
    sky130_fd_sc_hd__dfxtp_1 f2 (.CLK(CLK), .D(n1), .Q(n2));
    sky130_fd_sc_hd__dfxtp_1 f3 (.CLK(CLK), .D(n2), .Q(n3));
    sky130_fd_sc_hd__dfxtp_1 f4 (.CLK(CLK), .D(n3), .Q(out));
endmodule
