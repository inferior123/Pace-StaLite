# 规定文件名称
read_verilog /home/ysyx/project/pba-sta-base/proj/Testing/simple_nangate_test/nangate_test_3.v

read_liberty /home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib

create_clock -period 1 

set_clock_uncertainty -setup 30