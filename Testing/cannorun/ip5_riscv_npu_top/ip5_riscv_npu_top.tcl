set PROJ_PATH /home/ysyx/project/pba-sta-base/proj/Testing/all
set SDC_FILE  ip5_riscv_npu_top/ip5_riscv_npu_top.sdc
set NETLIST_V ip5_riscv_npu_top/ip5_riscv_npu_top.v
set DESIGN    ip5_riscv_npu_top
set RESULT_DIR [file dirname $NETLIST_V]

set_app_var report_default_significant_digits 10

set_app_var search_path [list $PROJ_PATH/liberty]

set_app_var target_library [list \
  ics55_LLSC_H7CH_typ_tt_1p2_25.db \
  ics55_LLSC_H7CL_typ_tt_1p2_25.db \
  ics55_LLSC_H7CR_typ_tt_1p2_25.db \
]
set_app_var link_library [concat * $target_library]

read_verilog $NETLIST_V
link_design $DESIGN

remove_wire_load_model

read_sdc $SDC_FILE

update_timing

report_timing -delay_type max -pba_mode none -start_end_type in_to_reg  -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_max_in2reg.rpt
report_timing -delay_type max -pba_mode none -start_end_type reg_to_reg -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_max_reg2reg.rpt
report_timing -delay_type max -pba_mode none -start_end_type reg_to_out -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_max_reg2out.rpt
report_timing -delay_type max -pba_mode none -start_end_type in_to_out  -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_max_in2out.rpt

report_timing -delay_type min -pba_mode none -start_end_type in_to_reg  -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_min_in2reg.rpt
report_timing -delay_type min -pba_mode none -start_end_type reg_to_reg -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_min_reg2reg.rpt
report_timing -delay_type min -pba_mode none -start_end_type reg_to_out -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_min_reg2out.rpt
report_timing -delay_type min -pba_mode none -start_end_type in_to_out  -max_paths 1000 -slack_lesser_than 1000 > $RESULT_DIR/timing_min_in2out.rpt

exit
