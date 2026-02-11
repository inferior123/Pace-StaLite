#include "cell/celllib_cache.hpp"
#include "cell/cell_data_structure.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace celllib {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char *CACHE_DIR = ".sta_cache";
const char *CACHE_PREFIX = "celllib_";

// 根据 lib 路径列表 + 各文件 mtime 计算缓存键，返回文件名 "celllib_<id>.json"
std::string compute_cache_filename(const std::vector<std::string> &lib_file_paths) {
  std::ostringstream key;
  for (const std::string &p : lib_file_paths) {
    key << p << ":";
    try {
      if (fs::exists(p) && fs::is_regular_file(p)) {
        auto t = fs::last_write_time(p);
        key << t.time_since_epoch().count();
      } else {
        key << "0";
      }
    } catch (...) {
      key << "0";
    }
    key << "|";
  }
  std::hash<std::string> hasher;
  size_t h = hasher(key.str());
  return std::string(CACHE_PREFIX) + std::to_string(static_cast<unsigned long long>(h)) + ".json";
}

std::string get_cache_file_path(const std::vector<std::string> &lib_file_paths) {
  return std::string(CACHE_DIR) + "/" + compute_cache_filename(lib_file_paths);
}

// -------- JSON 序列化 helpers --------
static void to_json(json &j, const LookupTable &t) {
  j["index_1"] = t.index_1;
  j["index_2"] = t.index_2;
  j["values"] = t.values;
  if (t.template_name.has_value())
    j["template_name"] = *t.template_name;
}

static void from_json(const json &j, LookupTable &t) {
  j.at("index_1").get_to(t.index_1);
  j.at("index_2").get_to(t.index_2);
  j.at("values").get_to(t.values);
  if (j.contains("template_name") && !j["template_name"].is_null())
    t.template_name = j["template_name"].get<std::string>();
}

static void to_json(json &j, const TimingArc &a) {
  j["related_pin"] = a.related_pin;
  j["timing_type"] = static_cast<int>(a.timing_type);
  j["timing_sense"] = static_cast<int>(a.timing_sense);
  if (a.sdf_cond.has_value())
    j["sdf_cond"] = *a.sdf_cond;
  if (a.intrinsic_rise.has_value()) j["intrinsic_rise"] = *a.intrinsic_rise;
  if (a.intrinsic_fall.has_value()) j["intrinsic_fall"] = *a.intrinsic_fall;
  if (a.cell_rise.has_value()) { json j_lt; to_json(j_lt, *a.cell_rise); j["cell_rise"] = j_lt; }
  if (a.cell_fall.has_value()) { json j_lt; to_json(j_lt, *a.cell_fall); j["cell_fall"] = j_lt; }
  if (a.rise_transition.has_value()) { json j_lt; to_json(j_lt, *a.rise_transition); j["rise_transition"] = j_lt; }
  if (a.fall_transition.has_value()) { json j_lt; to_json(j_lt, *a.fall_transition); j["fall_transition"] = j_lt; }
  if (a.fall_constraint.has_value()) { json j_lt; to_json(j_lt, *a.fall_constraint); j["fall_constraint"] = j_lt; }
  if (a.rise_constraint.has_value()) { json j_lt; to_json(j_lt, *a.rise_constraint); j["rise_constraint"] = j_lt; }
}

static void from_json(const json &j, TimingArc &a) {
  j.at("related_pin").get_to(a.related_pin);
  a.timing_type = static_cast<TimingType>(j.at("timing_type").get<int>());
  a.timing_sense = static_cast<TimingSense>(j.at("timing_sense").get<int>());
  if (j.contains("sdf_cond") && !j["sdf_cond"].is_null())
    a.sdf_cond = j["sdf_cond"].get<std::string>();
  if (j.contains("intrinsic_rise") && !j["intrinsic_rise"].is_null())
    a.intrinsic_rise = j["intrinsic_rise"].get<double>();
  if (j.contains("intrinsic_fall") && !j["intrinsic_fall"].is_null())
    a.intrinsic_fall = j["intrinsic_fall"].get<double>();
  if (j.contains("cell_rise") && !j["cell_rise"].is_null()) {
    LookupTable lt; from_json(j["cell_rise"], lt); a.cell_rise = lt;
  }
  if (j.contains("cell_fall") && !j["cell_fall"].is_null()) {
    LookupTable lt; from_json(j["cell_fall"], lt); a.cell_fall = lt;
  }
  if (j.contains("rise_transition") && !j["rise_transition"].is_null()) {
    LookupTable lt; from_json(j["rise_transition"], lt); a.rise_transition = lt;
  }
  if (j.contains("fall_transition") && !j["fall_transition"].is_null()) {
    LookupTable lt; from_json(j["fall_transition"], lt); a.fall_transition = lt;
  }
  if (j.contains("fall_constraint") && !j["fall_constraint"].is_null()) {
    LookupTable lt; from_json(j["fall_constraint"], lt); a.fall_constraint = lt;
  }
  if (j.contains("rise_constraint") && !j["rise_constraint"].is_null()) {
    LookupTable lt; from_json(j["rise_constraint"], lt); a.rise_constraint = lt;
  }
}

static void to_json(json &j, const InternalPower &p) {
  if (p.when.has_value()) j["when"] = *p.when;
  if (p.related_pin.has_value()) j["related_pin"] = *p.related_pin;
  if (p.rise_power.has_value()) { json j_lt; to_json(j_lt, *p.rise_power); j["rise_power"] = j_lt; }
  if (p.fall_power.has_value()) { json j_lt; to_json(j_lt, *p.fall_power); j["fall_power"] = j_lt; }
}

static void from_json(const json &j, InternalPower &p) {
  if (j.contains("when") && !j["when"].is_null()) p.when = j["when"].get<std::string>();
  if (j.contains("related_pin") && !j["related_pin"].is_null())
    p.related_pin = j["related_pin"].get<std::string>();
  if (j.contains("rise_power") && !j["rise_power"].is_null()) {
    LookupTable lt; from_json(j["rise_power"], lt); p.rise_power = lt;
  }
  if (j.contains("fall_power") && !j["fall_power"].is_null()) {
    LookupTable lt; from_json(j["fall_power"], lt); p.fall_power = lt;
  }
}

static void to_json(json &j, const Pin &p) {
  j["name"] = p.name;
  j["direction"] = static_cast<int>(p.direction);
  j["is_clock"] = p.is_clock;
  if (p.capacitance.has_value()) j["capacitance"] = *p.capacitance;
  if (p.max_capacitance.has_value()) j["max_capacitance"] = *p.max_capacitance;
  if (p.rise_capacitance_min.has_value()) j["rise_capacitance_min"] = *p.rise_capacitance_min;
  if (p.rise_capacitance_max.has_value()) j["rise_capacitance_max"] = *p.rise_capacitance_max;
  if (p.fall_capacitance_min.has_value()) j["fall_capacitance_min"] = *p.fall_capacitance_min;
  if (p.fall_capacitance_max.has_value()) j["fall_capacitance_max"] = *p.fall_capacitance_max;
  if (p.function.has_value()) j["function"] = *p.function;
  if (p.related_power_pin.has_value()) j["related_power_pin"] = *p.related_power_pin;
  if (p.related_ground_pin.has_value()) j["related_ground_pin"] = *p.related_ground_pin;
  j["timing_arcs"] = json::array();
  for (const auto &e : p.timing_arcs) { json je; to_json(je, e); j["timing_arcs"].push_back(je); }
  j["internal_power"] = json::array();
  for (const auto &e : p.internal_power) { json je; to_json(je, e); j["internal_power"].push_back(je); }
  if (p.setup_rise.has_value()) j["setup_rise"] = *p.setup_rise;
  if (p.setup_fall.has_value()) j["setup_fall"] = *p.setup_fall;
  if (p.hold_rise.has_value()) j["hold_rise"] = *p.hold_rise;
  if (p.hold_fall.has_value()) j["hold_fall"] = *p.hold_fall;
}

static void from_json(const json &j, Pin &p) {
  j.at("name").get_to(p.name);
  p.direction = static_cast<PinDirection>(j.at("direction").get<int>());
  p.is_clock = j.value("is_clock", false);
  if (j.contains("capacitance") && !j["capacitance"].is_null())
    p.capacitance = j["capacitance"].get<double>();
  if (j.contains("max_capacitance") && !j["max_capacitance"].is_null())
    p.max_capacitance = j["max_capacitance"].get<double>();
  if (j.contains("rise_capacitance_min") && !j["rise_capacitance_min"].is_null())
    p.rise_capacitance_min = j["rise_capacitance_min"].get<double>();
  if (j.contains("rise_capacitance_max") && !j["rise_capacitance_max"].is_null())
    p.rise_capacitance_max = j["rise_capacitance_max"].get<double>();
  if (j.contains("fall_capacitance_min") && !j["fall_capacitance_min"].is_null())
    p.fall_capacitance_min = j["fall_capacitance_min"].get<double>();
  if (j.contains("fall_capacitance_max") && !j["fall_capacitance_max"].is_null())
    p.fall_capacitance_max = j["fall_capacitance_max"].get<double>();
  if (j.contains("function") && !j["function"].is_null())
    p.function = j["function"].get<std::string>();
  if (j.contains("related_power_pin") && !j["related_power_pin"].is_null())
    p.related_power_pin = j["related_power_pin"].get<std::string>();
  if (j.contains("related_ground_pin") && !j["related_ground_pin"].is_null())
    p.related_ground_pin = j["related_ground_pin"].get<std::string>();
  if (j.contains("timing_arcs")) {
    p.timing_arcs.clear();
    for (const auto &je : j["timing_arcs"]) { TimingArc e; from_json(je, e); p.timing_arcs.push_back(e); }
  }
  if (j.contains("internal_power")) {
    p.internal_power.clear();
    for (const auto &je : j["internal_power"]) { InternalPower e; from_json(je, e); p.internal_power.push_back(e); }
  }
  if (j.contains("setup_rise") && !j["setup_rise"].is_null())
    p.setup_rise = j["setup_rise"].get<double>();
  if (j.contains("setup_fall") && !j["setup_fall"].is_null())
    p.setup_fall = j["setup_fall"].get<double>();
  if (j.contains("hold_rise") && !j["hold_rise"].is_null())
    p.hold_rise = j["hold_rise"].get<double>();
  if (j.contains("hold_fall") && !j["hold_fall"].is_null())
    p.hold_fall = j["hold_fall"].get<double>();
}

static void to_json(json &j, const FFDefinition &f) {
  j["state_var"] = f.state_var;
  j["state_var_inv"] = f.state_var_inv;
  if (f.next_state.has_value()) j["next_state"] = *f.next_state;
  if (f.clocked_on.has_value()) j["clocked_on"] = *f.clocked_on;
  if (f.clear.has_value()) j["clear"] = *f.clear;
  if (f.preset.has_value()) j["preset"] = *f.preset;
}

static void from_json(const json &j, FFDefinition &f) {
  j.at("state_var").get_to(f.state_var);
  j.at("state_var_inv").get_to(f.state_var_inv);
  if (j.contains("next_state") && !j["next_state"].is_null())
    f.next_state = j["next_state"].get<std::string>();
  if (j.contains("clocked_on") && !j["clocked_on"].is_null())
    f.clocked_on = j["clocked_on"].get<std::string>();
  if (j.contains("clear") && !j["clear"].is_null())
    f.clear = j["clear"].get<std::string>();
  if (j.contains("preset") && !j["preset"].is_null())
    f.preset = j["preset"].get<std::string>();
}

static void to_json(json &j, const LeakagePower &p) {
  if (p.when.has_value()) j["when"] = *p.when;
  if (p.value.has_value()) j["value"] = *p.value;
}

static void from_json(const json &j, LeakagePower &p) {
  if (j.contains("when") && !j["when"].is_null()) p.when = j["when"].get<std::string>();
  if (j.contains("value") && !j["value"].is_null()) p.value = j["value"].get<double>();
}

static void to_json(json &j, const PGPin &p) {
  j["name"] = p.name;
  if (p.voltage_name.has_value()) j["voltage_name"] = *p.voltage_name;
  if (p.pg_type.has_value()) j["pg_type"] = *p.pg_type;
}

static void from_json(const json &j, PGPin &p) {
  j.at("name").get_to(p.name);
  if (j.contains("voltage_name") && !j["voltage_name"].is_null())
    p.voltage_name = j["voltage_name"].get<std::string>();
  if (j.contains("pg_type") && !j["pg_type"].is_null())
    p.pg_type = j["pg_type"].get<std::string>();
}

static void to_json(json &j, const StandardCell &c) {
  j["name"] = c.name;
  if (c.area.has_value()) j["area"] = *c.area;
  if (c.drive_strength.has_value()) j["drive_strength"] = *c.drive_strength;
  if (c.cell_leakage_power.has_value()) j["cell_leakage_power"] = *c.cell_leakage_power;
  if (c.ff.has_value()) { json jf; to_json(jf, *c.ff); j["ff"] = jf; }
  j["pg_pins"] = json::array();
  for (const auto &e : c.pg_pins) { json je; to_json(je, e); j["pg_pins"].push_back(je); }
  j["leakage_power"] = json::array();
  for (const auto &e : c.leakage_power) { json je; to_json(je, e); j["leakage_power"].push_back(je); }
  j["pins"] = json::object();
  for (const auto &kv : c.pins) { json jp; to_json(jp, kv.second); j["pins"][kv.first] = jp; }
}

static void from_json(const json &j, StandardCell &c) {
  j.at("name").get_to(c.name);
  if (j.contains("area") && !j["area"].is_null()) c.area = j["area"].get<double>();
  if (j.contains("drive_strength") && !j["drive_strength"].is_null())
    c.drive_strength = j["drive_strength"].get<double>();
  if (j.contains("cell_leakage_power") && !j["cell_leakage_power"].is_null())
    c.cell_leakage_power = j["cell_leakage_power"].get<double>();
  if (j.contains("ff") && !j["ff"].is_null()) { FFDefinition f; from_json(j["ff"], f); c.ff = f; }
  if (j.contains("pg_pins")) {
    c.pg_pins.clear();
    for (const auto &je : j["pg_pins"]) { PGPin e; from_json(je, e); c.pg_pins.push_back(e); }
  }
  if (j.contains("leakage_power")) {
    c.leakage_power.clear();
    for (const auto &je : j["leakage_power"]) { LeakagePower e; from_json(je, e); c.leakage_power.push_back(e); }
  }
  if (j.contains("pins") && j["pins"].is_object()) {
    c.pins.clear();
    for (auto it = j["pins"].begin(); it != j["pins"].end(); ++it) {
      Pin p; from_json(it.value(), p); c.pins[it.key()] = p;
    }
  }
}

static void to_json(json &j, const TableTemplate &t) {
  j["name"] = t.name;
  if (t.variable_1.has_value()) j["variable_1"] = *t.variable_1;
  if (t.variable_2.has_value()) j["variable_2"] = *t.variable_2;
  j["index_1"] = t.index_1;
  j["index_2"] = t.index_2;
}

static void from_json(const json &j, TableTemplate &t) {
  j.at("name").get_to(t.name);
  if (j.contains("variable_1") && !j["variable_1"].is_null())
    t.variable_1 = j["variable_1"].get<std::string>();
  if (j.contains("variable_2") && !j["variable_2"].is_null())
    t.variable_2 = j["variable_2"].get<std::string>();
  j.at("index_1").get_to(t.index_1);
  j.at("index_2").get_to(t.index_2);
}

static json to_json(const CellLibrary &lib) {
  json j;
  j["library_name"] = lib.get_library_name();
  j["time_unit"] = lib.get_time_unit();
  j["capacitance_unit"] = lib.get_capacitance_unit();
  j["cells"] = json::object();
  for (const std::string &name : lib.get_cell_names()) {
    const StandardCell *cell = lib.get_cell(name);
    if (cell) { json jc; to_json(jc, *cell); j["cells"][name] = jc; }
  }
  j["table_templates"] = json::object();
  for (const std::string &name : lib.get_table_template_names()) {
    const TableTemplate *templ = lib.get_table_template(name);
    if (templ) { json jt; to_json(jt, *templ); j["table_templates"][name] = jt; }
  }
  return j;
}

static void from_json(const json &j, CellLibrary &lib) {
  lib.clear();
  if (j.contains("library_name") && !j["library_name"].is_null())
    lib.set_library_name(j["library_name"].get<std::string>());
  if (j.contains("time_unit") && !j["time_unit"].is_null())
    lib.set_time_unit(j["time_unit"].get<std::string>());
  if (j.contains("capacitance_unit") && !j["capacitance_unit"].is_null())
    lib.set_capacitance_unit(j["capacitance_unit"].get<double>());
  if (j.contains("table_templates") && j["table_templates"].is_object()) {
    for (auto it = j["table_templates"].begin(); it != j["table_templates"].end(); ++it) {
      TableTemplate t; from_json(it.value(), t); lib.add_table_template(t);
    }
  }
  if (j.contains("cells") && j["cells"].is_object()) {
    for (auto it = j["cells"].begin(); it != j["cells"].end(); ++it) {
      StandardCell c; from_json(it.value(), c); lib.add_cell(c);
    }
  }
}

} // namespace

bool try_load_celllib_cache(const std::vector<std::string> &lib_file_paths,
                            CellLibrary &out) {
  if (lib_file_paths.empty())
    return false;
  std::string path = get_cache_file_path(lib_file_paths);
  try {
    if (!fs::exists(path) || !fs::is_regular_file(path))
      return false;
    std::ifstream f(path);
    if (!f)
      return false;
    json j = json::parse(f);
    from_json(j, out);
    return true;
  } catch (...) {
    return false;
  }
}

void save_celllib_cache(const std::vector<std::string> &lib_file_paths,
                        const CellLibrary &lib) {
  if (lib_file_paths.empty())
    return;
  std::string path = get_cache_file_path(lib_file_paths);
  try {
    fs::create_directories(CACHE_DIR);
    std::ofstream f(path);
    if (!f)
      return;
    json j = to_json(lib);
    f << j.dump(2);
  } catch (...) {
    // ignore write errors
  }
}

} // namespace celllib
