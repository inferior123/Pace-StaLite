module tb_single_pad;
  wire pad, a;
  P65_1233_PAR u_pad (
    .PAD(pad),
    .A(a)
  );
endmodule
