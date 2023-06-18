gcc -I lib/kernel -I kernel/ -I lib/ -I device -c -fno-builtin -o build/timer.o device/timer.c
gcc -I lib/kernel -I kernel/ -I lib/ -I device -c -fno-builtin -o build/main.o kernel/main.c
nasm -f elf -o build/print.o lib/kernel/print.S
nasm -f elf -o build/kernel.o kernel/kernel.S
gcc -I lib/kernel -I kernel/ -I lib/ -I device -c -fno-builtin -o build/interrupt.o kernel/interrupt.c
gcc -I lib/kernel -I kernel/ -I lib/ -I device -c -fno-builtin -o build/init.o kernel/init.c
ld -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o build/timer.o