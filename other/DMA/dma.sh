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
source /opt/petalinux/2020.1/environment-setup-aarch64-xilinx-linux 
make clean
make
file="dmatest.ko"
scpFile "$file"