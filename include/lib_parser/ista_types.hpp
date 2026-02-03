/**
 * Minimal STA/Liberty types for lib parser (replacing iEDA include/Type.hh, Config.hh).
 * Used by Lib.hh and LibParserRustC.
 */
#ifndef PBA_STA_LIB_PARSER_ISTA_TYPES_HPP
#define PBA_STA_LIB_PARSER_ISTA_TYPES_HPP

namespace ista {

/** Rise/fall transition type */
enum class TransType {
  kRise = 0,
  kFall = 1
};

/** Max/min analysis mode */
enum class AnalysisMode {
  kMax = 0,
  kMin = 1,
  kMaxMin = 2
};

/** Time unit for liberty */
enum class TimeUnit {
  kNS = 0,
  kPS = 1,
  kFS = 2
};

/** Capacitive unit */
enum class CapacitiveUnit {
  kFF = 0,
  kPF = 1
};

/** Resistance unit */
enum class ResistanceUnit {
  kOHM = 0,
  kkOHM = 1
};

/** Port cap / slew split: max_rise, max_fall, min_rise, min_fall */
constexpr unsigned MODE_TRANS_SPLIT = 4u;
/** Max/min split */
constexpr unsigned MODE_SPLIT = 2u;

inline bool IS_MAX(AnalysisMode mode) {
  return mode == AnalysisMode::kMax || mode == AnalysisMode::kMaxMin;
}
inline bool IS_MIN(AnalysisMode mode) {
  return mode == AnalysisMode::kMin || mode == AnalysisMode::kMaxMin;
}
inline bool IS_RISE(TransType t) { return t == TransType::kRise; }
inline bool IS_FALL(TransType t) { return t == TransType::kFall; }

}  // namespace ista

#endif
