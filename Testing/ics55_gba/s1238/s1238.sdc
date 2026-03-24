create_clock -name CK -period 10 -waveform {0 5} [get_ports {CK}]

set_propagated_clock [get_clocks {CK}]

set_input_delay  0 -clock CK [get_ports {G13 G12 G11 G10 G9 G8 G7 G6 G5 G4 G3 G2 G1 G0}]
set_output_delay 0 -clock CK [get_ports {G539 G45 G537 G535 G532 G530 G548 G547 G546 G542 G552 G551 G550 G549}]
