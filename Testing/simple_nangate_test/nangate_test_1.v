module nangate_test_1 (input a, input b, input c, input d, output tar);

wire w1;
wire w2;

AND2_X2 and1 (.A1(a), .A2(b), .ZN(w1));

AND2_X2 and2 (.A1(w1), .A2(c), .ZN(w2));

AND2_X2 and3 (.A1(w2), .A2(d), .ZN(tar));

endmodule