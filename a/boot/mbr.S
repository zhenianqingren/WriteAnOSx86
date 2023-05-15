SECTION MBR vstart=0x7c00
    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00
    mov ax,0xb800;用于文本模式显示适配器显存起始位置0xb800
    mov gs,ax

;清屏
;利用0x06号功能，上卷全部行进行清屏
;输入
;AH 功能号=0x06
;AL = 上卷的行数
;BH = 上卷行属性
;(CL,CH) = 窗口左上角(X,Y)
;(DL,DH) = 窗口右下角(X,Y)

    mov ax,0x0600
    mov bx,0x0700
    mov cx,0
    mov dx,0x184f;0x18=24 0x4f=79 第25行第80列
    int 0x10

;输出背景色绿色，前景色红色，并且跳动的字符串 "1 MBR"

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

    jmp $;死循环
    times 510-($-$$) db 0
    db 0x55,0xaa