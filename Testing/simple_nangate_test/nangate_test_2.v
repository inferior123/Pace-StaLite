module nangate_test_2 (input a, input b, input c, input d, input clk, output tar);

wire w1;
wire w2;
wire reg_q;

AND2_X2 and1 (
    .A1(a), .A2(b), .ZN(w1)
);

AND2_X2 and2 (
    .A1(w1), .A2(c), .ZN(w2)
);

// DFF_X1 就是寄存器（D触发器）
// D: 数据输入, CK: 时钟输入, Q: 数据输出, QN: 反相输出
DFF_X1 reg1 (
    .D(w2),      // 数据输入：来自w2
    .CK(clk),    // 时钟输入
    .Q(reg_q),   // 数据输出
    .QN()        // 反相输出（未使用）
);

AND2_X2 and3 (
    .A1(reg_q), .A2(d), .ZN(tar)
);

endmodule