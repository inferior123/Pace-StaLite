#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 STA/C++ 的路径打印结果中构建一张“系统路径图”的小工具。

思路：
- C++ 里 `STAWorker::run_candidate_graphy_dfs()` 会对每一条路径调用
  `display_result_path_detail(pr)`，打印类似：

    --- Result Path #0 ---
      startpoint: pt12  ...
      endpoint:   pt345 ...
      data_arrival=1234ps
      [0] pt12 -> pt34 ... dir=r incr=12.3ps ...
      [1] pt34 -> pt56 ... dir=f incr=20.0ps ...
      ...

- 本脚本读取这类文本，把每一步 `ptX -> ptY` 当作一条图上的有向边，
  延迟用 `incr=` 的数值。
- 对于同一条边 (X, Y) 在多个路径上重复出现的情况，按你说的“汇聚取最大”，
  也就是只保留该边上 **最大的 incr 延迟**。
- 输出为 Graphviz DOT 文件，可用 graphviz 渲染成 pdf/png 等。

使用方式示例：

1. 运行你的 STA 程序，把所有路径打印到一个日志文件里，例如：

   ./your_sta_binary > sta_paths.log

   确保里面包含 `--- Result Path #... ---` 和步骤行。

2. 生成 DOT 图：

   python3 sta_visualize.py sta_paths.log sta_graph.dot

3. 用 graphviz 渲染（需要系统已安装 graphviz）：

   dot -Tpdf sta_graph.dot -o sta_graph.pdf
"""

import argparse
import re
from dataclasses import dataclass, field
from typing import Dict, Iterable, Tuple, Set


# 匹配路径头：--- Result Path #0 ---
PATH_HEADER_RE = re.compile(r"^---\s+Result Path #(\d+)\s+---")

# 匹配步骤行：
#   [0] pt12 -> pt34 U1/N1(AO22X1H7L)/Y dir=r incr=12.3ps slew=... cap=... arrival=...
STEP_RE = re.compile(
    r"^\s*\[(\d+)\]\s+pt(\d+)\s*->\s*pt(\d+)(.*?)dir=([rf\?])\s+incr=([0-9.eE+\-]+)ps"
)

# 匹配 startpoint/endpoint 行：
#   startpoint: pt12 U1/A type=REGQ
#   endpoint:   pt345 U2/Z type=REGD
STARTPOINT_RE = re.compile(r"^\s*startpoint:\s+pt(\d+)\s*(.*)$")
ENDPOINT_RE = re.compile(r"^\s*endpoint:\s+pt(\d+)\s*(.*)$")


@dataclass
class NodeInfo:
    """
    每个 pt 节点聚合的信息：
    - label: 原始文本描述（包含 instance/module/port）
    - inst_name/module_name/port_name: 结构化拆出来的实例/模块/管脚名
    - max_cap_pf: 所见路径中该点的最大 cap_load
    - max_slew_ns: 所见路径中该点的最大 slew
    - max_arrival_ps: 所见路径中该点的最大 arrival
    - dirs: 出现在该点上的沿方向集合（r/f/?）
    """

    label: str = ""
    inst_name: str = ""
    module_name: str = ""
    port_name: str = ""
    max_cap_pf: float = float("-inf")
    max_slew_ns: float = float("-inf")
    max_arrival_ps: float = float("-inf")
    dirs: Set[str] = field(default_factory=set)


def _clean_label_tail(label: str) -> str:
    """
    把 "U1/A type=REGQ" 裁剪成 "U1/A" 之类，主要是去掉 type=... 之后的部分。
    """
    label = label.strip()
    if not label:
        return ""
    # 截掉 type= 之后的信息，避免 label 太长
    if "type=" in label:
        label = label.split("type=", 1)[0].strip()
    return label


def _parse_pin_label(label: str) -> Tuple[str, str, str]:
    """
    从类似 "U1(NAND2X1)/Y" 的字符串解析出 (inst_name, module_name, port_name)。
    - 顶层端口只有 "port"：返回 ("", "", "port")
    """
    s = label.strip()
    if not s:
        return "", "", ""
    if "(" in s and ")/" in s:
        before, rest = s.split("(", 1)
        inst = before.strip()
        module_part, after = rest.split(")", 1)
        module = module_part.strip()
        port = after.lstrip("/").strip()
        return inst, module, port
    # 视为顶层端口
    return "", "", s


def parse_sta_display(
    lines: Iterable[str],
) -> Tuple[Dict[int, NodeInfo], Set[Tuple[int, int]], Set[int]]:
    """
    解析 display_result_path_detail 打印的所有路径。

    返回：
    - nodes:       point_id -> NodeInfo（聚合 module/instance/port、cap/slew/arrival/dir）
    - edges:       (from_id, to_id) 的集合，仅表示连接关系
    - used_points: 在任意边中出现过的 point_id 集合
    """
    nodes: Dict[int, NodeInfo] = {}
    edges: Set[Tuple[int, int]] = set()
    used_points: Set[int] = set()

    in_path = False

    for raw in lines:
        line = raw.rstrip("\n")

        # 检测路径头，仅用于提高鲁棒性
        if PATH_HEADER_RE.match(line):
            in_path = True
            continue

        if not in_path:
            # 没遇到路径头之前，先不解析，防止误伤其它输出
            continue

        # 解析 startpoint / endpoint 行，收集 point label
        m_start = STARTPOINT_RE.match(line)
        if m_start:
            pid = int(m_start.group(1))
            tail = _clean_label_tail(m_start.group(2) or "")
            if tail:
                ni = nodes.setdefault(pid, NodeInfo())
                if not ni.label:
                    ni.label = tail
                inst, module, port = _parse_pin_label(tail)
                if inst and not ni.inst_name:
                    ni.inst_name = inst
                if module and not ni.module_name:
                    ni.module_name = module
                if port and not ni.port_name:
                    ni.port_name = port
            continue

        m_end = ENDPOINT_RE.match(line)
        if m_end:
            pid = int(m_end.group(1))
            tail = _clean_label_tail(m_end.group(2) or "")
            if tail:
                ni = nodes.setdefault(pid, NodeInfo())
                if not ni.label:
                    ni.label = tail
                inst, module, port = _parse_pin_label(tail)
                if inst and not ni.inst_name:
                    ni.inst_name = inst
                if module and not ni.module_name:
                    ni.module_name = module
                if port and not ni.port_name:
                    ni.port_name = port
            continue

        # 解析步骤行：[i] ptX -> ptY ...
        m_step = STEP_RE.match(line)
        if not m_step:
            continue

        _step_idx_str, from_id_str, to_id_str, between, dir_char, _incr_str = (
            m_step.group(1),
            m_step.group(2),
            m_step.group(3),
            m_step.group(4),
            m_step.group(5),
            m_step.group(6),
        )

        from_id = int(from_id_str)
        to_id = int(to_id_str)

        used_points.add(from_id)
        used_points.add(to_id)

        edges.add((from_id, to_id))

        # 尝试从 between 里抽出终点的文字描述（例如 " U1/N1(AO22X1H7L)/Y"）
        ni = nodes.setdefault(to_id, NodeInfo())

        tail = _clean_label_tail(between or "")
        if tail and not ni.label:
            ni.label = tail
        if tail:
            inst, module, port = _parse_pin_label(tail)
            if inst and not ni.inst_name:
                ni.inst_name = inst
            if module and not ni.module_name:
                ni.module_name = module
            if port and not ni.port_name:
                ni.port_name = port

        # 记录到达该点时的沿方向
        if dir_char in ("r", "f", "?"):
            ni.dirs.add(dir_char)

        # 从整行里解析 slew/cap/arrival，并在节点上做 "汇聚取最大"
        m_cap = re.search(r"cap=([0-9.eE+\-]+)pf", line)
        if m_cap:
            try:
                cap = float(m_cap.group(1))
                if cap > ni.max_cap_pf:
                    ni.max_cap_pf = cap
            except ValueError:
                pass

        m_slew = re.search(r"slew=([0-9.eE+\-]+)ns", line)
        if m_slew:
            try:
                slew = float(m_slew.group(1))
                if slew > ni.max_slew_ns:
                    ni.max_slew_ns = slew
            except ValueError:
                pass

        m_arr = re.search(r"arrival=([0-9.eE+\-]+)ps", line)
        if m_arr:
            try:
                arr = float(m_arr.group(1))
                if arr > ni.max_arrival_ps:
                    ni.max_arrival_ps = arr
            except ValueError:
                pass

    return nodes, edges, used_points


def write_dot(
    nodes: Dict[int, NodeInfo],
    edges: Set[Tuple[int, int]],
    used_points: Set[int],
    fout,
) -> None:
    """
    把解析到的点/边信息写成 Graphviz DOT。
    """
    print("digraph STA {", file=fout)
    print('  rankdir=LR;', file=fout)
    print('  graph [fontsize=10];', file=fout)

    # 1) 统计每个 instance 拥有哪些 pt（pin）
    inst_to_pins: Dict[Tuple[str, str], Set[int]] = {}
    for pid in used_points:
        ni = nodes.get(pid)
        if not ni or not ni.inst_name:
            continue
        key = (ni.inst_name, ni.module_name)
        inst_to_pins.setdefault(key, set()).add(pid)

    # 2) 为每个 instance 建一个 cluster，把 instance 画成方框，pin 画成圆
    inst_id_map: Dict[Tuple[str, str], str] = {}
    for idx, (key, pin_ids) in enumerate(sorted(inst_to_pins.items())):
        inst_name, module_name = key
        inst_node_id = f"inst_{idx}"
        inst_id_map[key] = inst_node_id

        print(f'  subgraph "cluster_{idx}" {{', file=fout)
        print('    style=dashed;', file=fout)
        cluster_label = (
            f"{inst_name}" + (f"\\n({module_name})" if module_name else "")
        )
        print(f'    label="{cluster_label}";', file=fout)

        # instance 节点（方框）
        print(
            f'    "{inst_node_id}" [label="{inst_name}\\n({module_name})", '
            f'shape=box, style=filled, fillcolor="#e0e0ff"];',
            file=fout,
        )

        # 该 instance 下面的 pin 节点（圆），并连接 instance -> pin
        for pid in sorted(pin_ids):
            ni = nodes.get(pid, NodeInfo())
            lines = [f"pt{pid}"]
            # pin 文本标签：优先用 port 名，其次用原 label
            pin_text = ni.port_name or ni.label
            if pin_text:
                lines.append(pin_text)
            if ni.max_cap_pf > float("-inf"):
                lines.append(f"Cap={ni.max_cap_pf:.4g}pf")
            if ni.max_slew_ns > float("-inf"):
                lines.append(f"Slew={ni.max_slew_ns:.4g}ns")
            if ni.max_arrival_ps > float("-inf"):
                lines.append(f"Arr={ni.max_arrival_ps:.4g}ps")
            if ni.dirs:
                dirs_str = "/".join(sorted(ni.dirs))
                lines.append(f"Dir={dirs_str}")
            label = "\\n".join(lines)
            pin_node_id = f"pt_{pid}"
            print(
                f'    "{pin_node_id}" [label="{label}", shape=circle];',
                file=fout,
            )
            print(
                f'    "{inst_node_id}" -> "{pin_node_id}" '
                f'[style=dotted, arrowsize=0.6];',
                file=fout,
            )

        print("  }", file=fout)

    # 3) 没有隶属于任何 instance 的顶层端口 / 虚拟点，单独画出来（圆）
    for pid in sorted(used_points):
        ni = nodes.get(pid, NodeInfo())
        if ni and ni.inst_name:
            continue  # 已经在对应的 cluster 里画过
        lines = [f"pt{pid}"]
        if ni:
            text = ni.port_name or ni.label
            if text:
                lines.append(text)
            if ni.max_cap_pf > float("-inf"):
                lines.append(f"Cap={ni.max_cap_pf:.4g}pf")
            if ni.max_slew_ns > float("-inf"):
                lines.append(f"Slew={ni.max_slew_ns:.4g}ns")
            if ni.max_arrival_ps > float("-inf"):
                lines.append(f"Arr={ni.max_arrival_ps:.4g}ps")
            if ni.dirs:
                dirs_str = "/".join(sorted(ni.dirs))
                lines.append(f"Dir={dirs_str}")
        label = "\\n".join(lines)
        pin_node_id = f"pt_{pid}"
        print(
            f'  "{pin_node_id}" [label="{label}", shape=circle];',
            file=fout,
        )

    # 4) 路径连线：在 pin 之间连接，不在边上放数值
    for from_id, to_id in sorted(edges):
        from_node = f"pt_{from_id}"
        to_node = f"pt_{to_id}"
        print(f'  "{from_node}" -> "{to_node}";', file=fout)

    print("}", file=fout)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "从 STA C++ 的 display_result_path_detail 输出中，"
            "构建路径图（节点上聚合 module/instance/port 以及 cap/slew/arrival/dir），"
            "并生成 Graphviz DOT 文件。"
        )
    )
    parser.add_argument(
        "input",
        help="包含 Result Path / [i] ptX -> ptY 行的日志文件（例如程序 stdout 重定向的文件）",
    )
    parser.add_argument(
        "output",
        help="输出 DOT 文件路径（例如 sta_graph.dot）",
    )
    args = parser.parse_args()

    with open(args.input, "r", encoding="utf-8", errors="ignore") as fin:
        nodes, edges, used_points = parse_sta_display(fin)

    if not edges:
        print(
            "警告：没有解析到任何路径步骤（[i] ptX -> ptY ...），"
            "请确认输入文件是 display_result_path_detail 的完整输出。",
        )

    with open(args.output, "w", encoding="utf-8") as fout:
        write_dot(nodes, edges, used_points, fout)

    print(
        f"已生成 DOT 文件：{args.output}\n"
        "你可以用 graphviz 渲染，例如：\n"
        f"  dot -Tpdf {args.output} -o sta_graph.pdf"
    )


if __name__ == "__main__":
    main()

