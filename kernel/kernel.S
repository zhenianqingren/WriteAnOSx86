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
VECTOR 0x30 ,ZERO
VECTOR 0x31 ,ZERO
VECTOR 0x32 ,ZERO
VECTOR 0x33 ,ZERO
VECTOR 0x34 ,ZERO
VECTOR 0x35 ,ZERO
VECTOR 0x36 ,ZERO
VECTOR 0x37 ,ZERO
VECTOR 0x38 ,ZERO
VECTOR 0x39 ,ZERO
VECTOR 0x3A ,ZERO
VECTOR 0x3B ,ZERO
VECTOR 0x3C ,ZERO
VECTOR 0x3D ,ZERO
VECTOR 0x3E ,ZERO
VECTOR 0x3F ,ZERO
VECTOR 0x40 ,ZERO
VECTOR 0x41 ,ZERO
VECTOR 0x42 ,ZERO
VECTOR 0x43 ,ZERO
VECTOR 0x44 ,ZERO
VECTOR 0x45 ,ZERO
VECTOR 0x46 ,ZERO
VECTOR 0x47 ,ZERO
VECTOR 0x48 ,ZERO
VECTOR 0x49 ,ZERO
VECTOR 0x4A ,ZERO
VECTOR 0x4B ,ZERO
VECTOR 0x4C ,ZERO
VECTOR 0x4D ,ZERO
VECTOR 0x4E ,ZERO
VECTOR 0x4F ,ZERO
VECTOR 0x50 ,ZERO
VECTOR 0x51 ,ZERO
VECTOR 0x52 ,ZERO
VECTOR 0x53 ,ZERO
VECTOR 0x54 ,ZERO
VECTOR 0x55 ,ZERO
VECTOR 0x56 ,ZERO
VECTOR 0x57 ,ZERO
VECTOR 0x58 ,ZERO
VECTOR 0x59 ,ZERO
VECTOR 0x5A ,ZERO
VECTOR 0x5B ,ZERO
VECTOR 0x5C ,ZERO
VECTOR 0x5D ,ZERO
VECTOR 0x5E ,ZERO
VECTOR 0x5F ,ZERO
VECTOR 0x61 ,ZERO
VECTOR 0x62 ,ZERO
VECTOR 0x63 ,ZERO
VECTOR 0x64 ,ZERO
VECTOR 0x65 ,ZERO
VECTOR 0x66 ,ZERO
VECTOR 0x67 ,ZERO
VECTOR 0x68 ,ZERO
VECTOR 0x69 ,ZERO
VECTOR 0x6A ,ZERO
VECTOR 0x6B ,ZERO
VECTOR 0x6C ,ZERO
VECTOR 0x6D ,ZERO
VECTOR 0x6E ,ZERO
VECTOR 0x6F ,ZERO
VECTOR 0x70 ,ZERO
VECTOR 0x71 ,ZERO
VECTOR 0x72 ,ZERO
VECTOR 0x73 ,ZERO
VECTOR 0x74 ,ZERO
VECTOR 0x75 ,ZERO
VECTOR 0x76 ,ZERO
VECTOR 0x77 ,ZERO
VECTOR 0x78 ,ZERO
VECTOR 0x79 ,ZERO
VECTOR 0x7A ,ZERO
VECTOR 0x7B ,ZERO
VECTOR 0x7C ,ZERO
VECTOR 0x7D ,ZERO
VECTOR 0x7E ,ZERO
VECTOR 0x7F ,ZERO
VECTOR 0x80 ,ZERO

[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
;保存上下文环境
    push 0
    push ds
    push es
    push fs
    push gs
    pushad

    push 0x80;保持统一的内核栈格式
;为系统调用子功能传递参数
    push edx
    push ecx
    push ebx
;调用子功能处理函数
    call [syscall_table+eax*4]
    add esp,12
;将call调用后的返回值存入当前内核栈中eax的位置，之后恢复上下文环境时弹出
    mov [esp+8*4],eax
    jmp intr_exit