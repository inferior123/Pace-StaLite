# 规定文件名称
read_verilog /home/ysyx/project/pba-sta-base/proj/Testing/simple_nangate_test/nangate_test_2.v

read_liberty /home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib

# 时间单位：ps（1ns = 1000ps）
create_clock -period 1

# uncertainty 单位同上（30ps）
set_clock_uncertainty -setup 30
