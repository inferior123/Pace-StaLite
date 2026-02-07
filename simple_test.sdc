read_verilog /home/ysyx/project/pba-sta-base/proj/Testing/ics55/simple/simple.v

read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_typ_tt_1p2_25_nldm.lib
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_typ_tt_1p2_25_nldm.lib
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/liberty/ics55_LLSC_H7CH_typ_tt_1p2_25_nldm.lib

# 时间单位：ps（1ns = 1000ps）
create_clock -period 1000

# uncertainty 单位同上（30ps）
# set_clock_uncertainty -setup 30
