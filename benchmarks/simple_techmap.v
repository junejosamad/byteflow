// Techmap: map RTLIL primitives to simple.lib cells

module \$_NOT_ (A, Y);
  input A; output Y;
  NOT _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule

module \$_AND_ (A, B, Y);
  input A, B; output Y;
  AND2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_OR_ (A, B, Y);
  input A, B; output Y;
  OR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_NAND_ (A, B, Y);
  input A, B; output Y;
  NAND2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_NOR_ (A, B, Y);
  input A, B; output Y;
  NOR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_XOR_ (A, B, Y);
  input A, B; output Y;
  XOR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_BUF_ (A, Y);
  input A; output Y;
  BUF _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule

module \$_MUX_ (A, B, S, Y);
  input A, B, S; output Y;
  // MUX2 not in library; implement as: Y = (S ? B : A) = (S&B) | (~S&A)
  wire _s_n, _sa, _sb;
  NOT  _not_s  (.A(S),   .Y(_s_n));
  AND2 _and_a  (.A(A),   .B(_s_n), .Y(_sa));
  AND2 _and_b  (.A(B),   .B(S),    .Y(_sb));
  OR2  _or_out (.A(_sa), .B(_sb),  .Y(Y));
endmodule

module \$_DFF_P_ (C, D, Q);
  input C, D; output Q;
  DFF _TECHMAP_REPLACE_ (.C(C), .D(D), .Q(Q));
endmodule

// Synchronous-reset DFF (reset=1 → Q=0) mapped to DFF + AND
// $_SDFF_PP0_: posedge clock, active-high sync reset, reset to 0
module \$_SDFF_PP0_ (C, D, R, Q);
  input C, D, R; output Q;
  wire _d_gated, _r_n;
  NOT  _not_r (.A(R),       .Y(_r_n));
  AND2 _and_d (.A(D), .B(_r_n), .Y(_d_gated));
  DFF  _dff   (.C(C), .D(_d_gated), .Q(Q));
endmodule

// Async-reset DFF: fall back to DFF+AND (approximate)
module \$_DFF_PP0_ (C, D, R, Q);
  input C, D, R; output Q;
  wire _d_gated, _r_n;
  NOT  _not_r (.A(R),       .Y(_r_n));
  AND2 _and_d (.A(D), .B(_r_n), .Y(_d_gated));
  DFF  _dff   (.C(C), .D(_d_gated), .Q(Q));
endmodule
