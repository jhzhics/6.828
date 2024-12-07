#include <inc/lib.h>
#include <inc/elf.h>



int execl(const char *prog, const char *arg0, ...)
{
	int argc=0;
	va_list vl;
	va_start(vl, arg0);
	while(va_arg(vl, void *) != NULL)
		argc++;
	va_end(vl);

	// Now that we have the size of the args, do a second pass
	// and store the values in a VLA, which has the format of argv
	const char *argv[argc+2];
	argv[0] = arg0;
	argv[argc+1] = NULL;

	va_start(vl, arg0);
	unsigned i;
	for(i=0;i<argc;i++)
		argv[i+1] = va_arg(vl, const char *);
	va_end(vl);
	return execv(prog, argv);
}

int
execv(const char *prog, const char **argv)
{
	void *elf_buf = (void *)0x3f000000;
	int fd = open(prog, O_RDONLY);
	if(fd < 0)
	{
		panic("error");
	}
	struct Stat file_stat;
	if(fstat(fd, &file_stat) < 0)
	{
		panic("error");
	}

	int r;
	int elf_size = file_stat.st_size;
	int buf_size = ROUNDUP(elf_size, PGSIZE);
	for(int i = 0; i < buf_size / PGSIZE; i++)
	{
		r = sys_page_alloc(thisenv->env_id, (void *)((uintptr_t)elf_buf + PGSIZE * i), PTE_U | PTE_W | PTE_P);
		if(r < 0)
		{
			panic("error");
		}
	}
	int left_fsz = elf_size;
	int buf_p = 0;
	while (left_fsz > 0)
	{
		int read_size = MIN(left_fsz, PGSIZE);
		r = readn(fd, elf_buf + buf_p ,read_size);
		if(r != read_size)
		{
			panic("error");
		}
		buf_p += read_size;
		left_fsz -=read_size;
	}
	close(fd);
	sys_execv(elf_buf, elf_size, argv);
	panic("error");
	return -1;
}

