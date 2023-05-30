%include "boot.inc"
SECTION loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR 		   ;是个程序都需要有栈区 我设置的0x600以下的区域到0x500区域都是可用空间 况且也用不到

				       		   ;对汇编再复习 db define byte,dw define word,dd define dword
    GDT_BASE        : dd 0x00000000          		   ;刚开始的段选择子0不能使用 故用两个双字 来填充
   		       dd 0x00000000 
    
    CODE_DESC       : dd 0x0000FFFF         		   ;FFFF是与其他的几部分相连接 形成0XFFFFF段界限
    		       dd DESC_CODE_HIGH4
    
    DATA_STACK_DESC : dd 0x0000FFFF
  		       dd DESC_DATA_HIGH4
    		       
    VIDEO_DESC      : dd 0x80000007         		   ;0xB8000 到0xBFFFF为文字模式显示内存 B只能在boot.inc中出现定义了 此处不够空间了 8000刚好够
                                                       ;注意此处与之前的差别，开启保护模式后不会再左移4位了，因此段描述符中的是实际段地址0xb8000
                                                       ;cr0的PE位等于1后根据段选择子索引到这个地址
                        dd DESC_VIDEO_HIGH4     	   ;0x0007 (bFFFF-b8000)/4k = 0x7
                 
    GDT_SIZE              equ $ - GDT_BASE               ;当前位置减去GDT_BASE的地址 等于GDT的大小
    GDT_LIMIT       	   equ GDT_SIZE - 1   	           ;SIZE - 1即为最大偏移量
    
    times 60 dq 0                             	   ;预留60个 四字型 描述符

    SELECTOR_CODE        equ (0X0001<<3) + TI_GDT + RPL0    ;16位寄存器 4位TI RPL状态 GDT剩下的选择子
    SELECTOR_DATA	  equ (0X0002<<3) + TI_GDT + RPL0
    SELECTOR_VIDEO       equ (0X0003<<3) + TI_GDT + RPL0

    ;total_mem_bytes用于保存内存容量，以字节为单位
    ;当前偏移loader.bin文件头0x200bytes
    ;loader.bin的加载地址是0x900，因此total_mem_bytes内存中的地址是0xb00
    total_mem_bytes dd 0
    

    ;gdt指针 2字gdt界限放在前面 4字gdt地址放在后面 lgdt 48位格式 低位16位界限 高位32位起始地址
    gdt_ptr           dw GDT_LIMIT
    		       dd GDT_BASE
    
    ;人工对齐: total_mem_bytes4+gdt_ptr6+ards_buff244+ards_nr2 共256个字节
    ards_buff times 244 db 0
    ards_nr dw 0;记录ARDS结构体数量
    
loader_start:;偏移0x300
    
    ;int 0x15: eax=0xE820 , edx=0x534D4150 (SMAP) 获取内存布局

    xor ebx,ebx;ebx置为0
    mov edx,0x534d4150
    mov di,ards_buff

.e820_mem_get_loop:
    mov eax,0xe820;执行int 0x15后，每次eax都变为0x534D4150，因此重新赋值
    mov ecx,20
    int 0x15
    jc .e820_failed_so_try_e801;CF=1代表有错误发生，尝试0xe801

    add di,cx;使di指向缓冲区中新的位置
    inc word [ards_nr];读入的ards结构数量＋1
    cmp ebx,0;ebx=0并且cf=0 说明结束
    jnz .e820_mem_get_loop

    ;在所有ards结构中 找出(base_add_low + length_low)的最大值，即内存的容量
    mov cx,[ards_nr]
    ;遍历每一个结构体
    mov ebx,ards_buff
    xor edx,edx;edx为最大的内存容量，此处先清零

.find_max_mem_area:
    ;无需判断type是否为一，最大的内存块一定是可被使用的
    mov eax,[ebx];base_add_low
    add eax,[ebx+8]
    add ebx,20
    cmp edx,eax
    ;找出最大，edx寄存器始终是最大的内存容量
    jge .next_ards
    mov edx,eax
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok

;int 0x15 ax=0xe801获取内存大小，最大支持4GB
;返回值: ax=cx 以1KB为单位 bx=dx 以64KB为单位
;ax和cx为低15MB bx和dx为16MB到4GB

.e820_failed_so_try_e801:
    mov ax,0xe801
    int 0x15
    jc .e801_failed_so_try_88

;算出低15MB内存
    mov cx,0x400;单位1KB
    mul cx;res->dx:ax
    shl edx,16;最终结果全部保存在edx，把低16bit移至高16bit
    and eax,0x0000FFFF
    or edx,eax;取出ax的值放入edx低16bit
    add edx,0x10_0000;+1MB
    mov esi,edx;备份

;算16MB~4GB的内存
    xor eax,eax
    mov ax,bx
    mov ecx,0x10000;64KB
    mul ecx
    add esi,eax;加上之前的,eax有32bit，足够存放结果了，edx里面没有有效数据
    mov edx,esi;现在的edx存放总内存大小
    jmp .mem_get_ok

.e801_failed_so_try_88:
    mov ah,0x88
    int 0x15
    ; jc .error_hlt         wait here
    and eax,0x0000FFFF

    mov cx,0x400;粒度是1KB
    mul cx
    shl edx,16
    and eax,0x0000FFFF
    or edx,eax
    add edx,0x10_0000;加上最低的1MB

.mem_get_ok:
    mov [total_mem_bytes],edx

    jmp $;tmp

    ;;
    
; --------------------------------- 设置进入保护模式 -----------------------------
; 1 打开A20 gate
; 2 加载gdt
; 3 将cr0 的 pe位置1
    
    in al,0x92                 ;端口号0x92 中 第1位变成1即可
    or al,0000_0010b
    out 0x92,al
   
    lgdt [gdt_ptr] 
    
    mov eax,cr0                ;cr0寄存器第0位设置位1
    or  eax,0x00000001              
    mov cr0,eax
      
;-------------------------------- 已经打开保护模式 ---------------------------------------
    jmp dword SELECTOR_CODE:p_mode_start                       ;刷新流水线
 
 [bits 32]
 p_mode_start: 
    mov ax,SELECTOR_DATA
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov esp,LOADER_STACK_TOP
    mov ax,SELECTOR_VIDEO
    mov gs,ax
    
    mov byte [gs:160],'P'
    
    jmp $          
