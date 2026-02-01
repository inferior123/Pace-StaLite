# 三级 ADD 流水线 STA 测试
read_verilog /home/ysyx/project/pba-sta-base/proj/Testing/simple_nangate_test/add_pipeline_3stage.v
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib
create_clock -period 1 -name clk
set_clock_uncertainty -setup 2
