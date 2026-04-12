#ifndef STA_TIMING_API_HPP
#define STA_TIMING_API_HPP

#include "../cell/cell_data_structure.hpp"
#include "sta_timing_result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sta {

class STAWorker;

struct segment_res {
  double slew;
  double delay;
  TransitionDirection dir;
};

void segment_delay_slew(const TimingRunResult &res,
                        const celllib::CellLibrary *cell_library_,
                        AnalysisMode mode, std::size_t from_pt,
                        std::size_t to_pt, double prev_slew,
                        TransitionDirection cur_dir, double &out_delay,
                        double &out_slew, TransitionDirection &out_dir);

std::vector<segment_res>
segment_delays_slews(const TimingRunResult &res,
                     const celllib::CellLibrary *cell_library_,
                     AnalysisMode mode, std::size_t from_pt, std::size_t to_pt,
                     double prev_slew, TransitionDirection cur_dir);

std::vector<segment_res> segment_delays_slews_gba(
    const TimingRunResult &res, const celllib::CellLibrary *cell_library_,
    AnalysisMode mode, std::size_t from_pt, std::size_t to_pt, double prev_slew,
    TransitionDirection cur_dir);

double recalc_slew_with_max_cap(const TimingRunResult &res,
                                const celllib::CellLibrary *cell_library_,
                                AnalysisMode mode, std::size_t from_pt,
                                std::size_t to_pt, double prev_slew,
                                TransitionDirection out_dir);
void run_pba_analysis(STAWorker &worker);
void run_gba_analysis(STAWorker &worker);

} // namespace sta

double get_lut_avg(std::optional<celllib::LookupTable> lut);
double calculate_delay_rise(const celllib::TimingArc arc,
                            const celllib::CellLibrary *lib,
                            double input_slew_rise, double load_cap);
double calculate_delay_fall(const celllib::TimingArc arc,
                            const celllib::CellLibrary *lib,
                            double input_slew_fall, double load_cap);
double calculate_transition_fall(const celllib::TimingArc arc,
                                 const celllib::CellLibrary *lib,
                                 double input_slew_fall, double load_cap);
double calculate_transition_rise(const celllib::TimingArc arc,
                                 const celllib::CellLibrary *lib,
                                 double input_slew_rise, double load_cap);
sta::TransitionDirection
speculate_transition_direction(bool is_clock_to_q, celllib::TimingArc arc,
                               sta::TransitionDirection input_direction);

double calculate_setup_fall(const celllib::TimingArc arc,
                            const celllib::CellLibrary *lib, double data_trans,
                            double clk_trans);
double calculate_hold_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans);
double calculate_setup_rise(const celllib::TimingArc arc,
                            const celllib::CellLibrary *lib, double data_trans,
                            double clk_trans);
double calculate_hold_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans);
const celllib::TimingArc *find_default_arc(celllib::Pin &pin,
                                           const std::string &related_pin);

std::string group_type_str(sta::PathGroup group);

#endif // STA_TIMING_API_HPP
