/**
 * Convert ista::LibLibrary (from Lib parser) to celllib::CellLibrary.
 */
#ifndef PBA_STA_LIB_PARSER_LIB_TO_CELLLIB_CONVERTER_HPP
#define PBA_STA_LIB_PARSER_LIB_TO_CELLLIB_CONVERTER_HPP

#include "cell/cell_data_structure.hpp"
#include "lib_parser/Lib.hh"
#include <string>

namespace celllib {

/** Convert LibLibrary to CellLibrary and merge into out (or set library-level
 * attrs). */
void convert_lib_to_cell_library(ista::LibLibrary *lib_lib, CellLibrary &out);
} // namespace celllib

#endif
