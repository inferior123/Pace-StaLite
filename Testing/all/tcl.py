#!/usr/bin/env python2
# -*- coding: utf-8 -*-

import sys
import os
import re

def extract_design_name(verilog_file):
    """从Verilog文件中提取设计名称（第一个module的名称）"""
    try:
        with open(verilog_file, 'r') as f:
            content = f.read()
    except IOError as e:
        print("错误: 无法读取文件 {}: {}".format(verilog_file, e))
        sys.exit(1)
    
    # 匹配第一个module的名称
    # 正则表达式: module\s+(\w+)
    # 忽略注释和空白
    pattern = re.compile(r'module\s+(\w+)')
    match = pattern.search(content)
    
    if match:
        return match.group(1)
    else:
        print("错误: 在文件 {} 中未找到module定义".format(verilog_file))
        sys.exit(1)

def replace_template_variables(template_content, variables):
    """替换模板中的变量"""
    
    # 检查模板中的变量是否都在预期中
    tmpl_vars = re.findall(r'\$TMPL_(\w+)', template_content)
    expected_vars = ['PROJ_PATH', 'SDC_FILE', 'NETLIST_V', 'DESIGN']
    
    for var in tmpl_vars:
        if var not in expected_vars:
            print("错误: 模板中包含未知变量 $TMPL_{}".format(var))
            sys.exit(1)
    
    result = template_content
    
    # 替换所有变量
    for var_name, value in variables.items():
        tmpl_var = "$TMPL_" + var_name
        result = result.replace(tmpl_var, value)
    
    return result

def main():
    # 检查参数数量
    if len(sys.argv) != 4:
        print("用法: {} <verilog_file> <template_file> <output_file>".format(sys.argv[0]))
        print("示例: {} report/simple/simple.v tmpl.tcl report/simple/simple.tcl".format(sys.argv[0]))
        sys.exit(1)
    
    verilog_file = sys.argv[1]
    template_file = sys.argv[2]
    output_file = sys.argv[3]
    
    # 检查输入文件是否存在
    if not os.path.isfile(verilog_file):
        print("错误: Verilog文件 {} 不存在".format(verilog_file))
        sys.exit(1)
    
    if not os.path.isfile(template_file):
        print("错误: 模板文件 {} 不存在".format(template_file))
        sys.exit(1)
    
    # 提取设计名称
    print("正在解析Verilog文件: {}".format(verilog_file))
    design_name = extract_design_name(verilog_file)
    print("  提取的设计名称: {}".format(design_name))
    
    # 构建变量字典
    variables = {
        'PROJ_PATH': os.getcwd(),  # 当前工作目录
        'SDC_FILE': verilog_file.replace('.v', '.sdc'),  # 替换扩展名
        'NETLIST_V': verilog_file,  # 原始Verilog文件路径
        'DESIGN': design_name  # 设计名称
    }
    
    # 显示变量值
    print("变量值:")
    for var_name, value in variables.items():
        print("  $TMPL_{}: {}".format(var_name, value))
    
    # 读取模板文件
    try:
        with open(template_file, 'r') as f:
            template_content = f.read()
    except IOError as e:
        print("错误: 无法读取模板文件 {}: {}".format(template_file, e))
        sys.exit(1)
    
    # 替换模板变量
    print("正在替换模板变量...")
    result_content = replace_template_variables(template_content, variables)
    
    # 写入输出文件
    try:
        # 确保输出目录存在
        output_dir = os.path.dirname(output_file)
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir)
        
        with open(output_file, 'w') as f:
            f.write(result_content)
        print("成功: 已生成文件 {}".format(output_file))
    except IOError as e:
        print("错误: 无法写入输出文件 {}: {}".format(output_file, e))
        sys.exit(1)

if __name__ == "__main__":
    main()