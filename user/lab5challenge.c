#include <inc/lib.h>

void
umain(int argc, char **argv)
{
  cprintf("Run execl...\n");
  execl("/init", "init", "initarg1", "initarg2", (char*)0);
  panic("You should not see this here.\n");
}

