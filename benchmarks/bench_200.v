// Auto-generated 200-cell structural benchmark
// 170 combinational + 30 flip-flops
module bench_200(
    input  wire clk,
    input  wire pi0,
    input  wire pi1,
    input  wire pi2,
    input  wire pi3,
    input  wire pi4,
    input  wire pi5,
    input  wire pi6,
    input  wire pi7,
    input  wire pi8,
    input  wire pi9,
    input  wire pi10,
    input  wire pi11,
    input  wire pi12,
    input  wire pi13,
    output wire po0,
    output wire po1,
    output wire po2,
    output wire po3,
    output wire po4,
    output wire po5,
    output wire po6
);

  wire pi0, pi1, pi2, pi3, pi4, pi5, pi6, pi7, pi8, pi9, pi10, pi11, pi12, pi13, n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n11, n12, n13, n14, n15, n16, n17, n18, n19, n20, n21, n22, n23, n24, n25, n26, n27, n28, n29, n30, n31, n32, n33, n34, n35, n36, n37, n38, n39, n40, n41, n42, n43, n44, n45, n46, n47, n48, n49, n50, n51, n52, n53, n54, n55, n56, n57, n58, n59, n60, n61, n62, n63, n64, n65, n66, n67, n68, n69, n70, n71, n72, n73, n74, n75, n76, n77, n78, n79, n80, n81, n82, n83, n84, n85, n86, n87, n88, n89, n90, n91, n92, n93, n94, n95, n96, n97, n98, n99, n100, n101, n102, n103, n104, n105, n106, n107, n108, n109, n110, n111, n112, n113, n114, n115, n116, n117, n118, n119, n120, n121, n122, n123, n124, n125, n126, n127, n128, n129, n130, n131, n132, n133, n134, n135, n136, n137, n138, n139, n140, n141, n142, n143, n144, n145, n146, n147, n148, n149, n150, n151, n152, n153, n154, n155, n156, n157, n158, n159, n160, n161, n162, n163, n164, n165, n166, n167, n168, n169, n170, n171, n172, n173, n174, n175, n176, n177, n178, n179, n180, n181, n182, n183, n184, n185, n186, n187, n188, n189, n190, n191, n192, n193, n194, n195, n196, n197, n198, n199;

  NAND2 u0 ( .A(pi0), .B(pi11), .Y(n0) );
  OR2 u1 ( .A(pi3), .B(pi2), .Y(n1) );
  NOR2 u2 ( .A(pi2), .B(pi13), .Y(n2) );
  AND2 u3 ( .A(pi2), .B(pi6), .Y(n3) );
  OR2 u4 ( .A(pi0), .B(n3), .Y(n4) );
  AND2 u5 ( .A(n3), .B(pi13), .Y(n5) );
  OR2 u6 ( .A(n4), .B(pi8), .Y(n6) );
  NOR2 u7 ( .A(pi0), .B(pi5), .Y(n7) );
  NAND2 u8 ( .A(pi10), .B(pi8), .Y(n8) );
  AND2 u9 ( .A(pi10), .B(pi3), .Y(n9) );
  AND2 u10 ( .A(pi3), .B(pi11), .Y(n10) );
  NOR2 u11 ( .A(n5), .B(pi8), .Y(n11) );
  NOR2 u12 ( .A(n9), .B(n0), .Y(n12) );
  XOR2 u13 ( .A(pi12), .B(pi2), .Y(n13) );
  XOR2 u14 ( .A(n12), .B(n6), .Y(n14) );
  NAND2 u15 ( .A(n13), .B(pi11), .Y(n15) );
  NAND2 u16 ( .A(n8), .B(pi2), .Y(n16) );
  AND2 u17 ( .A(pi7), .B(n10), .Y(n17) );
  OR2 u18 ( .A(pi5), .B(n0), .Y(n18) );
  BUF u19 ( .A(n10), .Y(n19) );
  OR2 u20 ( .A(n9), .B(pi10), .Y(n20) );
  OR2 u21 ( .A(pi13), .B(n3), .Y(n21) );
  NAND2 u22 ( .A(pi4), .B(pi10), .Y(n22) );
  XOR2 u23 ( .A(n1), .B(pi10), .Y(n23) );
  XOR2 u24 ( .A(n3), .B(n21), .Y(n24) );
  OR2 u25 ( .A(n6), .B(pi3), .Y(n25) );
  OR2 u26 ( .A(pi2), .B(n6), .Y(n26) );
  OR2 u27 ( .A(pi4), .B(pi13), .Y(n27) );
  BUF u28 ( .A(n22), .Y(n28) );
  BUF u29 ( .A(n6), .Y(n29) );
  AND2 u30 ( .A(n17), .B(n11), .Y(n30) );
  BUF u31 ( .A(n27), .Y(n31) );
  XOR2 u32 ( .A(n2), .B(pi8), .Y(n32) );
  OR2 u33 ( .A(n21), .B(n20), .Y(n33) );
  OR2 u34 ( .A(n23), .B(n13), .Y(n34) );
  BUF u35 ( .A(n11), .Y(n35) );
  OR2 u36 ( .A(pi8), .B(n18), .Y(n36) );
  XOR2 u37 ( .A(n34), .B(pi3), .Y(n37) );
  BUF u38 ( .A(pi9), .Y(n38) );
  NAND2 u39 ( .A(n36), .B(n29), .Y(n39) );
  OR2 u40 ( .A(pi4), .B(n10), .Y(n40) );
  OR2 u41 ( .A(n16), .B(n20), .Y(n41) );
  OR2 u42 ( .A(n23), .B(pi2), .Y(n42) );
  NAND2 u43 ( .A(pi10), .B(n32), .Y(n43) );
  BUF u44 ( .A(n38), .Y(n44) );
  OR2 u45 ( .A(n32), .B(n12), .Y(n45) );
  AND2 u46 ( .A(n19), .B(n2), .Y(n46) );
  XOR2 u47 ( .A(n39), .B(n39), .Y(n47) );
  OR2 u48 ( .A(n26), .B(n42), .Y(n48) );
  AND2 u49 ( .A(n1), .B(n35), .Y(n49) );
  OR2 u50 ( .A(n36), .B(n28), .Y(n50) );
  NAND2 u51 ( .A(n6), .B(n20), .Y(n51) );
  NOR2 u52 ( .A(n32), .B(n47), .Y(n52) );
  BUF u53 ( .A(pi13), .Y(n53) );
  NAND2 u54 ( .A(n31), .B(n1), .Y(n54) );
  AND2 u55 ( .A(n24), .B(n54), .Y(n55) );
  NOR2 u56 ( .A(n17), .B(n5), .Y(n56) );
  OR2 u57 ( .A(n39), .B(n8), .Y(n57) );
  AND2 u58 ( .A(n35), .B(n56), .Y(n58) );
  AND2 u59 ( .A(n53), .B(n39), .Y(n59) );
  NOR2 u60 ( .A(n14), .B(n48), .Y(n60) );
  XOR2 u61 ( .A(n42), .B(n17), .Y(n61) );
  OR2 u62 ( .A(n46), .B(n35), .Y(n62) );
  NOT u63 ( .A(n43), .Y(n63) );
  NOR2 u64 ( .A(n54), .B(n22), .Y(n64) );
  NAND2 u65 ( .A(n36), .B(n53), .Y(n65) );
  NAND2 u66 ( .A(n40), .B(n45), .Y(n66) );
  XOR2 u67 ( .A(n28), .B(n27), .Y(n67) );
  AND2 u68 ( .A(n15), .B(n51), .Y(n68) );
  XOR2 u69 ( .A(n52), .B(n29), .Y(n69) );
  AND2 u70 ( .A(n61), .B(n56), .Y(n70) );
  AND2 u71 ( .A(n21), .B(n19), .Y(n71) );
  BUF u72 ( .A(n22), .Y(n72) );
  XOR2 u73 ( .A(n36), .B(n61), .Y(n73) );
  XOR2 u74 ( .A(n54), .B(n28), .Y(n74) );
  NOR2 u75 ( .A(n57), .B(n57), .Y(n75) );
  XOR2 u76 ( .A(n72), .B(n52), .Y(n76) );
  NOR2 u77 ( .A(n35), .B(n29), .Y(n77) );
  AND2 u78 ( .A(n51), .B(n46), .Y(n78) );
  OR2 u79 ( .A(n54), .B(n71), .Y(n79) );
  AND2 u80 ( .A(n67), .B(n67), .Y(n80) );
  AND2 u81 ( .A(n52), .B(n73), .Y(n81) );
  OR2 u82 ( .A(n34), .B(n43), .Y(n82) );
  AND2 u83 ( .A(n63), .B(n57), .Y(n83) );
  AND2 u84 ( .A(n41), .B(n47), .Y(n84) );
  XOR2 u85 ( .A(n35), .B(n59), .Y(n85) );
  NOR2 u86 ( .A(n67), .B(n38), .Y(n86) );
  AND2 u87 ( .A(n67), .B(n86), .Y(n87) );
  AND2 u88 ( .A(n39), .B(n82), .Y(n88) );
  NOR2 u89 ( .A(n45), .B(n61), .Y(n89) );
  XOR2 u90 ( .A(n49), .B(n61), .Y(n90) );
  BUF u91 ( .A(n47), .Y(n91) );
  OR2 u92 ( .A(n62), .B(n54), .Y(n92) );
  BUF u93 ( .A(n89), .Y(n93) );
  XOR2 u94 ( .A(n67), .B(n84), .Y(n94) );
  NOT u95 ( .A(n91), .Y(n95) );
  XOR2 u96 ( .A(n87), .B(n73), .Y(n96) );
  AND2 u97 ( .A(n61), .B(n56), .Y(n97) );
  NOT u98 ( .A(n81), .Y(n98) );
  NOR2 u99 ( .A(n48), .B(n92), .Y(n99) );
  OR2 u100 ( .A(n49), .B(n83), .Y(n100) );
  XOR2 u101 ( .A(n80), .B(n57), .Y(n101) );
  AND2 u102 ( .A(n80), .B(n53), .Y(n102) );
  NOR2 u103 ( .A(n53), .B(n87), .Y(n103) );
  AND2 u104 ( .A(n65), .B(n75), .Y(n104) );
  AND2 u105 ( .A(n87), .B(n66), .Y(n105) );
  NAND2 u106 ( .A(n54), .B(n91), .Y(n106) );
  AND2 u107 ( .A(n95), .B(n90), .Y(n107) );
  XOR2 u108 ( .A(n74), .B(n70), .Y(n108) );
  AND2 u109 ( .A(n100), .B(n75), .Y(n109) );
  OR2 u110 ( .A(n81), .B(n64), .Y(n110) );
  NAND2 u111 ( .A(n76), .B(n86), .Y(n111) );
  OR2 u112 ( .A(n106), .B(n62), .Y(n112) );
  AND2 u113 ( .A(n98), .B(n95), .Y(n113) );
  NOT u114 ( .A(n64), .Y(n114) );
  XOR2 u115 ( .A(n93), .B(n77), .Y(n115) );
  AND2 u116 ( .A(n84), .B(n66), .Y(n116) );
  BUF u117 ( .A(n86), .Y(n117) );
  OR2 u118 ( .A(n92), .B(n117), .Y(n118) );
  XOR2 u119 ( .A(n84), .B(n104), .Y(n119) );
  NOT u120 ( .A(n117), .Y(n120) );
  NAND2 u121 ( .A(n67), .B(n109), .Y(n121) );
  NOR2 u122 ( .A(n87), .B(n110), .Y(n122) );
  AND2 u123 ( .A(n77), .B(n85), .Y(n123) );
  AND2 u124 ( .A(n76), .B(n117), .Y(n124) );
  XOR2 u125 ( .A(n88), .B(n89), .Y(n125) );
  NAND2 u126 ( .A(n117), .B(n93), .Y(n126) );
  AND2 u127 ( .A(n113), .B(n89), .Y(n127) );
  XOR2 u128 ( .A(n90), .B(n77), .Y(n128) );
  AND2 u129 ( .A(n102), .B(n128), .Y(n129) );
  OR2 u130 ( .A(n76), .B(n97), .Y(n130) );
  NOR2 u131 ( .A(n117), .B(n93), .Y(n131) );
  AND2 u132 ( .A(n106), .B(n113), .Y(n132) );
  NAND2 u133 ( .A(n114), .B(n79), .Y(n133) );
  AND2 u134 ( .A(n124), .B(n89), .Y(n134) );
  XOR2 u135 ( .A(n134), .B(n104), .Y(n135) );
  NAND2 u136 ( .A(n91), .B(n109), .Y(n136) );
  AND2 u137 ( .A(n102), .B(n106), .Y(n137) );
  BUF u138 ( .A(n134), .Y(n138) );
  NOT u139 ( .A(n87), .Y(n139) );
  BUF u140 ( .A(n99), .Y(n140) );
  NAND2 u141 ( .A(n129), .B(n93), .Y(n141) );
  OR2 u142 ( .A(n123), .B(n114), .Y(n142) );
  NOT u143 ( .A(n136), .Y(n143) );
  AND2 u144 ( .A(n105), .B(n100), .Y(n144) );
  NOT u145 ( .A(n142), .Y(n145) );
  AND2 u146 ( .A(n118), .B(n93), .Y(n146) );
  AND2 u147 ( .A(n114), .B(n143), .Y(n147) );
  NOT u148 ( .A(n145), .Y(n148) );
  NAND2 u149 ( .A(n142), .B(n146), .Y(n149) );
  OR2 u150 ( .A(n106), .B(n146), .Y(n150) );
  NAND2 u151 ( .A(n121), .B(n99), .Y(n151) );
  BUF u152 ( .A(n112), .Y(n152) );
  AND2 u153 ( .A(n128), .B(n121), .Y(n153) );
  OR2 u154 ( .A(n150), .B(n114), .Y(n154) );
  OR2 u155 ( .A(n143), .B(n113), .Y(n155) );
  OR2 u156 ( .A(n119), .B(n106), .Y(n156) );
  NOT u157 ( .A(n120), .Y(n157) );
  OR2 u158 ( .A(n136), .B(n129), .Y(n158) );
  NAND2 u159 ( .A(n158), .B(n139), .Y(n159) );
  OR2 u160 ( .A(n107), .B(n113), .Y(n160) );
  BUF u161 ( .A(n123), .Y(n161) );
  AND2 u162 ( .A(n124), .B(n110), .Y(n162) );
  AND2 u163 ( .A(n136), .B(n131), .Y(n163) );
  NOR2 u164 ( .A(n130), .B(n137), .Y(n164) );
  NAND2 u165 ( .A(n143), .B(n118), .Y(n165) );
  OR2 u166 ( .A(n148), .B(n124), .Y(n166) );
  OR2 u167 ( .A(n158), .B(n140), .Y(n167) );
  AND2 u168 ( .A(n165), .B(n148), .Y(n168) );
  NAND2 u169 ( .A(n162), .B(n162), .Y(n169) );
  DFF ff0 ( .C(clk), .D(n158), .Q(n170) );
  DFF ff1 ( .C(clk), .D(n128), .Q(n171) );
  DFF ff2 ( .C(clk), .D(n139), .Q(n172) );
  DFF ff3 ( .C(clk), .D(n143), .Q(n173) );
  DFF ff4 ( .C(clk), .D(n120), .Q(n174) );
  DFF ff5 ( .C(clk), .D(n158), .Q(n175) );
  DFF ff6 ( .C(clk), .D(n174), .Q(n176) );
  DFF ff7 ( .C(clk), .D(n137), .Q(n177) );
  DFF ff8 ( .C(clk), .D(n155), .Q(n178) );
  DFF ff9 ( .C(clk), .D(n136), .Q(n179) );
  DFF ff10 ( .C(clk), .D(n131), .Q(n180) );
  DFF ff11 ( .C(clk), .D(n154), .Q(n181) );
  DFF ff12 ( .C(clk), .D(n180), .Q(n182) );
  DFF ff13 ( .C(clk), .D(n155), .Q(n183) );
  DFF ff14 ( .C(clk), .D(n168), .Q(n184) );
  DFF ff15 ( .C(clk), .D(n157), .Q(n185) );
  DFF ff16 ( .C(clk), .D(n167), .Q(n186) );
  DFF ff17 ( .C(clk), .D(n153), .Q(n187) );
  DFF ff18 ( .C(clk), .D(n186), .Q(n188) );
  DFF ff19 ( .C(clk), .D(n132), .Q(n189) );
  DFF ff20 ( .C(clk), .D(n140), .Q(n190) );
  DFF ff21 ( .C(clk), .D(n169), .Q(n191) );
  DFF ff22 ( .C(clk), .D(n164), .Q(n192) );
  DFF ff23 ( .C(clk), .D(n138), .Q(n193) );
  DFF ff24 ( .C(clk), .D(n188), .Q(n194) );
  DFF ff25 ( .C(clk), .D(n154), .Q(n195) );
  DFF ff26 ( .C(clk), .D(n167), .Q(n196) );
  DFF ff27 ( .C(clk), .D(n186), .Q(n197) );
  DFF ff28 ( .C(clk), .D(n116), .Q(n198) );
  DFF ff29 ( .C(clk), .D(n154), .Q(n199) );
  BUF po_buf0 ( .A(n193), .Y(po0) );
  BUF po_buf1 ( .A(n194), .Y(po1) );
  BUF po_buf2 ( .A(n195), .Y(po2) );
  BUF po_buf3 ( .A(n196), .Y(po3) );
  BUF po_buf4 ( .A(n197), .Y(po4) );
  BUF po_buf5 ( .A(n198), .Y(po5) );
  BUF po_buf6 ( .A(n199), .Y(po6) );

endmodule