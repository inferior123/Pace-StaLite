#!/bin/bash

PROJECT_DIR="${1:-.}"
cd "$PROJECT_DIR" || exit 1

EXCLUDE=( -not -path "*/build/*" -not -path "*/.git/*" -not -path "*/.cache/*"
          -not -path "*/CMakeFiles/*" -not -path "*/Testing/*"
          -not -path "*/_deps/*" -not -path "*/third_party/*" )

FILES=( -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) )

G='\033[0;32m'; B='\033[0;34m'; NC='\033[0m'

total_lines() {
    local out
    out=$(find . "${FILES[@]}" "${EXCLUDE[@]}" "$@" 2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
    echo "${out:-0}"
}

file_count() {
    find . "${FILES[@]}" "${EXCLUDE[@]}" "$@" 2>/dev/null | wc -l
}

echo "=========================================="
echo "  代码行数统计报告"
echo "=========================================="
printf "项目目录: %s\n" "$(pwd)"
printf "统计时间: %s\n\n" "$(date '+%Y-%m-%d %H:%M:%S')"

# 按文件类型
echo -e "${B}=== 按文件类型 ===${NC}"
for ext in cpp hpp h cc c; do
    n=$(find . -type f -name "*.$ext" "${EXCLUDE[@]}" 2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
    c=$(find . -type f -name "*.$ext" "${EXCLUDE[@]}" 2>/dev/null | wc -l)
    [ "${n:-0}" -gt 0 ] && printf "  %-8s %6d 行  (%d 文件)\n" ".$ext" "$n" "$c"
done
printf "  ${G}%-8s %6d 行${NC}\n\n" "合计" "$(total_lines)"

# 按目录
echo -e "${B}=== 按目录 ===${NC}"
for dir in src include; do
    [ -d "$dir" ] || continue
    n=$(find "$dir" "${FILES[@]}" "${EXCLUDE[@]}" 2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
    c=$(find "$dir" "${FILES[@]}" "${EXCLUDE[@]}" 2>/dev/null | wc -l)
    printf "  %-20s %6d 行  (%d 文件)\n" "$dir/" "${n:-0}" "$c"
done
echo ""

# Top 20 文件
echo -e "${B}=== 文件行数 Top 20 ===${NC}"
find . "${FILES[@]}" "${EXCLUDE[@]}" 2>/dev/null \
    | xargs wc -l 2>/dev/null \
    | sort -rn \
    | awk 'NR==1{next} NR<=21{printf "  %6d  %s\n", $1, $2}'
echo ""
echo "=========================================="
