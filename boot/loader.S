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
                      dd DESC_VIDEO_HIGH4              ;注意此处与之前的差别，开启保护模式后不会再左移4位了，因此段描述符中的是实际段地址0xb8000
                                                       ;cr0的PE位等于1后根据段选择子索引到这个地址
                                                 	   ;0x0007 (bFFFF-b8000)/4k = 0x7
                 
    GDT_SIZE              equ $ - GDT_BASE               ;当前位置减去GDT_BASE的地址 等于GDT的大小
    GDT_LIMIT       	  equ GDT_SIZE - 1   	         ;SIZE - 1即为最大偏移量
    
    times 60 dq 0                             	         ;预留60个 四字型 描述符

    SELECTOR_CODE        equ (0X0001<<3) + TI_GDT + RPL0    ;16位寄存器 4位TI RPL状态 GDT剩下的选择子
    SELECTOR_DATA	     equ (0X0002<<3) + TI_GDT + RPL0
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

    ; jmp $;tmp

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

;-----------------------加载kernel-----------------------------
    mov eax,KERNEL_START_SECTOR
    mov ebx,KERNEL_BIN_BASE_ADDR
    ;从磁盘读出后写入到ebx指定地址
    mov ecx,200;读入的扇区数，使其足够大，方便扩展
    call .rd_disk_m_32
    ;读入到内存

;--------------------------------------------------------------
;创建页目录及页表并初始化内存位图
    call setup_page
    ;将描述符表地址及偏移量写入内存gdt_ptr，一会用新地址重新加载
    sgdt [gdt_ptr]
    ;将gdt段描述符中的视频段描述符的段基址+0xc0000000，这种与硬件相关的一定要在内核地址空间，不能被用户程序随意访问
    mov ebx,[gdt_ptr+2];ebx<-GDT_BASE
    or dword [ebx+0x18+4],0xc000_0000
    ;将gdt的基址加上0xc0000000使其成为高地址
    add dword [gdt_ptr+2],0xc000_0000
    ;栈指针同样映射到内核地址
    add esp,0xc000_0000
    ;页目录地址赋给cr3
    mov eax,PAGE_DIR_TABLE_POS
    mov cr3,eax
    ;打开cr0的pg位
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax
    ;开启分页机制后，用gdt新的地址重新加载
    lgdt [gdt_ptr]

;以防万一，重新刷新流水线
    jmp dword SELECTOR_CODE:enter_kernel

.rd_disk_m_32:
;------------------------------------------------------------------------
;1 写入待操作磁盘数
;2 写入LBA 低24位寄存器 确认扇区
;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
;4 command 写指令
;5 读取status状态寄存器 判断是否完成工作
;6 完成工作 取出数据
 
 ;;;;;;;;;;;;;;;;;;;;;
 ;1 写入待操作磁盘数
 ;;;;;;;;;;;;;;;;;;;;;
    mov esi,eax   ; !!! 备份eax
    mov edi,ecx     ; !!! 备份ecx
    
    mov dx,0x1F2  ; 0x1F2为Sector Count 端口号 送到dx寄存器中
    mov al,cl     ; 只能由ax al传递数据
    out dx,al     ; 
    
    mov eax,esi   ; 前面需要ax来传递数据
    
;;;;;;;;;;;;;;;;;;;;;
;2 写入LBA 24位寄存器 确认扇区
;;;;;;;;;;;;;;;;;;;;;
    mov cl,0x8    ; shr 右移8位 把24位给送到 LBA low mid high 寄存器中

    mov dx,0x1F3  ; LBA low
    out dx,al
    
    mov dx,0x1F4  ; LBA mid
    shr eax,cl    ; eax为32位 ax为16位 eax的低位字节 右移8位即8~15
    out dx,al
    
    mov dx,0x1F5
    shr eax,cl
    out dx,al
    
;;;;;;;;;;;;;;;;;;;;;
;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
;;;;;;;;;;;;;;;;;;;;;

    		 
    		  ; 24 25 26 27位 尽管我们知道ax只有2 但还是需要按规矩办事 
    		  ; 把除了最后四位的其他位置设置成0
    shr eax,cl
    
    and al,0x0f 
    or al,0xe0   ;!!! 把第四-七位设置成0111 转换为LBA模式
    mov dx,0x1F6 ; 参照硬盘控制器端口表 Device 
    out dx,al

;;;;;;;;;;;;;;;;;;;;;
;4 向Command写操作 Status和Command一个寄存器
;;;;;;;;;;;;;;;;;;;;;

    mov dx,0x1F7 ; Status寄存器端口号
    mov ax,0x20  ; 0x20是读命令
    out dx,al
    
;;;;;;;;;;;;;;;;;;;;;
;5 向Status查看是否准备好惹 
;;;;;;;;;;;;;;;;;;;;;
    
		   ;设置不断读取重复 如果不为1则一直循环
.not_ready:     
    nop           ; !!! 空跳转指令 在循环中达到延时目的
    in al,dx      ; 把寄存器中的信息返还出来
    and al,0x88   ; !!! 0100 0100 0x88
    cmp al,0x08
    jne .not_ready ; !!! jump not equal == 0
    
;;;;;;;;;;;;;;;;;;;;;
;6 读取数据
;;;;;;;;;;;;;;;;;;;;;

    mov ax,di      ;把 di 储存的cx 取出来
    mov dx,256
    mul dx        ;与di 与 ax 做乘法 计算一共需要读多少次 方便作循环 低16位放ax 高16位放dx
    mov cx,ax      ;loop 与 cx相匹配 cx-- 当cx == 0即跳出循环
    mov dx,0x1F0
.go_read_loop:
    in ax,dx      ;两字节dx 一次读两字
    mov [ebx],ax
    add ebx,2
    loop .go_read_loop
    
    ret ;与call 配对返回原来的位置 跳转到call下一条指令

enter_kernel:
    call kernel_init
    mov esp,0xc009_f000;设置一个新的栈值确保不会盖过内核
    jmp KERNEL_ENTRY_POINT

;-------------------------初始化内核------------------------------------
kernel_init:
    ;loader位于0x900,最终大小不会超过2000bytes,因此选定0x900+2000=0x10d0,再取整到0x1500用于放置内核映像入口地址
    ;由于已经开启了分页,想要访问到低1MB空间,再加0xc000_0000,最终线性地址0xc000_1500
    xor eax,eax
    xor ebx,ebx;记录程序头表地址
    xor ecx,ecx;记录程序头表中program header数量
    xor edx,edx;记录program header尺寸

    mov dx,[KERNEL_BIN_BASE_ADDR+42];program header table entry size
    mov ebx,[KERNEL_BIN_BASE_ADDR+28];Program header table file offset
    add ebx,KERNEL_BIN_BASE_ADDR
    mov cx,[KERNEL_BIN_BASE_ADDR+44];Program header table entry count

.each_segment:
    cmp byte [ebx+0],PT_NULL;如果program header未使用
    je .PT_NULL

    push dword [ebx+16];Segment size in file parameter: size

    mov eax,[ebx+4]    ;Segment file offset
    add eax,KERNEL_BIN_BASE_ADDR
    push eax;parameter: src

    push dword [ebx+8];p_vaddr parameter:dst

    ;mem_cpy(dst,src,size)
    call mem_cpy
    add esp,12;清理三个参数

.PT_NULL:
    add ebx,edx
    loop .each_segment
    ret

;size
;src
;dst
;return address
;ebp            <---ebp
;ecx            <---esp
mem_cpy:
    cld;clean direction 每执行一次movsb，esi和edi自动加1，与std相反
    push ebp
    mov ebp,esp
    push ecx

    mov edi,[ebp+8];dst
    mov esi,[ebp+12];src
    mov ecx,[ebp+16];size
    rep movsb       ;movsb: move one byte es:edi<-ds:esi
                    ;rep: repeat do ... --ecx until ecx==0
    pop ecx
    pop ebp
    ret
;------------------创建页目录及页表------------------
setup_page:
;先把页目录占用的字节空间清0
    mov ecx,4096
    mov esi,0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS+esi],0
    inc esi
    loop .clear_page_dir

;开始创建页目录项PDE
;二级页表划分，高10bit索引页目录项，中间10bit索引页表项，低12bit组成最终物理地址页内偏移
;页目录项和页表项4byte对齐，因此它们的物理地址由索引左移两位
;创建任何表项实际上就是创建对虚拟地址到物理地址的映射
;首先创建对内核的映射，第一部分是规定了3GB~4GB的地址空间为内核空间，而我们的内核大小不会超过1MB，被装载到低1MB，因此只需映射0xc0000000~0xc00fffff到物理地址0x0~0xfffff
;由于loader此时还运行在低1MB的空间并且已经开启了分段机制，因此要保证它的虚拟地址就是物理地址，即映射0x0~0xfffff到物理地址0x0~0xfffff
;第一个页表负责映射内核地址
.create_pde:
    mov eax,PAGE_DIR_TABLE_POS
    add eax,0x1000;此时eax是第一个页表的物理地址
    mov ebx,eax

;每一个页表可映射的地址范围是4MB
    or eax,PG_US_U | PG_RW_W | PG_P;页表存在于内存，可读写并且所有特权级都可以访问
    mov [PAGE_DIR_TABLE_POS+0x0],eax
    mov [PAGE_DIR_TABLE_POS+0xc00],eax

    sub eax,0x1000
    mov [PAGE_DIR_TABLE_POS+4092],eax
    ;最后一个页目录项映射的页表是它自己，用来索引页表的物理地址

;创建页表项，仅创建1MB的映射即可
    mov ecx,256
    mov esi,0
    mov edx,PG_US_U | PG_RW_W | PG_P
.create_pte:
    mov [ebx+esi*4],edx
    add edx,4096
    inc esi
    loop .create_pte

;创建内核其他页表的pde
    mov eax,PAGE_DIR_TABLE_POS
    add eax,0x2000
    or eax,PG_US_U | PG_RW_W | PG_P
    mov ebx,PAGE_DIR_TABLE_POS
    mov ecx,254
    mov esi,769
    ;范围是从769~1022的所有目录项数量
.create_kernel_pde:
    mov [ebx+esi*4],eax
    add eax,0x1000
    inc esi
    loop .create_kernel_pde
    ret