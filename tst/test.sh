#!/bin/bash

#动态链接器目录前缀$QEMU_LD_PREFIX/lib/ld-linux-aarch64.so.1
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu
#查找libstd.so的目录
export LD_LIBRARY_PATH=$PWD

# 兼容bash, dash, zsh, busybox处理
SH=$(basename `readlink /proc/$$/exe`)
if [ $SH != 'dash' ]; then
    shopt -s expand_aliases 2>/dev/null
    alias echo='echo -e'
fi
alias ax='../build/compiler -S -o /dev/stdout'
# 编译后可执行文件临时目录
TMPD=/tmp/tst
if [ $SH != 'busybox' ]; then
    alias diff='diff -w -b --color=auto'
else
    alias diff='diff -w -b'
fi

mkdir $TMPD

ls *.c | (
    while read NAME;
    do
        NAME=`basename $NAME .c`
        echo -n $NAME
        INPUT=/dev/null
        # 生成汇编指令并编译为可执行文件（采用动态链接）
        # 使用aarch64-linux-gcc需要不同的指令
        ax $NAME.c | clang-18 -xassembler-with-cpp /dev/stdin -target aarch64-linux-gnueabihf -march=armv8a -fuse-ld=lld-19 -lstd -L./ -o $TMPD/$NAME
        if [ $? -ne 0 ]; then
            echo ' \033[31mCE\033[0m'
            continue
        elif [ -f $NAME.in ]; then
            INPUT=$NAME.in
        fi
        (qemu-aarch64-static $TMPD/$NAME < $INPUT; echo "\n$?")\
            | awk NF \
            | diff $NAME.out /dev/stdin
        echo ' \e[32mOK\e[0m'
    done
)
