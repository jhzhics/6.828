// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>


#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmappings", "Display physical page mappings for a range of virtual addresses", mon_showmappings },
	{ "setperm", "Set permissions of a mapping", mon_setperm },
	{ "dump", "Dump memory contents for a range of addresses", mon_dump }
};

int
mon_dump(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 4)
	{
		cprintf("Usage: dump <va|pa> <start> <end>\n");
		return 0;
	}

	int is_virtual = strcmp(argv[1], "va") == 0;
	uintptr_t start = strtol(argv[2], NULL, 16);
	uintptr_t end = strtol(argv[3], NULL, 16);
	if (start > end)
	{
		cprintf("Invalid range\n");
		return 0;
	}

	for (uintptr_t addr = start; addr <= end; addr += sizeof(uint32_t))
	{
		if (is_virtual && (addr % PGSIZE == 0 || addr == start))
		{
			pte_t *pte = pgdir_walk(kern_pgdir, (void *)addr, 0);
			if (pte == NULL || !(*pte & PTE_P))
			{
				cprintf("0x%08x: unmapped\n", addr);
				continue;
			}
		}

		uint32_t *ptr = is_virtual ? (uint32_t *)addr : (uint32_t *)KADDR(addr);
		cprintf("0x%08x: %08x\n", addr, *ptr);
	}

	return 0;
}

/***** Implementations of basic kernel monitor commands *****/
int
mon_setperm(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 4)
	{
		cprintf("Usage: setperm <va> <perm> <value>\n"
			"  va: virtual address\n"
			"  perm: permission to set (PTE_U, PTE_W, PTE_P)\n"
			"  value: 1 to set, 0 to clear\n"
			"  PTE_U: %x, PTE_W: %x, PTE_P: %x\n",
			PTE_U, PTE_W, PTE_P
			);
		return 0;
	}

	uintptr_t va = strtol(argv[1], NULL, 16);
	int perm = strtol(argv[2], NULL, 16);
	int value = strtol(argv[3], NULL, 16);

	pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);
	if (pte == NULL || !(*pte & PTE_P))
	{
		cprintf("Invalid address\n");
		return 0;
	}

	if (value)
		*pte |= perm;
	else
		*pte &= ~perm;

	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3)
	{
		cprintf("Usage: showmappings <start> <end>\n");
		return 0;
	}

	uintptr_t start = ROUNDDOWN(strtol(argv[1], NULL, 16), PGSIZE);
	uintptr_t end = ROUNDDOWN(strtol(argv[2], NULL, 16), PGSIZE);
	if (start > end)
	{
		cprintf("Invalid range\n");
		return 0;
	}

	for (uintptr_t va = start; va <= end; va += PGSIZE)
	{
		pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);
		if (pte == NULL || !(*pte & PTE_P))
		{
			cprintf("0x%08x - 0x%08x: unmapped\n", va, va + PGSIZE);
			continue;
		}

		cprintf("0x%08x - 0x%08x: 0x%08x - 0x%08x\tperm: %c%c%c\n",
		va, va + PGSIZE,
		PTE_ADDR(*pte), PTE_ADDR(*pte) + PGSIZE,
		(*pte & PTE_U) ? 'U' : '-',
		(*pte & PTE_W) ? 'W' : '-',
		(*pte & PTE_P) ? 'P' : '-'
		);
	}

	return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	struct Eipdebuginfo eip_debuginfo;
	uint32_t args[5];
	uint32_t ebp = read_ebp();
	while (ebp)
	{
		uint32_t eip = *((uint32_t *)ebp + 1);
		args[0] = *((uint32_t *)ebp + 2);
		args[1] = *((uint32_t *)ebp + 3);
		args[2] = *((uint32_t *)ebp + 4);
		args[3] = *((uint32_t *)ebp + 5);
		args[4] = *((uint32_t *)ebp + 6);

		debuginfo_eip(eip, &eip_debuginfo);
		
			cprintf("  ebp %08x  eip %08x  args \
%08x %08x %08x %08x %08x\n",
	ebp, eip,
	args[0], args[1], args[2], args[3], args[4]);

		cprintf("         %s:%d: %.*s+%d\n",
		eip_debuginfo.eip_file,
		eip_debuginfo.eip_line,
		eip_debuginfo.eip_fn_namelen,
		eip_debuginfo.eip_fn_name,
		eip - (uint32_t)eip_debuginfo.eip_fn_addr);

		ebp = *(uint32_t *)ebp;
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void lab()
{
	unsigned int i = 0x00646c72;
    cprintf("H%x Wo%s", 57616, &i);
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");
	
	cprintf("\033[97;31mText White, background red!\033[0m\n");

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
