#include "cell/cell_data_structure.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace celllib {

// LookupTable implementation
std::optional<double> LookupTable::lookup_nearest(double idx1,
                                                  double idx2) const {
  if (index_1.empty() || index_2.empty() || values.empty()) {
    return std::nullopt;
  }

  // 找到最近的索引
  auto find_nearest_index = [](const std::vector<double> &indices,
                               double value) -> size_t {
    if (indices.empty())
      return 0;

    size_t nearest = 0;
    double min_diff = std::abs(indices[0] - value);

    for (size_t i = 1; i < indices.size(); ++i) {
      double diff = std::abs(indices[i] - value);
      if (diff < min_diff) {
        min_diff = diff;
        nearest = i;
      }
    }
    return nearest;
  };

  size_t i1 = find_nearest_index(index_1, idx1);
  size_t i2 = find_nearest_index(index_2, idx2);

  if (i1 < values.size() && i2 < values[i1].size()) {
    return values[i1][i2];
  }

  return std::nullopt;
}

std::optional<double> LookupTable::lookup(double idx1, double idx2) const {
  // 简化实现：使用最近邻查找
  // 未来可以实现线性插值
  return lookup_nearest(idx1, idx2);
}

// StandardCell implementation
std::vector<std::string> StandardCell::get_input_pins() const {
  std::vector<std::string> result;
  for (const auto &[name, pin] : pins) {
    if (pin.direction == PinDirection::INPUT ||
        pin.direction == PinDirection::INOUT) {
      result.push_back(name);
    }
  }
  return result;
}

std::vector<std::string> StandardCell::get_output_pins() const {
  std::vector<std::string> result;
  for (const auto &[name, pin] : pins) {
    if (pin.direction == PinDirection::OUTPUT ||
        pin.direction == PinDirection::INOUT) {
      result.push_back(name);
    }
  }
  return result;
}

const Pin *StandardCell::get_pin(const std::string &pin_name) const {
  auto it = pins.find(pin_name);
  if (it != pins.end()) {
    return &it->second;
  }
  return nullptr;
}

Pin *StandardCell::get_pin(const std::string &pin_name) {
  auto it = pins.find(pin_name);
  if (it != pins.end()) {
    return &it->second;
  }
  return nullptr;
}

void StandardCell::add_pin(const Pin &pin) { pins[pin.name] = pin; }

bool StandardCell::is_sequential() const {
  // 检查是否有clock pin
  for (const auto &[name, pin] : pins) {
    if (pin.is_clock) {
      return true;
    }
  }
  // 未来可以检查ff定义
  return false;
}

// CellLibrary implementation
void CellLibrary::add_cell(const StandardCell &cell) {
  cells[cell.name] = cell;
}

const StandardCell *CellLibrary::get_cell(const std::string &cell_name) const {
  auto it = cells.find(cell_name);
  if (it != cells.end()) {
    return &it->second;
  }
  return nullptr;
}

StandardCell *CellLibrary::get_cell(const std::string &cell_name) {
  auto it = cells.find(cell_name);
  if (it != cells.end()) {
    return &it->second;
  }
  return nullptr;
}

bool CellLibrary::has_cell(const std::string &cell_name) const {
  return cells.find(cell_name) != cells.end();
}

std::vector<std::string> CellLibrary::get_cell_names() const {
  std::vector<std::string> result;
  result.reserve(cells.size());
  for (const auto &[name, cell] : cells) {
    result.push_back(name);
  }
  return result;
}

void CellLibrary::clear() {
  cells.clear();
  library_name.clear();
  time_unit.clear();
  capacitance_unit = 1.0;
  table_templates.clear();
}

void CellLibrary::add_table_template(const TableTemplate &templ) {
  table_templates[templ.name] = templ;
}

const TableTemplate *
CellLibrary::get_table_template(const std::string &name) const {
  auto it = table_templates.find(name);
  if (it != table_templates.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<std::string> CellLibrary::get_table_template_names() const {
  std::vector<std::string> names;
  names.reserve(table_templates.size());
  for (const auto &pair : table_templates) {
    names.push_back(pair.first);
  }
  return names;
}

inline double insert_caculate(double x0, double x1, double x2, double y0,
                              double y1, double y2, double T11, double T12,
                              double T21, double T22) {
  double x01 = (x0 - x1) / (x2 - x1);
  double x20 = (x2 - x0) / (x2 - x1);
  double y01 = (y0 - y1) / (y2 - y1);

  double y20 = (y2 - y0) / (y2 - y1);

  return x20 * y20 * T11 + x20 * y01 * T12 + x01 * y20 * T21 + x01 * y01 * T22;
}

double CellLibrary::caculate_lookuptable(const LookupTable &tb, double x0,
                                         double y0,
                                         const std::string &va_name_1,
                                         const std::string &va_name_2) const {
  if (!tb.template_name.has_value()) {
    std::cerr << "no spceficed template for table " << std::endl;
    assert(false);
  }

  if (tb.values.empty()) {
    std::cerr << "empty lookup table values" << std::endl;
    assert(false);
  }

  const TableTemplate *templ = get_table_template(tb.template_name.value());
  if (!templ) {
    std::cerr << "template not found: " << tb.template_name.value()
              << std::endl;
    assert(false);
  }

  // LUT 自身的 index 优先于 template 的 index（Liberty 中表可覆盖模板的索引）
  const std::vector<double> *x_indices =
      tb.index_1.empty() ? &templ->index_1 : &tb.index_1;
  const std::vector<double> *y_indices =
      tb.index_2.empty() ? &templ->index_2 : &tb.index_2;
  if (x_indices->empty() || y_indices->empty()) {
    std::cerr << "empty lookup table indices (both LUT and template)"
              << std::endl;
    assert(false);
  }

  // Determine which variable maps to which index (axis mapping only; indices
  // already chosen above)
  double mapped_x0, mapped_y0;

  // Debug print: show template name/variables and provided variable names
  // std::cout << "    [LUT] Template name: " << templ->name << std::endl;
  // std::cout << "      Template variables: "
  //           << (templ->variable_1.has_value() ? templ->variable_1.value()
  //                                             : std::string("<null>"))
  //           << ", "
  //           << (templ->variable_2.has_value() ? templ->variable_2.value()
  //                                             : std::string("<null>"))
  //           << std::endl;
  // std::cout << "      Provided variable names: " << va_name_1 << ", "
  //           << va_name_2 << std::endl;

  if (templ->variable_1.has_value() && templ->variable_2.has_value()) {
    if (templ->variable_1.value() == va_name_1 &&
        templ->variable_2.value() == va_name_2) {
      mapped_x0 = x0;
      mapped_y0 = y0;
    } else if (templ->variable_2.value() == va_name_1 &&
               templ->variable_1.value() == va_name_2) {
      mapped_x0 = y0;
      mapped_y0 = x0;
    } else {
      std::cerr << "mismatch index name for caculate lookuptable" << std::endl;
      std::cerr << "  templ: " << templ->variable_1.value() << " "
                << templ->variable_2.value() << std::endl;
      std::cerr << "  provided: " << va_name_1 << " " << va_name_2 << std::endl;
      assert(false);
    }
  } else {
    std::cerr << "template variables are null" << std::endl;
    assert(false);
  }

  // Find the two indices that bracket mapped_x0
  size_t x_idx1 = 0, x_idx2 = 0;
  if (x_indices->size() < 2) {
    std::cerr << "insufficient x indices in lookup table" << std::endl;
    assert(false);
  }

  // Check for extrapolation cases first
  if (mapped_x0 < (*x_indices)[0]) {
    // Extrapolation: mapped_x0 < first index
    x_idx1 = 0;
    x_idx2 = 1;
  } else if (mapped_x0 > (*x_indices)[x_indices->size() - 1]) {
    // Extrapolation: mapped_x0 > last index
    x_idx1 = x_indices->size() - 2;
    x_idx2 = x_indices->size() - 1;
  } else {
    // Interpolation: find the two indices that bracket mapped_x0
    for (size_t i = 0; i < x_indices->size() - 1; ++i) {
      if ((*x_indices)[i] <= mapped_x0 && mapped_x0 <= (*x_indices)[i + 1]) {
        x_idx1 = i;
        x_idx2 = i + 1;
        break;
      }
    }
  }

  // Find the two indices that bracket mapped_y0
  size_t y_idx1 = 0, y_idx2 = 0;
  if (y_indices->size() < 2) {
    std::cerr << "insufficient y indices in lookup table" << std::endl;
    assert(false);
  }

  // Check for extrapolation cases first
  if (mapped_y0 < (*y_indices)[0]) {
    y_idx1 = 0;
    y_idx2 = 1;
  } else if (mapped_y0 > (*y_indices)[y_indices->size() - 1]) {
    y_idx1 = y_indices->size() - 2;
    y_idx2 = y_indices->size() - 1;
  } else {
    for (size_t i = 0; i < y_indices->size() - 1; ++i) {
      if ((*y_indices)[i] <= mapped_y0 && mapped_y0 <= (*y_indices)[i + 1]) {
        y_idx1 = i;
        y_idx2 = i + 1;
        break;
      }
    }
  }

  // Extract the four corner values
  // T11 = values[x_idx1][y_idx1], T12 = values[x_idx1][y_idx2]
  // T21 = values[x_idx2][y_idx1], T22 = values[x_idx2][y_idx2]
  if (x_idx1 >= tb.values.size() || x_idx2 >= tb.values.size() ||
      y_idx1 >= tb.values[x_idx1].size() ||
      y_idx2 >= tb.values[x_idx1].size() ||
      y_idx1 >= tb.values[x_idx2].size() ||
      y_idx2 >= tb.values[x_idx2].size()) {
    std::cerr << "index out of range in lookup table values" << std::endl;
    assert(false);
  }

  double T11 = tb.values[x_idx1][y_idx1];
  double T12 = tb.values[x_idx1][y_idx2];
  double T21 = tb.values[x_idx2][y_idx1];
  double T22 = tb.values[x_idx2][y_idx2];

  double x1 = (*x_indices)[x_idx1];
  double x2 = (*x_indices)[x_idx2];
  double y1 = (*y_indices)[y_idx1];
  double y2 = (*y_indices)[y_idx2];

  // clang-format off
  //
  // 调试输出：显示查找表插值计算的详细信息
  // std::cout << "    [LUT] Lookup table interpolation:" << std::endl;
  // std::cout << "      Input: x0=" << x0 << " (" << va_name_1 << "), y0=" << y0
  //           << " (" << va_name_2 << ")" << std::endl;
  // std::cout << "      Mapped:mapped_x0=" << mapped_x0
  //           << ", mapped_y0=" << mapped_y0 << std::endl;
  // std::cout << "      X indices: [" << x1 << ", " << x2
  //           << "] (indices: " << x_idx1 << ", " << x_idx2 << ")" << std::endl;
  // std::cout << "      Y indices: [" << y1 << ", " << y2
  //           << "] (indices: " << y_idx1 << ", " << y_idx2 << ")" << std::endl;
  // std::cout << "      Corner values: T11=" << T11 << ", T12=" << T12
  //           << ", T21=" << T21 << ", T22=" << T22 << std::endl;
  //
  // clang-format on 

  double result =
      insert_caculate(mapped_x0, x1, x2, mapped_y0, y1, y2, T11, T12, T21, T22);

  // 显示插值计算的中间步骤
  // double x01 = (mapped_x0 - x1) / (x2 - x1);
  // double x20 = (x2 - mapped_x0) / (x2 - x1);
  // double y01 = (mapped_y0 - y1) / (y2 - y1);
  // double y20 = (y2 - mapped_y0) / (y2 - y1);
  // std::cout << "      Interpolation weights: x01=" << x01 << ", x20=" << x20
  //           << ", y01=" << y01 << ", y20=" << y20 << std::endl;
  // std::cout << "Calculation: " << x20 << "*" << y20 << "*" << T11 << " + "
  //           << x20 << "*" << y01 << "*" << T12 << " + " << x01 << "*" << y20
  //           << "*" << T21 << " + " << x01 << "*" << y01 << "*" << T22
  //           << std::endl;
  // std::cout << "      Result: " << result << std::endl;

  return result;
}

} // namespace celllib
