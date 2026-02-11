#!/usr/bin/env python3
"""
iSTA 回归脚本：

- 从 `Testing/ics55` 所有子文件夹中自动发现 `<design>.v` 与 `<design>.sdc`（可选 `<design>.spef`）
- 为每个 design 生成一份 `run_ista.tcl`，调用 iSTA 做 STA
- 将输出报告落到 `ref/` 目录中，按 design 分目录存放
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from shutil import copy2
from typing import Iterable, Optional

DEFAULT_ISTA = "../iEDA/bin/iSTA"
DEFAULT_ROOT = "Testing/ics55"
DEFAULT_OUT = "ref/iSTA/ics55"
DEFAULT_LIBS = ["lib/ics55.lib"]


@dataclass(frozen=True)
class Case:
    design: str
    case_dir: Path
    verilog: Path
    sdc: Path
    spef: Optional[Path]


def _pick_single(files: list[Path], preferred_stem: str, suffix: str, where: Path) -> Path:
    """Pick a single file from a directory, preferring `<preferred_stem><suffix>`."""
    if not files:
        raise FileNotFoundError(f"Missing {suffix} under {where}")
    if len(files) == 1:
        return files[0]
    preferred = where / f"{preferred_stem}{suffix}"
    if preferred in files:
        return preferred
    names = ", ".join(str(p.name) for p in sorted(files))
    raise RuntimeError(f"Multiple {suffix} files under {where}: {names}")


def discover_cases(root: Path) -> list[Case]:
    """Recursively find directories containing `.v` and matching `.sdc`."""
    cases: list[Case] = []
    for dirpath, _dirnames, filenames in os.walk(root):
        p = Path(dirpath)
        v_files = [p / f for f in filenames if f.endswith(".v")]
        sdc_files = [p / f for f in filenames if f.endswith(".sdc")]
        if not v_files and not sdc_files:
            continue
        # Use directory name as the default design name.
        design = p.name
        verilog = _pick_single(v_files, design, ".v", p)
        sdc = _pick_single(sdc_files, design, ".sdc", p)
        spef = (p / f"{design}.spef") if (p / f"{design}.spef").is_file() else None
        cases.append(Case(design=design, case_dir=p, verilog=verilog, sdc=sdc, spef=spef))
    cases.sort(key=lambda c: c.design)
    return cases


def write_run_tcl(
    *,
    tcl_path: Path,
    workspace_dir: Path,
    design: str,
    netlist_v: Path,
    sdc: Path,
    spef: Optional[Path],
    lib_files: Iterable[Path],
) -> None:
    libs = " ".join(str(p) for p in lib_files)
    lines = [
        f"set_design_workspace {workspace_dir}",
        "",
        f"read_netlist {netlist_v}",
        "",
        f"read_liberty {libs}",
        "",
        f"link_design {design}",
        "",
        f"read_sdc {sdc}",
    ]
    if spef is not None:
        lines.append(f"read_spef {spef}")
    lines += [
        "",
        "report_timing",
        "",
        "exit",
        "",
    ]
    tcl_path.write_text("\n".join(lines), encoding="utf-8")


def _clean_old_outputs(out_dir: Path, design: str) -> None:
    # Avoid mixing previous reports with the new run.
    suffixes = (
        ".rpt",
        ".cap",
        ".fanout",
        ".trans",
        "_hold.skew",
        "_setup.skew",
    )
    for p in out_dir.iterdir():
        if not p.is_file():
            continue
        if p.name == "run_ista.tcl" or p.name == "run.log":
            continue
        if p.name.startswith(design) and any(p.name.endswith(s) for s in suffixes):
            try:
                p.unlink()
            except OSError:
                pass


def run_one(
    *,
    ista_bin: Path,
    case: Case,
    out_root: Path,
    lib_files: list[Path],
    cwd: Path,
    timeout_s: int,
) -> tuple[str, int, str, str]:
    """Run iSTA for one case; place results under out_root/design/."""
    out_dir = out_root / case.design
    out_dir.mkdir(parents=True, exist_ok=True)
    _clean_old_outputs(out_dir, case.design)

    # Copy inputs into ref for reproducibility.
    netlist_dst = out_dir / f"{case.design}.v"
    sdc_dst = out_dir / f"{case.design}.sdc"
    copy2(case.verilog, netlist_dst)
    copy2(case.sdc, sdc_dst)
    spef_dst: Optional[Path] = None
    if case.spef is not None and case.spef.is_file():
        spef_dst = out_dir / f"{case.design}.spef"
        copy2(case.spef, spef_dst)

    tcl_path = out_dir / "run_ista.tcl"
    write_run_tcl(
        tcl_path=tcl_path,
        workspace_dir=out_dir,
        design=case.design,
        netlist_v=netlist_dst,
        sdc=sdc_dst,
        spef=spef_dst,
        lib_files=lib_files,
    )

    cmd = [str(ista_bin), str(tcl_path)]
    try:
        result = subprocess.run(
            cmd,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
        # Save stdout/stderr into ref folder.
        (out_dir / "run.log").write_text(
            "\n".join(
                [
                    f"time={datetime.now().isoformat(timespec='seconds')}",
                    f"cmd={' '.join(cmd)}",
                    "",
                    "----- STDOUT -----",
                    result.stdout or "",
                    "----- STDERR -----",
                    result.stderr or "",
                ]
            ),
            encoding="utf-8",
        )
        return (case.design, result.returncode, result.stdout, result.stderr)
    except subprocess.TimeoutExpired:
        return (case.design, -1, "", "timeout")
    except Exception as e:
        return (case.design, -1, "", str(e))


def main():
    parser = argparse.ArgumentParser(description="Run iSTA for all cases under Testing/ics55 and write results into ref/.")
    parser.add_argument("-j", "--jobs", type=int, default=4, help="Thread pool size (default 4)")
    parser.add_argument("--root", default=DEFAULT_ROOT, help=f"Case root (default: {DEFAULT_ROOT})")
    parser.add_argument("--out", default=DEFAULT_OUT, help=f"Output root under proj/ (default: {DEFAULT_OUT})")
    parser.add_argument("--ista", default=DEFAULT_ISTA, help=f"iSTA binary path (default: {DEFAULT_ISTA})")
    parser.add_argument(
        "--lib",
        action="append",
        default=[],
        help=f"Liberty file path (repeatable). Default: {', '.join(DEFAULT_LIBS)}",
    )
    parser.add_argument("--only", action="append", default=[], help="Only run these designs (repeatable)")
    parser.add_argument("--timeout", type=int, default=600, help="Timeout seconds per case (default 600)")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    cwd = script_dir
    root = (script_dir / args.root).resolve()
    out_root = (script_dir / args.out).resolve()
    ista_bin = (script_dir / args.ista).resolve()
    lib_args = args.lib if args.lib else DEFAULT_LIBS
    lib_files = [(script_dir / p).resolve() for p in lib_args]

    if not root.is_dir():
        print(f"ERROR: root directory not found: {root}", file=sys.stderr)
        sys.exit(1)
    if not ista_bin.is_file():
        print(f"ERROR: iSTA binary not found: {ista_bin}", file=sys.stderr)
        sys.exit(1)
    for lf in lib_files:
        if not lf.is_file():
            print(f"ERROR: liberty file not found: {lf}", file=sys.stderr)
            sys.exit(1)
    out_root.mkdir(parents=True, exist_ok=True)

    cases = discover_cases(root)
    if args.only:
        only = set(args.only)
        cases = [c for c in cases if c.design in only]
    if not cases:
        print(f"WARNING: no cases found under {root}", file=sys.stderr)
        sys.exit(0)

    print(f"root={root}")
    print(f"out={out_root}")
    print(f"ista={ista_bin}")
    print(f"libs={lib_files}")
    print(f"workers={args.jobs} cases={[c.design for c in cases]}")
    print("-" * 60)

    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(
                run_one,
                ista_bin=ista_bin,
                case=case,
                out_root=out_root,
                lib_files=lib_files,
                cwd=cwd,
                timeout_s=args.timeout,
            ): case.design
            for case in cases
        }
        for future in as_completed(futures):
            design_name, code, out, err = future.result()
            status = "PASS" if code == 0 else "FAIL"
            print(f"[{status}] {design_name} (exit {code})")
            if out:
                print(out)
            if err:
                print(err, file=sys.stderr)
            if code != 0:
                failed.append(design_name)

    print("-" * 60)
    if failed:
        print(f"Failed: {failed}")
        sys.exit(1)
    print("All passed.")


if __name__ == "__main__":
    main()
