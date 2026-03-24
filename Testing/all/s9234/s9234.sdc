create_clock -name CK -period 10 -waveform {0 5} [get_ports {CK}]

set_propagated_clock [get_clocks {CK}]

set_input_delay  0 -clock clk [get_ports {g23 g44 g22 g41 g37 g40 g47 g36 g46 g38 g32 g702 g39 g42 g45 g567 g639 g705 g564 g563 g562 g561 g560 g559 g558 g557 g319 g314 g310 g306 g301 g107 g102 g98 g94 g89}]
set_output_delay 0 -clock clk [get_ports {g4098 g4107 g4104 g4110 g4101 g4105 g4112 g4100 g4109 g4102 g4099 g1293 g4103 g4106 g4108 g4121 g1290 g6728 g6374 g6372 g6370 g6368 g6366 g6364 g6362 g6360 g6284 g6282 g5692 g5469 g5468 g5137 g4809 g4422 g4321 g4307 g3600 g3222 g2584}]
