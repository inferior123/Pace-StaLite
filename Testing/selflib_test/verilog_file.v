module simple (clk, input1, input2, input3, out);

// primary inputs
input clk;
input input1, input2, input3;

// primary output
output out; 

// wire out;

reg r1;

assign out = r1;

// // wires
// wire [1:0] w1;
// wire w2;
// wire w3;

// // module instances
// AND2_X1 g1 (.a(input1), .b(input2), .o(w1));
// OR2_X1 g2  (.a(input3), .b(w1[0]), .o(w2));
// INV_X2 g3  (.a(w2), .o(w3));
// NOR2_X1 g4 (.a(w1[1]), .b(w3), .o(out));

endmodule