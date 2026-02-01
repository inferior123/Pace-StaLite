// 三级 ADD 流水线
// 使用 simple_nangate 库中的 AND2_X2 和 DFF_X1
// 注：库中无 ADD 单元，此处用 AND 作为级间组合逻辑占位；若库中添加 ADD 可替换
//
// 流水线结构：
//   Stage 1: in1, in2 -> 组合逻辑 -> reg1
//   Stage 2: reg1, in3 -> 组合逻辑 -> reg2
//   Stage 3: reg2, in4 -> 组合逻辑 -> reg3 -> out
// 每级接受新输入，经组合逻辑后打入寄存器，最后输出

module add_pipeline_3stage (
    input  wire       clk,
    input  wire       in1,
    input  wire       in2,
    input  wire       in3,
    input  wire       in4,
    output out
);

wire stage1_out;  // Stage 1 组合逻辑输出
wire stage2_out;  // Stage 2 组合逻辑输出

wire reg1_q;
wire reg2_q;

// Stage 1: in1 & in2 -> reg1
AND2_X2 stage1_logic (.A1(in1), .A2(in2), .ZN(stage1_out));
DFF_X1  reg1         (.D(stage1_out), .CK(clk), .Q(reg1_q), .QN());

// Stage 2: reg1_q & in3 -> reg2
AND2_X2 stage2_logic (.A1(reg1_q), .A2(in3), .ZN(stage2_out));
DFF_X1  reg2         (.D(stage2_out), .CK(clk), .Q(reg2_q), .QN());

// Stage 3: reg2_q & in4 -> reg3 -> out
AND2_X2 stage3_logic (.A1(reg2_q), .A2(in4), .ZN(out));


endmodule
