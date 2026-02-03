module tb_pad_output;
  wire pad, a, c, ie, cs;
  P65_1233_PBMUX u_mux (
    .PAD(pad),
    .A(a),
    .C(c),
    .IE(ie),
    .CS(cs)
  );
endmodule
