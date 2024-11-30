#include <inc/lib.h>

void umain(int argc, char *argv[])
{
    cprintf("before breakpoint\n");
    asm volatile("int $3");
    cprintf("after breakpoint\n");
}