#!/usr/bin/env python3
"""
解析 ref（PT/iEDA）与 candidate 的 timing 报告，对比 slack、data arrival、data required。
用法:
  python3 compare_timing_reports.py --ref ./ref/simpel_pt --candidate ./result/candidate/simple
  python3 compare_timing_reports.py --ref ./ref/simpel_pt --candidate ./result/candidate/simple --tolerance 0.01
"""

import argparse
import re
import sys
from pathlib import Path

REPORT_NAMES = [
    "timing_max_in2out",
    "timing_max_in2reg",
    "timing_max_reg2reg",
    "timing_max_reg2out",
    "timing_min_in2out",
    "timing_min_in2reg",
    "timing_min_reg2reg",
    "timing_min_reg2out",
]
COL_LABELS = [
    "max_in2out",
    "max_in2reg",
    "max_reg2reg",
    "max_reg2out",
    "min_in2out",
    "min_in2reg",
    "min_reg2reg",
    "min_reg2out",
]
# 仅一方有 path 时的符号：R=仅 PT 有，C=仅本工具有，-=双方无
# 说明：reg2reg = 起点为 CLK、终点为 REGD（时钟→launch FF Q→…→capture FF D）；同一类型下
#       max/min 用同一批 path 仅排序不同；若某类型无 path 则 max/min 都会显示 R 或 -。
SYM_REF_ONLY = "R"
SYM_CAND_ONLY = "C"
SYM_NONE = "-"


def parse_rpt(path: Path) -> list[dict]:
    """解析一个 .rpt 文件，返回多段路径的 [{"slack": float, "data_arrival": float, "data_required": float}, ...]"""
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")

    # 按 "Startpoint:" 分块（每个 path 一块）
    blocks = re.split(r"\n\s*Startpoint:\s*", text)
    results = []

    for block in blocks:
        if "Endpoint:" not in block or "Path Type:" not in block:
            continue

        # slack: "  slack (MET)    9.85..." 或 "  slack (VIOLATED)  -0.01..."
        m_slack = re.search(r"slack\s+\((?:MET|VIOLATED)\)\s*([-\d.]+)", block)
        # data arrival time: 取第一个带数字的（path 段内的 arrival）
        m_arrival = re.search(r"data arrival time\s+([-\d.]+)", block)
        # data required time: 第一个出现即可（path 内和 summary 内数值相同）
        m_required = re.search(r"data required time\s+([-\d.]+)", block)

        slack = float(m_slack.group(1)) if m_slack else None
        # 第一个 "data arrival time" 为 path 段内的到达时间
        data_arrival = float(m_arrival.group(1)) if m_arrival else None
        data_required = float(m_required.group(1)) if m_required else None

        if slack is not None:
            results.append(
                {
                    "slack": slack,
                    "data_arrival": data_arrival,
                    "data_required": data_required,
                }
            )
    return results


def error_one(ref_paths: list[dict], cand_paths: list[dict]):
    """
    返回：数字（误差）| "R"（仅 PT 有）| "C"（仅本工具有）| "-"（双方无）
    """
    has_ref = len(ref_paths) > 0
    has_cand = len(cand_paths) > 0
    if not has_ref and not has_cand:
        return SYM_NONE
    if has_ref and not has_cand:
        return SYM_REF_ONLY
    if not has_ref and has_cand:
        return SYM_CAND_ONLY
    ref_slack = ref_paths[0]["slack"]
    cand_slack = cand_paths[0]["slack"]
    return round(abs(ref_slack - cand_slack), 6)


def run_table(ref_root: Path, candidate_root: Path) -> None:
    """按 design 匹配 ref（ics55）与 result1/candidate，打印误差表格。"""
    ref_designs = {
        d.name
        for d in ref_root.iterdir()
        if d.is_dir() and (d / "timing_max_in2reg.rpt").exists()
    }
    cand_designs = {
        d.name
        for d in candidate_root.iterdir()
        if d.is_dir() and (d / "timing_max_in2reg.rpt").exists()
    }
    designs = sorted(ref_designs & cand_designs)

    if not designs:
        print("未找到 ref 与 candidate 共有的 design", file=sys.stderr)
        return

    W_DESIGN = 14
    W_COL = 12
    SEP = "  "

    def cell(x) -> str:
        s = str(x)
        if s == SYM_NONE:
            return SYM_NONE.rjust(W_COL)
        if s in (SYM_REF_ONLY, SYM_CAND_ONLY):
            return s.rjust(W_COL)
        return s.rjust(W_COL)

    print("# 误差大小（误差越小，数值越接近0）  R=仅PT有  C=仅我有  -=双方无")
    header = (
        "design".ljust(W_DESIGN) + SEP + SEP.join(c.ljust(W_COL) for c in COL_LABELS)
    )
    print(header)

    for design in designs:
        ref_dir = ref_root / design
        cand_dir = candidate_root / design
        parts = [design.ljust(W_DESIGN)]
        for rpt_name in REPORT_NAMES:
            ref_paths = parse_rpt(ref_dir / f"{rpt_name}.rpt")
            cand_paths = parse_rpt(cand_dir / f"{rpt_name}.rpt")
            err = error_one(ref_paths, cand_paths)
            parts.append(cell(err))
        print(SEP.join(parts))


def compare_one(
    name: str,
    ref_paths: list[dict],
    cand_paths: list[dict],
    tolerance: float,
) -> tuple[bool, list[str]]:
    """对比一个报告类型。返回 (ok, messages)。"""
    msgs = []
    ok = True

    if not ref_paths and not cand_paths:
        msgs.append(f"  {name}: 两边均无路径，一致")
        return True, msgs
    if not ref_paths:
        msgs.append(f"  {name}: ref 无路径，candidate 有 {len(cand_paths)} 条")
        return False, msgs
    if not cand_paths:
        msgs.append(f"  {name}: candidate 无路径，ref 有 {len(ref_paths)} 条")
        return False, msgs

    if len(ref_paths) != len(cand_paths):
        ok = False
        msgs.append(
            f"  {name}: 路径条数不一致 ref={len(ref_paths)} candidate={len(cand_paths)}"
        )

    n = min(len(ref_paths), len(cand_paths))
    for i in range(n):
        r, c = ref_paths[i], cand_paths[i]
        for key in ("slack", "data_arrival", "data_required"):
            rv = r.get(key)
            cv = c.get(key)
            if rv is None and cv is None:
                continue
            if rv is None or cv is None:
                ok = False
                msgs.append(f"    path{i + 1} {key}: ref={rv} cand={cv} (缺失)")
                continue
            diff = abs(rv - cv)
            if diff > tolerance:
                ok = False
                msgs.append(
                    f"    path{i + 1} {key}: ref={rv} cand={cv} diff={diff:.6f} > {tolerance}"
                )
    if ok and n > 0:
        msgs.append(f"  {name}: {n} 条路径数值在容差 {tolerance} 内一致")
    return ok, msgs


def main():
    parser = argparse.ArgumentParser(
        description="对比 ref（PT/ics55）与 candidate 的 timing 报告"
    )
    parser.add_argument(
        "--table",
        action="store_true",
        help="输出误差表格：按 design 匹配 ref-root 与 candidate-root，R=仅PT有 C=仅我有 -=双方无",
    )
    parser.add_argument(
        "--ref-root",
        type=Path,
        default=Path("Testing/ics55"),
        help="表格模式：ref 根目录（PT 结果）",
    )
    parser.add_argument(
        "--candidate-root",
        type=Path,
        default=Path("result/candidate"),
        help="表格模式：candidate 根目录",
    )
    parser.add_argument(
        "--ref",
        type=Path,
        default=None,
        help="单 design：ref 报告目录",
    )
    parser.add_argument(
        "--candidate",
        type=Path,
        default=None,
        help="单 design：candidate 报告目录",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=0.02,
        help="单 design 容差（ns）",
    )
    args = parser.parse_args()

    if args.table:
        if not args.ref_root.is_dir():
            print(f"ERROR: ref 根目录不存在: {args.ref_root}", file=sys.stderr)
            sys.exit(1)
        if not args.candidate_root.is_dir():
            print(
                f"ERROR: candidate 根目录不存在: {args.candidate_root}", file=sys.stderr
            )
            sys.exit(1)
        run_table(args.ref_root, args.candidate_root)
        return

    ref_dir = args.ref or Path("ref/simpel_pt")
    cand_dir = args.candidate or Path("result/candidate/simple")
    if not ref_dir.is_dir():
        print(f"ERROR: ref 目录不存在: {ref_dir}", file=sys.stderr)
        sys.exit(1)
    if not cand_dir.is_dir():
        print(f"ERROR: candidate 目录不存在: {cand_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Ref:       {ref_dir}")
    print(f"Candidate: {cand_dir}")
    print(f"Tolerance: {args.tolerance} ns")
    print("-" * 60)

    all_ok = True
    for name in REPORT_NAMES:
        rpt_name = name + ".rpt"
        ref_paths = parse_rpt(ref_dir / rpt_name)
        cand_paths = parse_rpt(cand_dir / rpt_name)
        ok, msgs = compare_one(name, ref_paths, cand_paths, args.tolerance)
        for m in msgs:
            print(m)
        if not ok:
            all_ok = False

    print("-" * 60)
    if all_ok:
        print("PASS: 所有报告类型对比通过")
        sys.exit(0)
    else:
        print("FAIL: 存在不一致或缺失")
        sys.exit(1)


if __name__ == "__main__":
    main()
