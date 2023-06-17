#include "print.h"

int main(void)
{
    int i;
    int j;
    for (i = 0; i < 25; ++i)
    {
        for (j = 0; j < 8; ++j)
        {
            put_str("0123456789");
        }
    }
    put_int(0x12345678);
    while (1)
        ;
    return 0;
}