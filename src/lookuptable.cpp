#include "cell/cell_data_structure.hpp"
#include "iostream"

// 将 cap clamp 到 LUT 中 total_output_net_capacitance 轴的最大值。
// var1/var2 指明哪个轴是 cap 轴；LUT 自身的 index 优先于 template 的 index。
