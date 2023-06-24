[bits 32]
section .text
global switch_to
switch_to:
    ;栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+20];得到栈中参数cur
    mov [eax],esp;保存栈顶指针到task_struct的self_kstack字段

    ;--------以上是备份当前线程环境，下面恢复下一个线程环境--------
    mov eax,[esp+24]
    mov esp,[eax];它用来记录0级栈指针，被换上CPU时用来恢复0级栈
                 ;0级栈中保存了线程或进程的所有信息，包括3级栈指针
    pop ebp
    pop ebx
    pop edi
    pop esi
    ret
    ;如果不是通过中断进入，第一次返回时会返回到kernel_thread

