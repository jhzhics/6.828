#include <inc/lib.h>

void
umain(int argc, char **argv)
{
  cprintf("Run execl...\n");
  execl("hello", "hello", (char*)0);
  panic("You should not see this here.\n");
}

