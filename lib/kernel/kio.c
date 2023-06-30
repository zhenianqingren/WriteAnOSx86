#include "kio.h"
#include "console.h"

void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    vsprintf(buf, format, args);
    va_end(args);
    console_put_str(buf);
}