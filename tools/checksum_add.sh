#!/bin/sh
if [ $# -ne 1 ]; then
  echo "usage: $0 <file>"
  exit 1
fi 

if [ ! -f "$1" ]; then
    echo "error: file $1 doesn't exists"
    exit 1
fi

size=$(stat -c%s $1)

if [ $size -ne 2592 ]; then
  echo "error: wrong file size, is $size but should be 2592"
  exit 1
fi

#crc32=$(dd if=$1 bs=1 count=$((0xa00)) 2>/dev/null | gzip -1 -c | tail -c8 | hexdump -n4 -e '"%0.8x\n"' | tac)
 
#echo "crc32: $crc32"
   
dd if=$1 bs=1 count=$((0xa00)) 2>/dev/null | gzip -1 -c | tail -c8 | hexdump -n4 -e '"%0.8x\n"' | tac | xxd -p -r | dd of=$1 conv=notrunc obs=1 seek=$((0xa00)) count=4 bs=1



echo "success, done!"
