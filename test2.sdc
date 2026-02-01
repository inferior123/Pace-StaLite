# 测试 nangate_test_2 - 有寄存器和组合逻辑，多个 endpoint
read_verilog /home/ysyx/project/pba-sta-base/proj/Testing/simple_nangate_test/nangate_test_2.v
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib
create_clock -period 100 -name clk
set_clock_uncertainty -setup 2
