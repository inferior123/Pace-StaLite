#!/bin/bash

# 代码行数统计脚本
# 用法: ./count_lines.sh [目录路径]

PROJECT_DIR="${1:-.}"
cd "$PROJECT_DIR" || exit 1

echo "=========================================="
echo "  代码行数统计报告"
echo "=========================================="
echo "项目目录: $(pwd)"
echo "统计时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 统计函数
count_lines() {
    local pattern="$1"
    local desc="$2"
    local files=$(find . -type f -name "$pattern" \
        ! -path "*/build/*" \
        ! -path "*/.git/*" \
        ! -path "*/.cache/*" \
        ! -path "*/CMakeFiles/*" \
        ! -path "*/Testing/*" \
        2>/dev/null)
    
    if [ -z "$files" ]; then
        echo "0"
        return 0
    fi
    
    local total=$(echo "$files" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
    local file_count=$(echo "$files" | wc -l)
    
    if [ "$total" != "total" ] && [ -n "$total" ]; then
        printf "  %-20s %6s 行  (%3d 文件)\n" "$desc:" "$total" "$file_count"
        echo "$total"
    else
        echo "0"
    fi
}

# 按文件类型统计
echo -e "${BLUE}=== 按文件类型统计 ===${NC}"
cpp_lines=$(count_lines "*.cpp" "C++ 源文件 (.cpp)")
hpp_lines=$(count_lines "*.hpp" "C++ 头文件 (.hpp)")
h_lines=$(count_lines "*.h" "C 头文件 (.h)")
cc_lines=$(count_lines "*.cc" "C++ 源文件 (.cc)")
c_lines=$(count_lines "*.c" "C 源文件 (.c)")

total_all=$((cpp_lines + hpp_lines + h_lines + cc_lines + c_lines))
echo -e "  ${GREEN}总计:${NC}              ${GREEN}$total_all${NC} 行"
echo ""

# 按目录统计
echo -e "${BLUE}=== 按目录统计 ===${NC}"
for dir in src include; do
    if [ -d "$dir" ]; then
        lines=$(find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) \
            ! -path "*/build/*" \
            ! -path "*/.git/*" \
            ! -path "*/.cache/*" \
            ! -path "*/CMakeFiles/*" \
            2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
        file_count=$(find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) \
            ! -path "*/build/*" \
            ! -path "*/.git/*" \
            ! -path "*/.cache/*" \
            ! -path "*/CMakeFiles/*" \
            2>/dev/null | wc -l)
        printf "  %-20s %6s 行  (%3d 文件)\n" "$dir/:" "$lines" "$file_count"
    fi
done
echo ""

# 按文件统计（Top 15）
echo -e "${BLUE}=== 文件行数排行 (Top 15) ===${NC}"
find . -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) \
    ! -path "*/build/*" \
    ! -path "*/.git/*" \
    ! -path "*/.cache/*" \
    ! -path "*/CMakeFiles/*" \
    ! -path "*/Testing/*" \
    2>/dev/null | xargs wc -l 2>/dev/null | \
    sort -rn | head -16 | \
    awk 'NR==1 {total=$1; next} {printf "  %6s  %s\n", $1, substr($0, index($0,$2))}'
echo ""

# 详细文件列表
echo -e "${BLUE}=== 详细文件列表 ===${NC}"
find . -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) \
    ! -path "*/build/*" \
    ! -path "*/.git/*" \
    ! -path "*/.cache/*" \
    ! -path "*/CMakeFiles/*" \
    ! -path "*/Testing/*" \
    2>/dev/null | while read -r file; do
    lines=$(wc -l < "$file" 2>/dev/null)
    printf "  %6s  %s\n" "$lines" "${file#./}"
done | sort -rn

echo ""
echo "=========================================="