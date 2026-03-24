set PROJ_PATH /nfs/share/home/mingyang/run_pt/report
set SDC_FILE  ./report/mos6502/mos6502.sdc
set NETLIST_V ./report/mos6502/mos6502.v
set DESIGN    mos6502
set RESULT_DIR [file dirname $NETLIST_V]

set_app_var report_default_significant_digits 10

set_app_var search_path [list $PROJ_PATH/liberty]

set_app_var target_library [list \
  ./liberty/ics55_LLSC_H7CH_typ_tt_1p2_25.db \
  ./liberty/ics55_LLSC_H7CL_typ_tt_1p2_25.db \
  ./liberty/ics55_LLSC_H7CR_typ_tt_1p2_25.db \
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
