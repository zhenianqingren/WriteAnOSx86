#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"
#include "ioqueue.h"

#define KBD_PORT 0x60

// 转义字符定义
#define ESC '\033'
#define BACKSPACE '\b'
#define TAB '\t'
#define ENTER '\r'
#define DELETE '\0177'

// 不可见字符均定义为0
#define INVISIBLE 0
#define L_CTRL INVISIBLE
#define R_CTRL INVISIBLE
#define L_SHIFT INVISIBLE
#define R_SHIFT INVISIBLE
#define L_ALT INVISIBLE
#define R_ALT INVISIBLE
#define CAPSLOCK INVISIBLE

// 定义控制字符的通码和断码
#define L_CTRL_MAKE 0x1d
#define R_CTRL_MAKE 0xe01d
#define R_CTRL_BREAK 0xe09d
#define L_SHIFT_MAKE 0x2a
#define R_SHIFT_MAKE 0x36
#define L_ALT_MAKE 0x38
#define R_ALT_MAKE 0xe038
#define R_ALT_BREAK 0xe0b8
#define CAPSLOCK_MAKE 0x3a

// 定义变量记录控制键是否被按下 ext_scancode记录makecode是否以0xe0开头,区分左右
static int ctrl_stat, shift_stat, alt_stat, capslock_stat, ext_scancode;
struct ioqueue kbd_buf;

/* 以通码make_code为索引的二维数组 */
static char keymap[][2] =
    {
        /* 扫描码   未与shift组合  与shift组合*/
        /* ---------------------------------- */
        /* 0x00 */ {0, 0},
        /* 0x01 */ {ESC, ESC},
        /* 0x02 */ {'1', '!'},
        /* 0x03 */ {'2', '@'},
        /* 0x04 */ {'3', '#'},
        /* 0x05 */ {'4', '$'},
        /* 0x06 */ {'5', '%'},
        /* 0x07 */ {'6', '^'},
        /* 0x08 */ {'7', '&'},
        /* 0x09 */ {'8', '*'},
        /* 0x0A */ {'9', '('},
        /* 0x0B */ {'0', ')'},
        /* 0x0C */ {'-', '_'},
        /* 0x0D */ {'=', '+'},
        /* 0x0E */ {BACKSPACE, BACKSPACE},
        /* 0x0F */ {TAB, TAB},
        /* 0x10 */ {'q', 'Q'},
        /* 0x11 */ {'w', 'W'},
        /* 0x12 */ {'e', 'E'},
        /* 0x13 */ {'r', 'R'},
        /* 0x14 */ {'t', 'T'},
        /* 0x15 */ {'y', 'Y'},
        /* 0x16 */ {'u', 'U'},
        /* 0x17 */ {'i', 'I'},
        /* 0x18 */ {'o', 'O'},
        /* 0x19 */ {'p', 'P'},
        /* 0x1A */ {'[', '{'},
        /* 0x1B */ {']', '}'},
        /* 0x1C */ {ENTER, ENTER},
        /* 0x1D */ {L_CTRL, L_CTRL},
        /* 0x1E */ {'a', 'A'},
        /* 0x1F */ {'s', 'S'},
        /* 0x20 */ {'d', 'D'},
        /* 0x21 */ {'f', 'F'},
        /* 0x22 */ {'g', 'G'},
        /* 0x23 */ {'h', 'H'},
        /* 0x24 */ {'j', 'J'},
        /* 0x25 */ {'k', 'K'},
        /* 0x26 */ {'l', 'L'},
        /* 0x27 */ {';', ':'},
        /* 0x28 */ {'\'', '"'},
        /* 0x29 */ {'`', '~'},
        /* 0x2A */ {L_SHIFT, L_SHIFT},
        /* 0x2B */ {'\\', '|'},
        /* 0x2C */ {'z', 'Z'},
        /* 0x2D */ {'x', 'X'},
        /* 0x2E */ {'c', 'C'},
        /* 0x2F */ {'v', 'V'},
        /* 0x30 */ {'b', 'B'},
        /* 0x31 */ {'n', 'N'},
        /* 0x32 */ {'m', 'M'},
        /* 0x33 */ {',', '<'},
        /* 0x34 */ {'.', '>'},
        /* 0x35 */ {'/', '?'},
        /* 0x36 */ {R_SHIFT, R_SHIFT},
        /* 0x37 */ {'*', '*'},
        /* 0x38 */ {L_ALT, L_ALT},
        /* 0x39 */ {' ', ' '},
        /* 0x3A */ {CAPSLOCK, CAPSLOCK}};

/*
    键盘中断处理程序
*/

static void intr_keyboard_handler(void)
{
    /*
        上一次中断相应控制键是否被按下
    */
    int ctrl_last = ctrl_stat;
    int shift_last = shift_stat;
    int capslock_last = capslock_stat;

    int break_code;
    /**
     * 必须读出缓冲区寄存器，否则8042不再响应键盘中断
     */
    uint8_t inputcode = inb(KBD_PORT);
    if (inputcode == 0xe0)
    {
        // 以0xe0开头的特殊控制字符，赶紧进行下一次处理
        ext_scancode = 1;
        return;
    }
    uint16_t scancode = (uint16_t)inputcode;

    if (ext_scancode)
    {
        scancode = ((0xe0) << 8) | scancode;
        ext_scancode = 0; // 关闭标记
    }

    break_code = ((scancode & 0x0080) != 0); // 是否是断码
    if (break_code)
    {
        // 超过2字节的多字节扫描码暂不处理
        uint16_t make_code = (scancode &= 0xff7f); // 获取到是谁的断码

        if (make_code == L_CTRL_MAKE || make_code == R_CTRL_MAKE)
        {
            ctrl_stat = 0;
        }
        else if (make_code == L_SHIFT_MAKE || make_code == R_SHIFT_MAKE)
        {
            shift_stat = 0;
        }
        else if (make_code == L_ALT_MAKE || make_code == R_ALT_MAKE)
        {
            alt_stat = 0;
        }
        /*
            caplslock需要单独处理
        */
        return;
    }
    else if ((scancode > 0x0 && scancode < 0x3b) || scancode == R_CTRL || scancode == R_ALT)
    {
        /**
         * 只处理数组中定义的键+R_CTRL+R_ALT
         */
        int shift = 0;
        if ((scancode < 0x0e) /*'0'~'9' '-' '='*/ || (scancode == 0x29) /*'`'*/ || (scancode == 0x1a) /*'['*/ ||
            (scancode == 0x1b) /*']'*/ || (scancode == 0x2b) /*'\\'*/ || (scancode == 0x27) /*';'*/ ||
            (scancode == 0x28) /*'\''*/ || (scancode == 0x33) /*','*/ || (scancode == 0x34) /*'.'*/ ||
            (scancode == 0x35) /*'/'*/)
        {
            if (shift_last)
            {
                shift = 1;
            }
        }
        else
        {
            /*默认字母键*/
            if (shift_last && capslock_last)
            {
                shift = 0;
            }
            else if (shift_last || capslock_last)
            {
                shift = 1;
            }
            else
            {
                shift = 0;
            }
        }
        uint8_t index = (scancode & 0xff);
        char ch = keymap[index][shift];

        if ((ctrl_last && ch == 'l') || (ctrl_last && ch == 'u'))
        {
            ch -= 'a';
        }
        if (ch)
        {
            /*
                只处理可打印字符
            */
            if (!ioqueue_full(&kbd_buf))
            {
                // put_char(ch);
                ioqueue_push(&kbd_buf, ch);
            }
            return;
        }
        if (scancode == L_CTRL_MAKE || scancode == R_CTRL_MAKE)
        {
            ctrl_stat = 1;
        }
        else if (scancode == L_SHIFT_MAKE || scancode == R_SHIFT_MAKE)
        {
            shift_stat = 1;
        }
        else if (scancode == L_ALT_MAKE || scancode == R_ALT_MAKE)
        {
            alt_stat = 1;
        }
        else if (scancode == CAPSLOCK)
        {
            /*不管之前有没有按过，再次按下状态取反*/
            capslock_stat = !capslock_stat;
        }
    }
    else
    {
        put_str("unknown key\n");
    }
}

void keyboard_init()
{
    put_str("key board init start\n");
    ioqueue_init(&kbd_buf);
    register_handler(0x21, intr_keyboard_handler);
    put_str("key board init done\n");
}