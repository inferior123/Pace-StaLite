/**
 * Minimal 1D/2D interpolation for liberty table lookup (replacing iEDA solver/Interpolation.hh).
 */
#ifndef PBA_STA_LIB_PARSER_INTERPOLATION_HPP
#define PBA_STA_LIB_PARSER_INTERPOLATION_HPP

#include <cmath>

namespace ista {

inline bool IsDoubleEqual(double a, double b, double eps = 1e-9) {
  return std::fabs(a - b) < eps;
}

inline double LinearInterpolate(double x1, double x2, double v1, double v2, double x) {
  if (x2 == x1) return v1;
  double t = (x - x1) / (x2 - x1);
  return v1 + t * (v2 - v1);
}

inline double BilinearInterpolation(double q11, double q12, double q21, double q22,
                                    double x1, double x2, double y1, double y2,
                                    double x, double y) {
  if (x2 == x1 || y2 == y1)
    return q11;
  double tx = (x - x1) / (x2 - x1);
  double ty = (y - y1) / (y2 - y1);
  double v1 = q11 + tx * (q21 - q11);
  double v2 = q12 + tx * (q22 - q12);
  return v1 + ty * (v2 - v1);
}

}  // namespace ista

#endif
