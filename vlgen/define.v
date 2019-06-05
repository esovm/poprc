`ifndef __DEFINE__
`define __DEFINE__

`ifndef intN
 `define intN 32
`endif

`ifndef symN
 `define symN 1
`endif

`define intT [`intN:0]
`define intD [`intN-1:0]
`define intR [`intN]
`define symT [`symN:0]
`define symD [`symN-1:0]
`define symR [`symN]

`define true 1'b1
`define false 1'b0

`define on_write(n, x) {x[n] & write, x[n-1:0]}

`define read(n, x) (n'b1 << n | x)

`define set(x) x <= `true
`define reset(x) x <= `false

`define valid(x) (! (& int4))

`endif
