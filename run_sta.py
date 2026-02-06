#!/usr/bin/env python3
"""
STA 回归脚本：硬编码 test_list 与 tcl ref，用线程池执行 TCL 并打印结果。
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

# ---------- 硬编码：TCL 脚本路径（ref） ----------
TCL_REF = "./sta.tcl"

# ---------- 硬编码：测试列表（design 名，传给 TCL 的 argv） ----------
TEST_LIST = [
    "simple",
]

# ---------- 硬编码：STA 可执行程序（执行 tcl 的 ref） ----------
MODLE_REF = "../iEDA/bin/iSTA"


def run_one(tcl_ref: str, design_name: str, cwd: str) -> tuple[str, int, str, str]:
    """执行 <MODLE_REF> <tcl_ref>，design 名通过环境变量 DESIGN_NAME 传入，避免被 iSTA 当文件名打开。"""
    env = {**os.environ, "DESIGN_NAME": design_name}
    cmd = [MODLE_REF, tcl_ref]
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            env=env,
            capture_output=True,
            text=True,
            timeout=300,
        )
        return (design_name, result.returncode, result.stdout, result.stderr)
    except subprocess.TimeoutExpired:
        return (design_name, -1, "", "timeout")
    except Exception as e:
        return (design_name, -1, "", str(e))


def main():
    parser = argparse.ArgumentParser(description="Run STA TCL script for each design (hardcoded list) with a thread pool.")
    parser.add_argument("-j", "--jobs", type=int, default=4, help="Thread pool size (default 4)")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    cwd = script_dir
    tcl_ref = os.path.normpath(os.path.join(cwd, TCL_REF))
    tests = TEST_LIST

    if not os.path.isfile(tcl_ref):
        print(f"ERROR: TCL script not found: {tcl_ref}", file=sys.stderr)
        sys.exit(1)
    if not tests:
        print("WARNING: TEST_LIST is empty.", file=sys.stderr)
        sys.exit(0)

    print(f"ref={tcl_ref} workers={args.jobs} tests={tests}")
    print("-" * 60)

    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run_one, tcl_ref, design, cwd): design for design in tests}
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
