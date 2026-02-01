module nangate_test_3 (input a, input clk, output out);

wire reg_q;
wire and_out;

// 寄存器：Q端输出连接到AND门
DFF_X1 reg1 (
    .D(and_out),  // 数据输入：来自AND门的输出（形成反馈）
    .CK(clk),     // 时钟输入
    .Q(reg_q),    // 数据输出：连接到AND门的另一个输入
    .QN()         // 反相输出（未使用）
);

// AND门：输入a和寄存器的Q端，输出连接到模块输出和寄存器的D端
AND2_X2 and1 (
    .A1(a),           // 第一个输入：模块输入
    .A2(reg_q),       // 第二个输入：寄存器的Q端
    .ZN(and_out)      // 输出：连接到模块输出和寄存器的D端
);

// 将AND门的输出连接到模块输出
assign out = and_out;

endmodule
