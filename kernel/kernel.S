[bits 32]

%define ERR_CODE nop
%define ZERO push 0     
;统一栈中格式

extern put_str
extern idt_table

section .data
intr_str db "interrupt occur!",0x0a,0
global intr_entry_table
intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:
    %2
    ;保存现场
    push ds
    push es
    push fs
    push gs
    pushad;备份32 bit寄存器环境,压入所有双字长的寄存器,入栈顺序:EAX->ECX->EDX->EBX->ESP->EBP->ESI->EDI

    ;向主片和从片发送中断结束命令
    mov al,0x20
    out 0xa0,al
    out 0x20,al

    push %1 ;压入中断向量号
    call [idt_table+%1*4]
    jmp intr_exit

section .data
    dd intr%1entry  ;存储各个中断程序入口的地址
%endmacro           ;形成intr_entry_table数组

section .text
global intr_exit
intr_exit:
    add esp,4
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp,4
    iretd

VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO
VECTOR 0x08,ZERO
VECTOR 0x09,ZERO
VECTOR 0x0a,ZERO
VECTOR 0x0b,ZERO
VECTOR 0x0c,ZERO
VECTOR 0x0d,ZERO
VECTOR 0x0e,ZERO
VECTOR 0x0f,ZERO
VECTOR 0x10,ZERO
VECTOR 0x11,ZERO
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO
VECTOR 0x18,ZERO
VECTOR 0x19,ZERO
VECTOR 0x1a,ZERO
VECTOR 0x1b,ZERO
VECTOR 0x1c,ZERO
VECTOR 0x1d,ZERO
VECTOR 0x1e,ERR_CODE
VECTOR 0x1f,ZERO
VECTOR 0x20,ZERO                    ;时钟中断
VECTOR 0X21,ZERO					;键盘中断
VECTOR 0X22,ZERO					;级联
VECTOR 0X23,ZERO					;串口2
VECTOR 0X24,ZERO					;串口1
VECTOR 0X25,ZERO					;并口2
VECTOR 0X26,ZERO					;软盘
VECTOR 0X27,ZERO					;并口1
VECTOR 0X28,ZERO					;实时时钟
VECTOR 0X29,ZERO					;重定向
VECTOR 0X2A,ZERO					;保留
VECTOR 0x2B,ZERO					;保留
VECTOR 0x2C,ZERO					;ps/2 鼠标
VECTOR 0x2D,ZERO					;fpu 浮点单元异常
VECTOR 0x2E,ZERO					;硬盘
VECTOR 0x2F,ZERO					;保留