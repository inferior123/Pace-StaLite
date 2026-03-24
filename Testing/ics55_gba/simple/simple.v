module simple (
inp1,
inp2,
clk,
out
);

// Start PIs
input inp1;
input inp2;
input clk;

// Start POs
output out;

// Start wires
wire n1;
wire n2;
wire n3;
wire n4;
wire n5;
wire n6;
wire n7;
wire inp1;
wire inp2;
wire clk;
wire out;

// Start cells
NAND2BX0P5H7L u1 ( .AN(inp1), .B(inp2), .Y(n1) );
DFFQX1H7L f1 ( .D(n2), .CK(clk), .Q(n3) );
INVX0P5H7L u2 ( .A(n3), .Y(n4) );
INVX0P5H7L u3 ( .A(n4), .Y(n5) );
NOR2X0P5H7L u4 ( .A(n1), .B(n3), .Y(n2) );
DFFQX1H7L f2 ( .D(n5), .CK(clk), .Q(n6) );
INVX0P5H7L u5 ( .A(n6), .Y(n7) );
INVX0P5H7L u6 ( .A(n7), .Y(out) );

endmodule
