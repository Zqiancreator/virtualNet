FILENAME=Makefile
process_line() {
    local lineno=$1
    local op=$2
    
    if [ "$op" = "add" ]; then
        # 如果行不以#开头，添加注释
        if ! sed -n "${lineno}p" "$FILENAME" | grep -q "^#"; then
            sed -i "${lineno}s/^/#/" "$FILENAME"
        fi
    elif [ "$op" = "remove" ]; then
        # 如果行以#开头，删除注释
        if sed -n "${lineno}p" "$FILENAME" | grep -q "^#"; then
            sed -i "${lineno}s/^#//g" "$FILENAME"
        fi
    fi
}
scpFile(){
    local dir="/media/npc/05fef276-2245-4018-81e0-54ae00d93705/"
    local file=$1
    # 检查文件和目录是否存在
    if [ -f "$file" ] && [ -d "$dir" ]; then
        echo "$file exists."
        sudo scp "$file" "$dir"
    else
        echo "$file does not exist or $dir is not a directory."
    fi
}
# 对指定的行执行操作
process_line 1 add     
process_line 2 remove 
process_line 3 remove
process_line 4 add     
process_line 6 remove 
process_line 9 remove 

source /opt/petalinux/2020.1/environment-setup-aarch64-xilinx-linux 
make clean
make
file="nv_if_c2h.ko"
scpFile "$file"
file="irq-notify.ko"
scpFile "$file"