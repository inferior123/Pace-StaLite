# STA script with configurable design
# Usage:
#   tclsh sta.tcl                    # use default design "simple"
#   tclsh sta.tcl <design_name>      # e.g. tclsh sta.tcl simple

set default_design "simple"

# 优先从环境变量 DESIGN_NAME 取（避免 iSTA 把命令行第二参数当文件名读）
if { [info exists env(DESIGN_NAME)] && $env(DESIGN_NAME) != "" } {
  set design_name $env(DESIGN_NAME)
} else {
  set design_name [lindex $argv 0]
}
if { $design_name == "" } {
  set design_name $default_design
}

set workspace "./ref/iEDA/$design_name"

# Paths under project root (proj/); netlist/sdc/spef can be under Testing/ics55/<design>/
set netlist_dir  "./Testing/ics55/$design_name"
set netlist_file "$netlist_dir/$design_name.v"
set sdc_file    "$netlist_dir/$design_name.sdc"
set spef_file   "$netlist_dir/$design_name.spef"

# Override with env or leave default workspace
set_design_workspace $workspace

if { [file exists $netlist_file] } {
  read_netlist $netlist_file
} else {
  puts "ERROR: Netlist not found: $netlist_file"
  exit 1
}

read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_typ_tt_1p2_25_nldm.lib
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_typ_tt_1p2_25_nldm.lib
read_liberty /home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/liberty/ics55_LLSC_H7CH_typ_tt_1p2_25_nldm.lib

link_design $design_name

if { [file exists $sdc_file] } {
  read_sdc $sdc_file
} else {
  puts "WARNING: SDC not found: $sdc_file"
}

if { [file exists $spef_file] } {
  read_spef $spef_file
} else {
  puts "WARNING: SPEF not found: $spef_file"
}

report_timing -max_path 1 -digits 8
