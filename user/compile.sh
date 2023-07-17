BIN="tpipe"
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers"
LIB1="../lib/"
LIB2="../lib/user/"
LIB3="../fs/"
LIB4="../device/"
LIB5="../lib/kernel/"
LIB6="../kernel/"
LIB7="../thread/"
LIB8="../userprog/"
OBJS="../build/string.o ../build/syscall.o ../build/stdio.o ../start.o"

gcc $CFLAGS -I $LIB1 -I $LIB2 -I $LIB3 -I $LIB4 -I $LIB5 -I $LIB6 -I $LIB7 -I $LIB8 -o $BIN".o" $BIN".c"
ld $BIN".o" $OBJS -o $BIN