[bits 32]
extern main
extern exit
section .text
global _start
_start:
    ;下面使用的要和execv中load之后指定的寄存器一致
    push ebx
    push ecx
    call main
    
    ;将main的返回值返回
    push eax
    call exit
    ;exit不会返回