module tb_power_ground;
  wire vdd1, vss1;
  P65_1233_VDD1 u_vdd1 (.VDD1(vdd1));
  P65_1233_VSS1 u_vss1 (.VSS1(vss1));
endmodule
