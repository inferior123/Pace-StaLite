#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../third_party/liberty-parser/LibParserRustC.hh"

// 遍历 attri_values，打印属性值
// attri_values 存的是 Vec<Box<dyn LibertyAttrValue>>（string/float），不是
// LibertyStmt
static void print_attributes(const RustVec *attri_values) {
  uintptr_t len = attri_values->len;
  for (uintptr_t i = 0; i < len; ++i) {
    void *elem = ((char *)attri_values->data) + i * attri_values->type_size;
    if (!elem)
      continue;

    if (rust_is_string_value(elem)) {
      auto *sv = rust_convert_string_value(elem);
      std::cout << "    [attr] = \"" << (sv->value ? sv->value : "") << "\"\n";
      rust_free_string_value(sv);
    } else if (rust_is_float_value(elem)) {
      auto *fv = rust_convert_float_value(elem);
      std::cout << "    [attr] = " << fv->value << "\n";
      rust_free_float_value(fv);
    }
  }
}

// 递归遍历 group（只打印前两层）
static void walk_group(const RustLibertyGroupStmt *group, int depth) {
  std::string indent(depth * 2, ' ');
  std::cout << indent << "[group] "
            << (group->group_name ? group->group_name : "?") << " (line "
            << group->line_no << ")\n";

  if (depth == 0) {
    print_attributes(&group->attri_values);
  }

  uintptr_t stmt_len = group->stmts.len;
  for (uintptr_t i = 0; i < stmt_len && depth < 2; ++i) {
    void *elem = ((char *)group->stmts.data) + i * group->stmts.type_size;
    if (!elem)
      continue;

    // elem 是 &Box<dyn LibertyStmt>，应直接传 elem 给 rust_convert_group_stmt
    if (rust_is_group_stmt(elem)) {
      auto *sub = rust_convert_group_stmt(elem);
      walk_group(sub, depth + 1);
      rust_free_group_stmt(sub);
    }
  }
}

int main(int argc, char *argv[]) {
  const char *lib_path =
      (argc > 1) ? argv[1]
                 : "/home/ysyx/project/pba-sta-base/proj/lib/ics55.lib";

  std::cout << "Parsing: " << lib_path << "\n";

  void *raw = rust_parse_lib(lib_path);
  if (!raw) {
    std::cerr << "ERROR: rust_parse_lib returned NULL\n";
    return 1;
  }

  // rust_parse_lib 返回 thin pointer (*mut LibertyGroupStmt)，必须用
  // rust_convert_raw_group_stmt rust_convert_group_stmt 仅用于遍历 stmts 时的
  // fat pointer (Box<dyn LibertyStmt>)
  auto *root = rust_convert_raw_group_stmt(raw);
  if (!root) {
    std::cerr << "ERROR: rust_convert_raw_group_stmt returned NULL\n";
    return 1;
  }

  std::cout << "--- Liberty AST (first 2 levels) ---\n";
  walk_group(root, 0);
  std::cout << "--- Done ---\n";

  rust_free_group_stmt(root);
  rust_free_lib_group(raw); // 释放 Rust 侧的原始解析结果
  return 0;
}
