#include "interface/verilog_adapter.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "verilog_data.hpp"
extern "C" {
#include "verilog_parser.h"
}

namespace {
void *rust_vec_get_ptr(RustVec *vec, uintptr_t i) {
  char *base = static_cast<char *>(vec->data);
  return static_cast<void *>(base + i * vec->type_size);
}

verilog::NetType to_net_type(DclType dcl_type) {
  switch (dcl_type) {
  case KWire:
    return verilog::NetType::WIRE;
  case KSupply0:
    return verilog::NetType::SUPPLY0;
  case KSupply1:
    return verilog::NetType::SUPPLY1;
  case KTri:
    return verilog::NetType::TRI;
  case KWand:
    return verilog::NetType::WAND;
  case KWor:
    return verilog::NetType::WOR;
  default:
    return verilog::NetType::NONE;
  }
}

verilog::PortDirection to_port_dir(DclType dcl_type) {
  switch (dcl_type) {
  case KInput:
    return verilog::PortDirection::INPUT;
  case KOutput:
    return verilog::PortDirection::OUTPUT;
  case KInout:
    return verilog::PortDirection::INOUT;
  default:
    assert(false && "invalid port declaration type");
    return verilog::PortDirection::INPUT;
  }
}

verilog::ConnectionType to_connection_type(DclType dcl_type) {
  if (dcl_type == KWire) {
    return verilog::ConnectionType::WIRE;
  }
  return verilog::ConnectionType::NONE;
}

verilog::NetConcat convert_net_id(const void *verilog_id) {
  void *id_ptr = const_cast<void *>(verilog_id);
  if (rust_is_id(id_ptr)) {
    auto *id = rust_convert_verilog_id(id_ptr);
    return std::string(id->id);
  }
  if (rust_is_bus_index_id(id_ptr)) {
    auto *id = rust_convert_verilog_index_id(id_ptr);
    return verilog::NetBit(std::string(id->base_id), id->index);
  }
  if (rust_is_bus_slice_id(id_ptr)) {
    auto *id = rust_convert_verilog_slice_id(id_ptr);
    return verilog::NetRange(std::string(id->base_id), id->range_base,
                             id->range_max);
  }
  assert(false && "unsupported verilog id");
  return std::string();
}

std::vector<verilog::NetConcat> convert_net_expr(const void *net_expr) {
  void *expr_ptr = const_cast<void *>(net_expr);
  std::vector<verilog::NetConcat> out;

  if (rust_is_id_expr(expr_ptr)) {
    auto *id_expr = rust_convert_verilog_net_id_expr(expr_ptr);
    out.emplace_back(convert_net_id(id_expr->verilog_id));
    return out;
  }

  if (rust_is_concat_expr(expr_ptr)) {
    auto *concat_expr = rust_convert_verilog_net_concat_expr(expr_ptr);
    for (uintptr_t i = 0; i < concat_expr->verilog_id_concat.len; ++i) {
      void *concat_id = rust_vec_get_ptr(&concat_expr->verilog_id_concat, i);
      out.emplace_back(convert_net_id(concat_id));
    }
    return out;
  }

  if (rust_is_constant(expr_ptr)) {
    auto *const_expr = rust_convert_verilog_constant_expr(expr_ptr);
    auto constant_data = convert_net_id(const_expr->verilog_id);
    if (std::holds_alternative<std::string>(constant_data)) {
      auto value = std::get<std::string>(constant_data);
      out.emplace_back(
          verilog::Constant(std::move(value), verilog::ConstantType::DECIMAL));
      return out;
    }
    assert(false && "unsupported constant encoding");
  }

  assert(false && "unsupported net expression");
  return out;
}

verilog::LHS to_lhs(const std::vector<verilog::NetConcat> &parts) {
  verilog::LHS lhs;
  for (const auto &item : parts) {
    if (std::holds_alternative<std::string>(item)) {
      lhs.emplace_back(std::get<std::string>(item));
    } else if (std::holds_alternative<verilog::NetBit>(item)) {
      lhs.emplace_back(std::get<verilog::NetBit>(item));
    } else if (std::holds_alternative<verilog::NetRange>(item)) {
      lhs.emplace_back(std::get<verilog::NetRange>(item));
    } else {
      assert(false && "constant is not allowed on lhs");
    }
  }
  return lhs;
}

verilog::RHS to_rhs(const std::vector<verilog::NetConcat> &parts) {
  verilog::RHS rhs;
  for (const auto &item : parts) {
    if (std::holds_alternative<std::string>(item)) {
      rhs.emplace_back(std::get<std::string>(item));
    } else if (std::holds_alternative<verilog::NetBit>(item)) {
      rhs.emplace_back(std::get<verilog::NetBit>(item));
    } else if (std::holds_alternative<verilog::NetRange>(item)) {
      rhs.emplace_back(std::get<verilog::NetRange>(item));
    } else if (std::holds_alternative<verilog::Constant>(item)) {
      rhs.emplace_back(std::get<verilog::Constant>(item));
    } else {
      assert(false && "unsupported rhs item");
    }
  }
  return rhs;
}
} // namespace

MyVerilogParser::MyVerilogParser(sta::STAWorker &worker, std::string file_name)
    : worker_(worker), filename(std::move(file_name)) {}

void MyVerilogParser::set_filename(const std::string &file_name) {
  filename = file_name;
}

const std::string &MyVerilogParser::get_filename() const { return filename; }

void MyVerilogParser::read(const char *verilog_file) {
  if (verilog_file == nullptr) {
    return;
  }
  filename = verilog_file;
  read_with_filename();
}

void MyVerilogParser::read_with_filename() {
  if (filename.empty()) {
    return;
  }
  if (!std::filesystem::exists(filename)) {
    assert(false && "verilog file not found");
  }

  void *verilog_file_ptr = rust_parse_verilog(filename.c_str());
  if (verilog_file_ptr == nullptr) {
    assert(false && "rust verilog parser failed");
  }

  RustVerilogFile *verilog_file =
      rust_convert_verilog_file(static_cast<const VerilogFile *>(verilog_file_ptr));
  if (verilog_file->verilog_modules.len != 1u) {
    assert(false && "auto top module failed, require single flattened module");
  }

  for (uintptr_t stmt_i = 0; stmt_i < verilog_file->verilog_modules.len;
       ++stmt_i) {
    void *mod_ref = rust_vec_get_ptr(&verilog_file->verilog_modules, stmt_i);
    void *mod_ptr = rust_convert_rc_ref_cell_module(
        static_cast<const Rc_RefCell_VerilogModule *>(mod_ref));
    auto *module = rust_convert_raw_verilog_module(
        static_cast<VerilogModule *>(mod_ptr));
    assert(module != nullptr && "top module is null");
    worker_.top_module = module->module_name;

    for (uintptr_t i = 0; i < module->module_stmts.len; ++i) {
      void *stmt = rust_vec_get_ptr(&module->module_stmts, i);
      if (rust_is_verilog_dcl_stmt(stmt) || rust_is_verilog_dcls_stmt(stmt)) {
        RustVec *dcl_vec = nullptr;
        RustVec single_dcl{};
        if (rust_is_verilog_dcl_stmt(stmt)) {
          single_dcl.data = stmt;
          single_dcl.len = 1;
          single_dcl.type_size = sizeof(void *);
          dcl_vec = &single_dcl;
        } else {
          auto *dcls = rust_convert_verilog_dcls(stmt);
          dcl_vec = &dcls->verilog_dcls;
        }
        for (uintptr_t d = 0; d < dcl_vec->len; ++d) {
          void *dcl_ptr = rust_is_verilog_dcl_stmt(stmt) ? stmt : rust_vec_get_ptr(dcl_vec, d);
          auto *dcl = rust_convert_verilog_dcl(dcl_ptr);
          if (dcl->dcl_type == KInput || dcl->dcl_type == KOutput ||
              dcl->dcl_type == KInout) {
            verilog::Port port;
            port.names.emplace_back(dcl->dcl_name);
            if (dcl->range.has_value) {
              port.beg = dcl->range.start;
              port.end = dcl->range.end;
            }
            port.dir = to_port_dir(dcl->dcl_type);
            port.type = to_connection_type(dcl->dcl_type);
            worker_.collect_port(port);
          } else {
            verilog::Net net;
            net.names.emplace_back(dcl->dcl_name);
            if (dcl->range.has_value) {
              net.beg = dcl->range.start;
              net.end = dcl->range.end;
            }
            net.type = to_net_type(dcl->dcl_type);
            worker_.collect_net(net);
          }
        }
        continue;
      }

      if (rust_is_module_assign_stmt(stmt)) {
        auto *assign_stmt = rust_convert_verilog_assign(stmt);
        verilog::Assignment assign;
        assign.lhs = to_lhs(convert_net_expr(assign_stmt->left_net_expr));
        assign.rhs = to_rhs(convert_net_expr(assign_stmt->right_net_expr));
        worker_.collect_assign(assign);
        continue;
      }

      if (rust_is_module_inst_stmt(stmt)) {
        auto *rust_inst = rust_convert_verilog_inst(stmt);
        verilog::Instance inst;
        inst.module_name = rust_inst->cell_name;
        inst.inst_name = rust_inst->inst_name;
        for (uintptr_t c = 0; c < rust_inst->port_connections.len; ++c) {
          void *conn_ptr = rust_vec_get_ptr(&rust_inst->port_connections, c);
          auto *conn = rust_convert_verilog_port_ref_port_connect(conn_ptr);
          auto port_name_data = convert_net_id(conn->port_id);
          assert(std::holds_alternative<std::string>(port_name_data) &&
                 "named connection required");
          inst.pin_names.emplace_back(std::get<std::string>(port_name_data));
          inst.net_names.emplace_back(convert_net_expr(conn->net_expr));
        }
        worker_.collect_instance(inst);
      }
    }
    break;
  }
}
