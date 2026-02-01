#!/bin/bash
# GDB调试脚本 - 将程序输出重定向到另一个tmux窗格
# 用法: ./gdb_debug.sh <可执行文件> [-- 程序参数...]

set -e

# 检查是否在tmux中
if [ -z "$TMUX" ]; then
    echo "错误: 请在tmux会话中运行此脚本"
    echo "使用方法:"
    echo "  1. 启动tmux: tmux"
    echo "  2. 运行脚本: ./gdb_debug.sh ./build/sta -- test.sdc"
    exit 1
fi

# 检查参数
if [ $# -lt 1 ]; then
    echo "用法: $0 <可执行文件> [-- 程序参数...]"
    echo "示例:"
    echo "  $0 ./build/sta"
    echo "  $0 ./build/sta -- test.sdc"
    echo "  $0 ./build/sta -- arg1 arg2 arg3"
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

# 创建临时文件存储从窗格的tty
TTY_FILE=$(mktemp)
trap "rm -f $TTY_FILE" EXIT

# 获取当前窗格ID
MAIN_PANE=$(tmux display-message -p '#{pane_id}')

# 创建一个新的水平分割窗格用于输出
# -h 水平分割, -d 不切换到新窗格, -P 打印新窗格信息
OUTPUT_PANE=$(tmux split-window -h -d -P -F '#{pane_id}')

# 在输出窗格中获取tty并保持窗格打开
tmux send-keys -t "$OUTPUT_PANE" "tty > $TTY_FILE && echo '=== 程序输出窗口 ===' && echo '程序的stdout/stderr将显示在这里' && echo '' && cat" Enter

# 等待tty文件被写入
sleep 0.5

# 读取输出窗格的tty
OUTPUT_TTY=$(cat "$TTY_FILE")

if [ -z "$OUTPUT_TTY" ]; then
    echo "错误: 无法获取输出窗格的tty"
    tmux kill-pane -t "$OUTPUT_PANE"
    exit 1
fi

echo "输出窗格TTY: $OUTPUT_TTY"
echo "启动GDB..."

# 在主窗格中启动GDB，使用tty命令重定向程序输出
# 创建GDB初始化命令
GDB_INIT=$(mktemp)
trap "rm -f $TTY_FILE $GDB_INIT" EXIT

cat > "$GDB_INIT" << EOF
# 设置被调试程序的终端
tty $OUTPUT_TTY

# 设置一些有用的选项
set print pretty on
set pagination off

# 设置程序参数
set args $PROG_ARGS

# 提示信息
echo 程序输出将重定向到右侧窗格 ($OUTPUT_TTY)\n
echo 程序参数: $PROG_ARGS\n
echo 使用 'run' 命令启动程序\n
EOF

# 启动GDB
gdb -tui -x "$GDB_INIT" "$EXECUTABLE"

# GDB退出后，关闭输出窗格
echo "GDB已退出，按Enter关闭输出窗格..."
read
tmux kill-pane -t "$OUTPUT_PANE" 2>/dev/null || true
