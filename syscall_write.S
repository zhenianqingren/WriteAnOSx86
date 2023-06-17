section .data
    str_c_lib: db "c library says hello world",0xa,0xa;换行符'\n'
    str_c_lib_len equ $-str_c_lib

    str_syscall: db "syscall says hello world",0xa
    str_syscall_len: db $-str_syscall

section .text
global _start

_start:
    ;模拟C语言调用库函数
    push str_c_lib_len
    push str_c_lib
    push 1
    call simu_write
    add esp,12

    ;直接系统调用
    mov eax,4
    mov ebx,1
    mov ecx,str_syscall
    mov edx,str_syscall_len
    int 0x80

simu_write:
    push ebp
    mov ebp,esp
    mov eax,4;4号子功能write
    mov ebx,[ebp+8]     ;第一个参数
    mov ecx,[ebp+12]    ;第二个参数
    mov edx,[ebp+16]    ;第三个参数
    int 0x80
    pop ebp
    ret