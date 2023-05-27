;主引导程序
;----------------------------------------------------------------
%include "boot.inc"
SECTION MBR vstart=0x7c00
    mov ax,cs
    mov bx,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00
    mov ax,0xb800;用于文本模式显示适配器显存起始位置0xb800
    mov gs,ax


;清屏
;利用0x06号功能，上卷全部行
;----------------------------------------------------------------
;int 0x10 功能号: 0x06

    mov ax,0x06
    mov bx,0x0700
    mov cx,0
    mov dx,0x184f
    int 0x10

;输出字符串:1 MBR
    mov byte [gs:0x00],'1'
    mov byte [gs:0x01],0xA4

    mov byte [gs:0x02],' '
    mov byte [gs:0x03],0xA4

    mov byte [gs:0x04],'M'
    mov byte [gs:0x05],0xA4

    mov byte [gs:0x06],'B'
    mov byte [gs:0x07],0xA4

    mov byte [gs:0x08],'R'
    mov byte [gs:0x09],0xA4

    mov eax,LOADER_START_SECTION    ;起始扇区lba地址
    mov bx,LOADER_BASE_ADDR         ;写入的地址
    mov cx,1                        ;待读入的扇区数
    call rd_disk_m_16               ;读取程序的起始部分
    jmp LOADER_BASE_ADDR            ;执行loader

;读取硬盘n个扇区: rd_disk_m_16

rd_disk_m_16:
    mov esi,eax
    mov di,cx
    ;数据备份

;设置要读取的扇区数
    mov dx,0x1f2    ;目的端口设置
    mov al,cl       ;向端口写入的数据
    out dx,al

    mov eax,esi;恢复ax

;将LBA地址存入low mid high以及device之中
    mov dx,0x1f3
    out dx,al
    ;low

    mov dx,0x1f4
    mov cl,8
    shr eax,cl
    out dx,al
    ;mid

    mov dx,0x1f5
    shr eax,cl
    out dx,al
    ;high

    mov dx,0x1f6
    shr eax,cl
    and al,0x0f
    or  al,0xe0
    out dx,al
    ;device

;向command端口写入读命令
    mov dx,0x1f7
    mov al,0x20
    out dx,al

;检测硬盘状态

.not_ready:
    nop
    in al,dx
    and al,0x88
    cmp al,0x08
    jne .not_ready

;从data端口读数据,首先算出要读多少次并将结果保存在cx寄存器之中
    mov ax,di
    mov dx,256
    mul dx
    mov cx,ax

;开始读
    mov dx,0x1f0
.go_on_head:
    in ax,dx
    mov [bx],ax
    add bx,2
    loop .go_on_head
    ret

    times 510-($-$$) db 0
    db 0x55,0xaa
