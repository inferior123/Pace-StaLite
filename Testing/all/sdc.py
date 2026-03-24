#!/usr/bin/env python2
# -*- coding: utf-8 -*-

import sys
import os
import re

def parse_verilog_file(verilog_file):
    """解析Verilog文件，提取输入、输出和时钟信号"""
    
    input_list = []
    output_list = []
    clock_signal = None
    
    try:
        with open(verilog_file, 'r') as f:
            content = f.read()
    except IOError as e:
        print("错误: 无法读取文件 {}: {}".format(verilog_file, e))
        sys.exit(1)
    
    # 匹配所有input定义
    input_pattern = re.compile(r'\s*input\s+(\w+)\s*;', re.MULTILINE)
    for match in input_pattern.finditer(content):
        input_name = match.group(1)
        # 检查是否有逗号（多信号定义）
        if ',' in input_name:
            print("错误: 检测到逗号在input定义中，不支持多信号定义: {}".format(match.group(0)))
            sys.exit(1)
        input_list.append(input_name)
    
    # 匹配所有output定义
    output_pattern = re.compile(r'\s*output\s+(\w+)\s*;', re.MULTILINE)
    for match in output_pattern.finditer(content):
        output_name = match.group(1)
        # 检查是否有逗号（多信号定义）
        if ',' in output_name:
            print("错误: 检测到逗号在output定义中，不支持多信号定义: {}".format(match.group(0)))
            sys.exit(1)
        output_list.append(output_name)
    
    # 查找时钟信号
    ck_pattern = re.compile(r'\.CK\s*\(\s*(\w+)\s*\)')
    for match in ck_pattern.finditer(content):
        potential_clock = match.group(1)
        if potential_clock in input_list:
            clock_signal = potential_clock
            # 从input_list中移除时钟信号
            input_list.remove(clock_signal)
            break  # 找到第一个时钟信号后就停止查找
    
    if clock_signal is None:
        print("警告: 未找到时钟信号")
    
    return clock_signal, input_list, output_list

def replace_template_variables(template_content, clock_signal, input_list, output_list):
    """替换模板中的变量"""
    
    # 检查模板中的变量是否都在预期中
    tmpl_vars = re.findall(r'\$TMPL_(\w+)', template_content)
    expected_vars = ['CLK_NAME', 'INPUT_LIST', 'OUTPUT_LIST']
    
    for var in tmpl_vars:
        if var not in expected_vars:
            print("错误: 模板中包含未知变量 $TMPL_{}".format(var))
            sys.exit(1)
    
    # 替换时钟信号
    if clock_signal is None:
        print("警告: 时钟信号未定义，将使用空字符串替换")
        clock_signal = ""
    
    result = template_content.replace('$TMPL_CLK_NAME', clock_signal)
    
    # 替换输入列表（已移除时钟信号）
    input_str = ' '.join(input_list)
    result = result.replace('$TMPL_INPUT_LIST', input_str)
    
    # 替换输出列表
    output_str = ' '.join(output_list)
    result = result.replace('$TMPL_OUTPUT_LIST', output_str)
    
    return result

def main():
    # 检查参数数量
    if len(sys.argv) != 4:
        print("用法: {} <verilog_file> <template_file> <output_file>".format(sys.argv[0]))
        print("示例: {} report/simple/simple.v tmpl.sdc report/simple/simple.sdc".format(sys.argv[0]))
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
    
    # 解析Verilog文件
    print("正在解析Verilog文件: {}".format(verilog_file))
    clock_signal, input_list, output_list = parse_verilog_file(verilog_file)
    
    # 打印提取的信息
    print("提取的信息:")
    if clock_signal:
        print("  时钟信号: {}".format(clock_signal))
    else:
        print("  时钟信号: 未找到")
    print("  输入信号列表: {}".format(' '.join(input_list) if input_list else "无"))
    print("  输出信号列表: {}".format(' '.join(output_list) if output_list else "无"))
    
    # 读取模板文件
    try:
        with open(template_file, 'r') as f:
            template_content = f.read()
    except IOError as e:
        print("错误: 无法读取模板文件 {}: {}".format(template_file, e))
        sys.exit(1)
    
    # 替换模板变量
    print("正在替换模板变量...")
    result_content = replace_template_variables(template_content, clock_signal, input_list, output_list)
    
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