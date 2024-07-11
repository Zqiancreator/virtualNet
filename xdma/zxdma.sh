if [ -f "Makefile1" ]; then
    mv "Makefile" "Makefile2"
    mv "Makefile1" "Makefile"
fi

make

if [ -f "xdma.ko" ]; then
    sudo cp xdma.ko /lib/modules/5.4.0-150-generic/xdma/
fi