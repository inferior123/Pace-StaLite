// Simple testbench for P65_1233_PWE
module tb_PWE;
  reg E, XIN;
  wire XOUT, XC;

  P65_1233_PWE u_pwe (
    .E(E), .XIN(XIN), .XOUT(XOUT), .XC(XC)
  );
  endmodule
