/**
 * @file LibClassifyCell.hh
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief classify the liberty cell according the liberty
 * cell、port、arc、function.
 * @version 0.1
 * @date 2023-12-05
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once

#include <unordered_map>

#include "Lib.hh"

namespace ista {

/**
 * @brief class for classify the lib cell.
 *
 */
class LibClassifyCell
{
 public:
  void classifyLibCell(std::vector<LibLibrary*>& the_libs);
  Vector<LibCell*>* getClassOfCell(LibCell* cell)
  {
    auto it = _func_same_cells.find(cell);
    if (it != _func_same_cells.end()) {
      return &it->second;
    }
    return nullptr;
  }

 private:
  std::size_t hashCellPort(LibPort* port);
  std::size_t hashCellPortFuncExpr(RustLibertyExpr* expr);

  std::size_t calculateCellHash(LibCell* the_cell);

  bool comparePort(LibPort* port1, LibPort* port2);
  bool comparePortFunc(RustLibertyExpr* expr1, RustLibertyExpr* expr2);
  bool comparePorts(LibCell* cell1, LibCell* cell2);

  bool compareTimingArc(LibArcSet* set1, LibArcSet* set2);
  bool compareTimingArcSets(LibCell* cell1, LibCell* cell2);

  bool compareFunction(LibCell* the_cell1, LibCell* the_cell2);

  void classifyOneLibCell(LibLibrary* the_lib, std::unordered_map<std::size_t, Vector<LibCell*>>& hash_to_cells);

  ista::BTreeMap<LibCell*, ista::Vector<LibCell*>> _func_same_cells;  //!< The one cell map to the func same cell with
                                                                //!< different size.
};
}  // namespace ista