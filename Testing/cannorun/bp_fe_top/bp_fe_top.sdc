create_clock -name clk_i -period 10 -waveform {0 5} [get_ports {clk_i}]

set_propagated_clock [get_clocks {clk_i}]

set_input_delay  0 -clock clk [get_ports {reset_i icache_id_i bp_fe_cmd_v_i bp_fe_queue_ready_i lce_cce_req_ready_i lce_cce_resp_ready_i lce_cce_data_resp_ready_i cce_lce_cmd_v_i cce_lce_data_cmd_v_i lce_lce_tr_resp_v_i lce_lce_tr_resp_ready_i}]
set_output_delay 0 -clock clk [get_ports {bp_fe_cmd_ready_o bp_fe_queue_v_o lce_cce_req_v_o lce_cce_resp_v_o lce_cce_data_resp_v_o cce_lce_cmd_ready_o cce_lce_data_cmd_ready_o lce_lce_tr_resp_ready_o lce_lce_tr_resp_v_o}]
