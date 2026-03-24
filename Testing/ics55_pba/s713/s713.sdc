create_clock -name CK -period 10 -waveform {0 5} [get_ports {CK}]

set_propagated_clock [get_clocks {CK}]

set_input_delay  0 -clock CK [get_ports {G36 G35 G34 G33 G32 G31 G30 G29 G28 G27 G26 G25 G24 G23 G22 G21 G20 G19 G18 G17 G16 G15 G14 G13 G12 G11 G10 G9 G8 G6 G5 G4 G3 G2 G1}]
set_output_delay 0 -clock CK [get_ports {G101BF G100BF G99BF G98BF G97BF G96BF G95BF G94 G92 G91 G90 G89BF G88BF G87BF G86BF G85 G84 G83 G107 G106BF G105BF G104BF G103BF}]
