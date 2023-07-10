BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ -I device/  -I thread/ -I userprog/ -I fs/
ASFLAGS = -f elf
CFLAGS = -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS = -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
      $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
      $(BUILD_DIR)/debug.o $(BUILD_DIR)/string.o $(BUILD_DIR)/memory.o	$(BUILD_DIR)/bitmap.o \
	  $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o $(BUILD_DIR)/switch.o $(BUILD_DIR)/sync.o	\
	  $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o \
	  $(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall_init.o $(BUILD_DIR)/stdio.o \
	  $(BUILD_DIR)/kio.o $(BUILD_DIR)/ide.o $(BUILD_DIR)/fs.o $(BUILD_DIR)/dir.o $(BUILD_DIR)/inode.o $(BUILD_DIR)/file.o
 
HEADERS = device/console.h device/ioqueue.h device/keyboard.h device/timer.h \
		kernel/debug.h kernel/interrupt.h kernel/global.h kernel/init.h kernel/memory.h \
		thread/sync.h thread/thread.h userprog/tss.h lib/kernel/print.h lib/kernel/bitmap.h \
		lib/kernel/list.h lib/kernel/io.h lib/stdint.h lib/string.h lib/user/syscall.h 		\
		userprog/syscall_init.h userprog/process.h fs/fs.h fs/dir.h fs/inode.h fs/super_block.h

$(BUILD_DIR)/main.o: kernel/main.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: thread/sync.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/console.o: device/console.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@	

$(BUILD_DIR)/debug.o: kernel/debug.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ioqueue.o: device/ioqueue.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/tss.o: userprog/tss.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: userprog/process.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/kio.o: lib/kernel/kio.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ide.o: device/ide.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@		

$(BUILD_DIR)/syscall_init.o: userprog/syscall_init.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/keyboard.o: device/keyboard.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@
##############    汇编代码编译    ###############
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/fs.o: fs/fs.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/dir.o: fs/dir.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/inode.o: fs/inode.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/file.o: fs/file.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: kernel/switch.S
	$(AS) $(ASFLAGS) $< -o $@
##############    链接所有目标文件    #############
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

.PHONY : mk_dir hd clean all

mk_dir:
	if [ ! -d $(BUILD_DIR) ]; then mkdir $(BUILD_DIR); fi

hd:
	dd if=build/kernel.bin \
           of=/home/mice/learn/others/bochs/body/hd60M.img \
           bs=512 count=200 seek=9 conv=notrunc

clean:
	cd $(BUILD_DIR) && rm -f  ./*

build: $(BUILD_DIR)/kernel.bin

all: mk_dir build hd
