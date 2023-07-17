BIN="tpipe"
DD_IN=$BIN
DD_OUT="/home/mice/learn/others/bochs/body/hd60M.img"
SEC_CNT=$(ls -l $BIN | awk '{printf("%d",($5+511)/512)}')
dd if=./$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=300 conv=notrunc

BIN="tpipe.c"
DD_IN=$BIN
DD_OUT="/home/mice/learn/others/bochs/body/hd60M.img"
SEC_CNT=$(ls -l $BIN | awk '{printf("%d",($5+511)/512)}')
dd if=./$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=500 conv=notrunc