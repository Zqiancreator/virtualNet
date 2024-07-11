if [ -f "Makefile2" ]; then
    mv "Makefile" "Makefile1"
    mv "Makefile2" "Makefile"
fi

sudo rmmod  zh2c
make
sudo insmod zh2c.ko
sudo ip addr add 192.168.24.12/24 brd 192.168.24.255 dev veth_net0
sudo ip route add 192.168.24.13 via 192.168.24.12    # 添加静态路由
sudo ip neigh add 192.168.24.13 lladdr 02:11:22:33:44:56 dev veth_net0 #arp静态配置
sudo sysctl -w net.ipv6.conf.all.disable_ipv6=1       # 禁用ipv6
sudo sysctl -w net.ipv6.conf.default.disable_ipv6=1