module tb_pad_chain;
  wire pad1, pad2, a1, a2;
  P65_1233_PAR u_pad1 (
    .PAD(pad1),
    .A(a1)
  );
  P65_1233_PAR_5 u_pad2 (
    .PAD(pad2),
    .A(a2)
  );
endmodule
