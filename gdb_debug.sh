#!/bin/bash
# GDB 调试脚本 - 三窗格布局：
#   左侧：仅源码 (layout src) + 交互
#   右上：GDB 输出（log）
#   右下：被调试程序 stdout/stderr
# 用法: ./gdb_debug.sh <可执行文件> [-- 程序参数...]

set -e

# 检查是否在 tmux 中
if [ -z "$TMUX" ]; then
    echo "错误: 请在 tmux 会话中运行此脚本"
    echo "使用方法:"
    echo "  1. 启动 tmux: tmux"
    echo "  2. 运行脚本: ./gdb_debug.sh ./build/sta -- test.sdc"
    exit 1
fi

# 检查参数
if [ $# -lt 1 ]; then
    echo "用法: $0 <可执行文件> [-- 程序参数...]"
    echo "示例:"
    echo "  $0 ./build/sta"
    echo "  $0 ./build/sta -- test.sdc"
    exit 1
fi

EXECUTABLE="$1"
shift

# 解析程序参数（-- 之后的部分）
PROG_ARGS=""
if [ "$1" = "--" ]; then
    shift
    PROG_ARGS="$@"
fi

# 检查可执行文件是否存在
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 找不到可执行文件 '$EXECUTABLE'"
    exit 1
fi

# 临时文件
TTY_FILE=$(mktemp)
GDB_LOG_FILE=$(mktemp)
trap "rm -f $TTY_FILE $GDB_LOG_FILE $GDB_INIT" EXIT

# 当前为主窗格（左侧，用于 GDB）
MAIN_PANE=$(tmux display-message -p '#{pane_id}')

# 1) 右侧大窗格（水平分割）
RIGHT_PANE=$(tmux split-window -h -d -P -F '#{pane_id}')

# 2) 右侧再上下分割：上 = GDB 输出，下 = 程序输出
RIGHT_BOTTOM=$(tmux split-window -t "$RIGHT_PANE" -v -d -P -F '#{pane_id}')
# 此时 RIGHT_PANE = 右上，RIGHT_BOTTOM = 右下

# 右下：程序输出（获取 tty 并挂住）
tmux send-keys -t "$RIGHT_BOTTOM" "tty > $TTY_FILE && echo '=== 程序输出 (stdout/stderr) ===' && echo '' && cat" Enter

# 右上：GDB 输出（先清空/创建 log，再 tail -f）
tmux send-keys -t "$RIGHT_PANE" "echo '=== GDB 输出 ===' && touch $GDB_LOG_FILE && tail -f $GDB_LOG_FILE" Enter

sleep 0.5

OUTPUT_TTY=$(cat "$TTY_FILE")
if [ -z "$OUTPUT_TTY" ]; then
    echo "错误: 无法获取程序输出窗格的 tty"
    tmux kill-pane -t "$RIGHT_BOTTOM" 2>/dev/null || true
    tmux kill-pane -t "$RIGHT_PANE" 2>/dev/null || true
    exit 1
fi

echo "程序输出 TTY: $OUTPUT_TTY"
echo "GDB 日志: $GDB_LOG_FILE"
echo "启动 GDB（左侧仅 layout src）..."

# GDB 初始化：tty、logging、layout src、程序参数
GDB_INIT=$(mktemp)
trap "rm -f $TTY_FILE $GDB_LOG_FILE $GDB_INIT" EXIT

cat > "$GDB_INIT" << EOF
# 被调试程序输出到右下窗格
tty $OUTPUT_TTY

# GDB 输出同时写入右上窗格（set logging 会复制到文件）
set logging file $GDB_LOG_FILE
set logging overwrite on
set logging on

# 左侧只显示源码
layout src

set print pretty on
set pagination off
set args $PROG_ARGS

echo \n程序输出 → 右下 | GDB 输出 → 右上 | 左侧仅 src\n
echo 程序参数: $PROG_ARGS\n
echo 使用 run 启动程序\n
EOF

# 在左侧主窗格启动 GDB
gdb -tui -x "$GDB_INIT" "$EXECUTABLE"

# 退出后清理
echo "GDB 已退出，按 Enter 关闭右侧窗格..."
read
tmux kill-pane -t "$RIGHT_BOTTOM" 2>/dev/null || true
tmux kill-pane -t "$RIGHT_PANE" 2>/dev/null || true
