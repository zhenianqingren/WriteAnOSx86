SECTION MBR vstart=0x7c00;不要与保护模式下的虚拟地址混淆
    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00

;功能号0x06 上卷行 清屏
;AH=0x06 AL=行数 如果为0表示上卷全部行
;BH=上卷行属性
;(Cl,CH)=窗口左上角(X,Y) (DL,DH)=窗口右下角(X,Y)
;无返回值

    mov ax,0x0600
    mov bx,0x700
    mov cx,0
    mov dx,0x184f;右下角(80,25) 下标从0开始 0x18=24 0x4f=79
    
    int 0x10

;获取光标位置
    mov ah,3;3号子功能
    mov bh,0;存储的是待获取光标的页号
    int 0x10

;打印字符串
    mov ax,message
    mov bp,ax;es:bp是串首地址,es已被初始化

;光标位置用到dx寄存器中的内容
    mov cx,5;串长度,不包括结束符
    mov ax,0x1301;子功能号13显示字符及属性放入ah al=01:显示字符串,光标跟随移动
    mov bx,0x2;bh中存放要显示的页号0 bl存放字符属性黑底绿字(bl=02h)

    int 0x10

    jmp $

    message db "1 MBR"
    times 510-($-$$) db 0;用0将本扇区剩余部位填充
    db 0x55,0xaa
