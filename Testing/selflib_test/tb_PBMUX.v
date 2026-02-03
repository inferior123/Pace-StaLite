// Simple testbench for P65_1233_PBMUX
module tb_PBMUX;
  reg OE, I, OD, DS1, DS0, PU, PD, IE, CS;
  wire PAD, A, C;

  // Instantiate the cell (primitive name as in .lib)
  P65_1233_PBMUX u_mux (
    .OE(OE), .I(I), .OD(OD), .DS1(DS1), .DS0(DS0), .PU(PU), .PD(PD), .IE(IE), .CS(CS), .PAD(PAD), .A(A), .C(C)
  );

endmodule
