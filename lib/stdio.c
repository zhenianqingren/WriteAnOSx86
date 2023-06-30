#include "./stdio.h"
#include "./string.h"
#include "./user/syscall.h"

static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base)
{
    uint32_t quot = value / base; // 商
    uint32_t rema = value % base; // 余数

    if (quot)
    {
        itoa(quot, buf_ptr_addr, base);
    }

    if (rema < 10)
    {
        *((*buf_ptr_addr)++) = rema + '0';
    }
    else
    {
        *((*buf_ptr_addr)++) = rema - 10 + 'A';
    }
}

// 将参数格式ap按照格式format输出到字符串，并返回替换后str长度
uint32_t vsprintf(char *str, const char *format, va_list ap)
{
    char *buf_ptr = str;
    const char *ind_ptr = format;
    char ind_ch = *ind_ptr;
    int32_t arg_int;
    char *arg_str;

    while (ind_ch)
    {
        if (ind_ch != '%')
        {
            *(buf_ptr++) = ind_ch;
            ind_ch = *(++ind_ptr);
            continue;
        }
        ind_ch = *(++ind_ptr);
        switch (ind_ch)
        {
        case 'x':
            arg_int = va_arg(ap, int);
            itoa(arg_int, &buf_ptr, 16);
            break;
        case 's':
            arg_str = va_arg(ap, char *);
            strcpy(buf_ptr, arg_str);
            buf_ptr += strlen(arg_str);
            break;
        case 'c':
            *(buf_ptr++) = va_arg(ap, char);
            break;
        case 'd':
            arg_int = va_arg(ap, int);
            if (arg_int < 0)
            {
                arg_int = -arg_int;
                *(buf_ptr++) = '-';
            }
            itoa(arg_int, &buf_ptr, 10);
            break;
        default:
            break;
        }
        ind_ch = *(++ind_ptr);
    }
    return strlen(str);
}

uint32_t printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024];
    vsprintf(buf, format, args);
    va_end(args);
    return write(buf);
}

uint32_t sprintf(char *buf, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    uint32_t ret;
    ret = vsprintf(buf, format, args);
    va_end(args);
    return ret;
}