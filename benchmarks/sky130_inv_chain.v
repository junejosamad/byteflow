// sky130 validation benchmark — inverter chain + NAND tree
// Uses real sky130_fd_sc_hd__ cell names from the sky130 PDK.
// Tests: Liberty parse, LEF geometry, placement, routing, GDSII export.

module sky130_inv_chain (
    input  A,
    input  B,
    input  C,
    output Y,
    output Z
);

    wire n1, n2, n3, n4, n5;

    sky130_fd_sc_hd__inv_1   u_inv0 (.A(A),  .Y(n1));
    sky130_fd_sc_hd__inv_1   u_inv1 (.A(n1), .Y(n2));
    sky130_fd_sc_hd__inv_2   u_inv2 (.A(n2), .Y(n3));
    sky130_fd_sc_hd__nand2_1 u_nd0  (.A(B),  .B(n3), .Y(n4));
    sky130_fd_sc_hd__nand2_1 u_nd1  (.A(C),  .B(n4), .Y(n5));
    sky130_fd_sc_hd__buf_1   u_buf0 (.A(n5), .X(Y));
    sky130_fd_sc_hd__nor2_1  u_nor0 (.A(n3), .B(n4), .Y(Z));

endmodule
