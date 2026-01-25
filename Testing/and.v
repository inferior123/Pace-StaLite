module and(input a, input b, output c);

wire a1;
wire b1;
wire c1;

assign a1 = a;
assign b1 = b;
assign c = c1;

AND2_X1 g1 (.a(a1), .b(b1), .o(c1));

endmodule