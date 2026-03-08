module soc_sram ( clk, en, addr, din, dout_final );
  // Input signals buffer
  BUF b_clk ( .A(clk), .Y(clk_buf) );
  BUF b_en ( .A(en), .Y(en_buf) );
  BUF b_addr ( .A(addr), .Y(addr_buf) );
  BUF b_din ( .A(din), .Y(din_buf) );
  
  // The Macro Subsystem
  SRAM1024x32 mem (
    .clk(clk_buf),
    .en(en_buf),
    .addr(addr_buf),
    .din(din_buf),
    .dout(dout_raw)
  );
  
  // Output logic wrapper
  NOT n_out ( .A(dout_raw), .Y(dout_inv) );
  BUF b_out ( .A(dout_inv), .Y(dout_final) );
  
endmodule
