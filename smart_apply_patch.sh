#!/bin/bash

# 智能补丁应用脚本 - 增强版

# 配置补丁搜索路径
ARMBIAN_BASE="${ARMBIAN_BASE:-$HOME/armbian-build}"
PATCH_DIRS=(
    "$ARMBIAN_BASE/patch/misc"
    "$ARMBIAN_BASE/patch/kernel/meson64-current"
    "$ARMBIAN_BASE/patch/kernel/meson64-edge"
    "$ARMBIAN_BASE/patch/u-boot/u-boot-meson64"
)

# U-Boot 和 Kernel 特定路径
UBOOT_PATCH_DIRS=(
    "$ARMBIAN_BASE/patch/u-boot/u-boot-meson64"
)

KERNEL_PATCH_DIRS=(
	"$ARMBIAN_BASE/patch/misc"
    "$ARMBIAN_BASE/patch/kernel/meson64-current"
    "$ARMBIAN_BASE/patch/kernel/meson64-edge"
    "$ARMBIAN_BASE/patch/kernel/meson64-legacy"
)

display_alert() {
    local message="$1"
    local status="$2"
    local level="$3"
    
    case "$level" in
        "wrn") echo -e "\033[33m[WARNING]\033[0m $message" ;;
        "info") echo -e "\033[32m[INFO]\033[0m $message" ;;
        *) echo "[$level] $message" ;;
    esac
}

find_patch() {
    local patch_name="$1"
    local search_dirs=("${@:2}")  # 从第二个参数开始的所有参数作为搜索目录
    local found_patches=()
    
    # 如果没有指定搜索目录，使用默认目录
    if [[ ${#search_dirs[@]} -eq 0 ]]; then
        search_dirs=("${PATCH_DIRS[@]}")
    fi
    
    # 如果是完整路径且存在，直接返回
    if [[ -f "$patch_name" ]]; then
        echo "$patch_name"
        return 0
    fi
    
    # 在指定目录中搜索
    for dir in "${search_dirs[@]}"; do
        [[ ! -d "$dir" ]] && continue
        
        # 精确匹配
        if [[ -f "$dir/$patch_name" ]]; then
            found_patches+=("$dir/$patch_name")
        fi
        
        # 如果没有扩展名，尝试添加 .patch 和 .diff
        if [[ "$patch_name" != *.* ]]; then
            [[ -f "$dir/$patch_name.patch" ]] && found_patches+=("$dir/$patch_name.patch")
            [[ -f "$dir/$patch_name.diff" ]] && found_patches+=("$dir/$patch_name.diff")
        fi
        
        # 模糊搜索（使用 -L 选项跟随符号链接）
        while IFS= read -r -d '' file; do
            found_patches+=("$file")
        done < <(find -L "$dir" -maxdepth 1 -name "*$patch_name*" \( -name "*.patch" -o -name "*.diff" \) -print0 2>/dev/null)
    done
    
    # 去重（可能同一个补丁被多次找到）
    local unique_patches=($(printf '%s\n' "${found_patches[@]}" | sort -u))
    
    # 处理搜索结果
    if [[ ${#unique_patches[@]} -eq 0 ]]; then
        echo "未找到补丁: $patch_name" >&2
        return 1
    elif [[ ${#unique_patches[@]} -eq 1 ]]; then
        echo "${unique_patches[0]}"
        return 0
    else
        echo "找到多个匹配的补丁:" >&2
        for i in "${!unique_patches[@]}"; do
            echo "  $((i+1))) ${unique_patches[i]}" >&2
        done
        echo -n "请选择 (1-${#unique_patches[@]}): " >&2
        read -r choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [[ "$choice" -ge 1 ]] && [[ "$choice" -le "${#unique_patches[@]}" ]]; then
            echo "${unique_patches[$((choice-1))]}"
            return 0
        else
            echo "无效选择" >&2
            return 1
        fi
    fi
}

extract_subject_from_patch() {
    local patch_file="$1"
    awk '/^Subject: / {
        gsub(/^Subject: \[[^]]*\] /, "", $0);
        gsub(/^Subject: /, "", $0);
        subject_line = $0;
        getline;
        while (/^ /) {
            gsub(/^ /, "", $0);
            subject_line = subject_line " " $0;
            getline;
        }
        print subject_line;
        exit;
    }' "$patch_file"
}

apply_patch() {
    local patch="$1"
    
    # 基本检查
    [[ ! -f "$patch" ]] && { echo "补丁文件不存在: $patch"; return 1; }
    
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "不在Git仓库中"
        return 1
    fi
    
    echo "应用补丁: $(basename "$patch")"
    
    # 移除补丁将创建的文件（如果存在）
    lsdiff -s --strip=1 "$patch" 2>/dev/null | grep '^+' | awk '{print $2}' | xargs -r rm -f
    
    # 应用补丁
    if ! patch --batch --silent -p1 -N < "$patch"; then
        display_alert "补丁应用失败: $(basename "$patch")" "failed" "wrn"
        return 1
    fi
    
    display_alert "补丁应用成功: $(basename "$patch")" "success" "info"
    
    # 添加文件到Git
    lsdiff -s --strip=1 "$patch" 2>/dev/null | grep '^+' | awk '{print $2}' | xargs -r git add
    git add -u
    
    # 提交
    local commit_msg
    commit_msg=$(extract_subject_from_patch "$patch")
    
    if [[ -n "$commit_msg" ]] && ! git diff --cached --quiet; then
        git commit -m "$commit_msg"
        display_alert "已提交: $commit_msg" "committed" "info"
    fi
}

show_help() {
    cat << EOF
智能补丁应用工具

用法: $0 [选项] <补丁文件名>

选项:
  -u, --uboot     仅在U-Boot补丁目录中搜索
  -k, --kernel    仅在内核补丁目录中搜索
  -l, --list      列出所有可用补丁
      --list-uboot    列出U-Boot补丁
      --list-kernel   列出内核补丁
  -h, --help      显示帮助

示例:
  $0 my-fix.patch         # 在所有目录中搜索并应用补丁
  $0 -u uboot-fix.patch   # 仅在U-Boot目录中搜索补丁
  $0 -k kernel-fix        # 仅在内核目录中搜索补丁（自动添加扩展名）
  $0 --list               # 列出所有补丁
  $0 --list-uboot         # 仅列出U-Boot补丁
  $0 --list-kernel        # 仅列出内核补丁

环境变量:
  ARMBIAN_BASE        Armbian根目录 (默认: $HOME/armbian-build)

搜索目录:
  默认搜索: 
    - $ARMBIAN_BASE/patch/misc
    - $ARMBIAN_BASE/patch/kernel/meson64-current
    - $ARMBIAN_BASE/patch/kernel/meson64-edge
    - $ARMBIAN_BASE/patch/u-boot/u-boot-meson64
  
  U-Boot搜索(-u):
    - $ARMBIAN_BASE/patch/u-boot/u-boot-meson64
  
  内核搜索(-k):
    - $ARMBIAN_BASE/patch/kernel/meson64-current
    - $ARMBIAN_BASE/patch/kernel/meson64-edge
EOF
}

list_patches() {
    local search_dirs=("${@}")
    local title="$1"
    
    # 如果没有指定搜索目录，使用默认目录
    if [[ ${#search_dirs[@]} -eq 0 ]]; then
        search_dirs=("${PATCH_DIRS[@]}")
        title="所有可用的补丁文件"
    fi
    
    echo "$title:"
    for dir in "${search_dirs[@]}"; do
        if [[ -d "$dir" ]]; then
            echo
            echo "📁 $dir:"
            find -L "$dir" -maxdepth 1 \( -name "*.patch" -o -name "*.diff" \) | sort | sed 's|.*/|  |'
        fi
    done
}

# 解析命令行参数
SEARCH_TYPE="default"
PATCH_NAME=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -u|--uboot)
            SEARCH_TYPE="uboot"
            shift
            ;;
        -k|--kernel)
            SEARCH_TYPE="kernel"
            shift
            ;;
        -l|--list)
            list_patches
            exit 0
            ;;
        --list-uboot)
            list_patches "${UBOOT_PATCH_DIRS[@]}" "U-Boot补丁文件"
            exit 0
            ;;
        --list-kernel)
            list_patches "${KERNEL_PATCH_DIRS[@]}" "内核补丁文件"
            exit 0
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        -*)
            echo "未知选项: $1"
            show_help
            exit 1
            ;;
        *)
            PATCH_NAME="$1"
            shift
            ;;
    esac
done

# 主逻辑
if [[ -z "$PATCH_NAME" ]]; then
    show_help
    exit 0
fi

# 根据搜索类型确定搜索目录
case "$SEARCH_TYPE" in
    "uboot")
        display_alert "在U-Boot目录中搜索补丁: $PATCH_NAME" "search" "info"
        patch_file=$(find_patch "$PATCH_NAME" "${UBOOT_PATCH_DIRS[@]}")
        ;;
    "kernel")
        display_alert "在内核目录中搜索补丁: $PATCH_NAME" "search" "info"
        patch_file=$(find_patch "$PATCH_NAME" "${KERNEL_PATCH_DIRS[@]}")
        ;;
    *)
        display_alert "在所有目录中搜索补丁: $PATCH_NAME" "search" "info"
        patch_file=$(find_patch "$PATCH_NAME")
        ;;
esac

if [[ $? -eq 0 ]] && [[ -n "$patch_file" ]]; then
    apply_patch "$patch_file"
else
    echo "找不到补丁文件，尝试以下命令查看可用补丁:"
    echo "  $0 --list        # 查看所有补丁"
    echo "  $0 --list-uboot  # 查看U-Boot补丁"
    echo "  $0 --list-kernel # 查看内核补丁"
    exit 1
fi

