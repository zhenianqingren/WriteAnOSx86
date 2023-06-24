TI_GDT equ 0
RPL equ 0
SELECTOR_VIDEO equ (0x0003<<3)+TI_GDT+RPL

[bits 32]
section .data
put_int_buf dq 0

section .text

;把栈中的一个字符写在光标处
global put_char:
put_char:
    pushad                  ;备份32 bit寄存器环境,压入所有双字长的寄存器,入栈顺序:EAX->ECX->EDX->EBX->ESP->EBP->ESI->EDI
    mov ax,SELECTOR_VIDEO
    mov gs,ax
    ;保证gs中是正确的视频段选择子

;获取当前光标位置,是一维的线性地址
    mov dx,0x03d4   ;索引寄存器
    mov al,0x0e     ;提供光标位置的高8bit
    out dx,al       
    mov dx,0x03d5
    in al,dx        ;得到光标位置高8bit
    mov ah,al

    mov dx,0x03d4   ;索引寄存器
    mov al,0x0f     ;提供光标位置的低8bit
    out dx,al       
    mov dx,0x03d5
    in al,dx        ;得到光标位置低8bit

    ;光标存入bx
    mov bx,ax
    ;在栈中获取待打印的字符
    mov ecx,[esp+36]
    ;pushad压入4*8=32bytes+返回地址4bytes
    cmp cl,0xd  ;0x0d->CR 0x0a->LF
    jz .is_carriage_return
    cmp cl,0xa
    jz .is_line_feed

    cmp cl,0x8
    jz .is_backspace
    jmp .put_other

.is_backspace:
    ;80*25,一页可容纳2000个字符,故光标寄存器索引值的范围是0~1999,由于一个字符占据两个字节,真实地址再乘2
    dec bx
    shl bx,1

    mov byte [gs:bx],0x20   ;删除实质上是在光标处输入新字符覆盖之前的字符,但为了达到平常的效果,用空格覆盖
    inc bx
    mov byte [gs:bx],0x07   ;前景色和背景色
    shr bx,1                ;恢复索引
    jmp .set_cursor

.put_other:
    shl bx,1
    mov [gs:bx],cl
    inc bx
    mov byte [gs:bx],0x07
    shr bx,1
    inc bx  ;下一个光标值
    cmp bx,2000
    jl .set_cursor

.is_line_feed:          ;LF 新行
.is_carriage_return:    ;CR 移至行首
    xor dx,dx
    mov ax,bx   ;dx:ax->被除数 商->AX 余数->DX
    mov si,80
    
    div si
    sub bx,dx   ;退回行首

.is_carriage_return_end:
    add bx,80   ;移至下一行
    cmp bx,2000 
.is_line_feed_end:
    jl .set_cursor

.roll_screen:
    ;滚屏操作,借助上方的回车换行实现,调整光标
    ;1~24覆盖0~23,新行覆盖24
    cld
    mov ecx,960 ;2000-80=1920,一次搬4bytes,共计1920*2/4=960次

    mov esi,0xc00b80a0  ;第1行行首
    mov edi,0xc00b8000  ;第0行行首
    rep movsd

;最后一行填充空白
    mov ebx,3840    ;1920<<1=3840,最后一行第一个字符的索引
    mov ecx,80      ;填充80次

.cls:
    mov word [gs:ebx],0x0720
    add ebx,2
    loop .cls
    mov ebx,1920    ;重置光标值

.set_cursor:
    ;put_char后移动光标至bx
    mov dx,0x03d4
    mov al,0x0e
    out dx,al

    mov dx,0x03d5
    mov al,bh
    out dx,al
    ;光标高8bit

    mov dx,0x03d4
    mov al,0x0f
    out dx,al

    mov dx,0x03d5
    mov al,bl
    out dx,al
    ;光标低8bit

.put_char_done:
    popad
    ret

global set_cursor:
set_cursor:
    pushad
    ;在栈中获取光标位置
    mov bx,[esp+36]
    ;put_char后移动光标至bx
    mov dx,0x03d4
    mov al,0x0e
    out dx,al

    mov dx,0x03d5
    mov al,bh
    out dx,al
    ;光标高8bit

    mov dx,0x03d4
    mov al,0x0f
    out dx,al

    mov dx,0x03d5
    mov al,bl
    out dx,al
    ;光标低8bit
    popad
    ret

global put_str:
put_str:
    pushad
    xor ecx,ecx
    mov ebx,[esp+36]

.ploop:
    mov cl,[ebx]
    cmp cl,0
    jz .pend
    push ecx
    call put_char
    add esp,4
    inc ebx
    jmp .ploop
.pend:
    popad
    ret

global put_int:
put_int:
    pushad
    mov ebp,esp
    mov eax,[ebp+36]
    mov edx,eax
    mov edi,7       ;指定初始偏移量
    mov ecx,8       ;16进制数字为8个
    mov ebx,put_int_buf

.16based_4bits:
    and edx,0xf
    cmp edx,9
    jg .isA2F
    add edx,'0'
    jmp .store

.isA2F:
    sub edx,10
    add edx,'A'

.store:
    mov byte [ebx+edi],dl
    dec edi
    shr eax,4
    mov edx,eax
    loop .16based_4bits

.ready_to_print:
    inc edi
.skip_prefix_0:
    cmp edi,8
    je .full0

.go_on_skip:
    mov cl,[put_int_buf+edi]
    inc edi
    cmp cl,'0'
    je .skip_prefix_0
    dec edi
    jmp .put_each_num

.full0:
    mov cl,'0'

.put_each_num:
    push ecx
    call put_char
    add esp,4
    inc edi
    mov cl,[put_int_buf+edi]
    cmp edi,8
    jl .put_each_num
    popad
    ret