#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"

// 将 cap clamp 到 LUT 中 total_output_net_capacitance 轴的最大值。
// var1/var2 指明哪个轴是 cap 轴；LUT 自身的 index 优先于 template 的 index。
static double clamp_cap_to_lut_max(const celllib::LookupTable &lut,
                                   const celllib::CellLibrary *lib,
                                   const std::string &var1,
                                   const std::string &var2,
                                   double cap) {
  static const std::string CAP_VAR = "total_output_net_capacitance";
  // 确定 cap 对应哪个轴（index_1 or index_2）
  bool cap_is_index2 = (var2 == CAP_VAR);
  bool cap_is_index1 = (var1 == CAP_VAR);
  if (!cap_is_index1 && !cap_is_index2)
    return cap;  // 该表无 cap 轴，不 clamp

  // 取 LUT 自身 index 优先，否则用 template 的 index
  const std::vector<double> *indices = nullptr;
  if (cap_is_index2) {
    if (!lut.index_2.empty()) {
      indices = &lut.index_2;
    } else if (lib) {
      const auto *templ = lib->get_table_template(lut.template_name.value_or(""));
      if (templ && !templ->index_2.empty())
        indices = &templ->index_2;
    }
  } else {
    if (!lut.index_1.empty()) {
      indices = &lut.index_1;
    } else if (lib) {
      const auto *templ = lib->get_table_template(lut.template_name.value_or(""));
      if (templ && !templ->index_1.empty())
        indices = &templ->index_1;
    }
  }

  if (!indices || indices->empty())
    return cap;

  double max_cap = indices->back();
  return (cap > max_cap) ? max_cap : cap;
}

// 调试打印：给定具体的 LUT 表（而不是整条 arc），输出索引与所有表值，
// 供 calculate_transition_* / calculate_setup_* 等调用处查看。
static void debug_print_tb(const celllib::LookupTable &tb,
                           const std::string &template_name,
                           const std::string &var1, const std::string &var2) {
  std::cout << "\n[DEBUG][LUT] template=" << template_name << " var1=" << var1
            << " var2=" << var2 << "\n";
  std::cout << "  index_1[" << tb.index_1.size() << "] =";
  for (double v : tb.index_1)
    std::cout << " " << v;
  std::cout << "\n";
  std::cout << "  index_2[" << tb.index_2.size() << "] =";
  for (double v : tb.index_2)
    std::cout << " " << v;
  std::cout << "\n";
  std::cout << "  values (rows=index_1, cols=index_2):\n";
  for (size_t i = 0; i < tb.values.size(); ++i) {
    std::cout << "    ";
    for (size_t j = 0; j < tb.values[i].size(); ++j)
      std::cout << " " << tb.values[i][j];
    std::cout << "\n";
  }
}
