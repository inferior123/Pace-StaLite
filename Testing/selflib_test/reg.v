module and(input a, input b, output c);

wire a1;
wire b1;
wire c1;

reg reg1, reg2, reg3;

assign reg1 = a;
assign reg2 = b;
assign reg3 = c1;

assign a1 = reg1;
assign b1 = reg2;
assign c = reg3;

AND2_X1 g1 (.a(a1), .b(b1), .o(c1));

endmodule