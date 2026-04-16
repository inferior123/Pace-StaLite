#include <cstdlib>
#include <iostream>
#include <string>

#include "sta/debug.h"

int main(int argc, char *argv[]) {

  if (argc > 6) {
    test_lut(argv[2], argv[3], argv[4], std::stod(argv[5]), std::stod(argv[6]));
  } else if (argc > 5) {
    // test_setup_hold: cell pin related_pin slew_ns（4参数，cell被忽略时可不传）
    test_setup_hold(argv[2], argv[3], argv[4], std::stod(argv[5]));
  } else if(argc > 3) {
    debug_lib_cell();
  } else if (argc > 2) {
    std::cerr << "unknown argv count" << std::endl;
  } else if (argc > 1)
    signal_test(argv[1]);
  else
    auto_test(argc, argv);

  return 0;
}
