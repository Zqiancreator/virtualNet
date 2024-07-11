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

# 对指定的行执行操作
process_line 2 remove  # 尝试删除第2行的注释
process_line 4 add     # 尝试给第4行添加注释
process_line 7 add     # 尝试给第7行添加注释

sudo rmmod nv_if_h2c 
make clean
make
sudo insmod nv_if_h2c.ko
# dmesg

# status_output=$(systemctl status NetworkManager | grep "Active:")
# 使用grep查找是否存在"running"状态
# echo "$status_output" | grep -q "running"
# if [ $? -eq 0 ]; then
#     echo "NetworkManager服务正在运行。"
#     systemctl stop NetworkManager
#     systemctl disable NetworkManager
#     systemctl restart network
# else
#     echo "NetworkManager服务不在运行状态。"
    
# fi
sudo ip addr add 192.168.23.12/24 dev mytun
# ip addr add 192.168.23.13/24 dev mytun
