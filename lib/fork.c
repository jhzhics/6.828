// implement fork from user space
#define JOS_USER 1
#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	uint32_t pdx = PDX(utf->utf_fault_va);

	pde_t pde = uvpd[pdx];
	if(!(pde & PTE_P))
	{
		panic("pde does not exist");
	}
	pte_t pte = uvpt[PGNUM(utf->utf_fault_va)];
	if(!(utf->utf_err & FEC_WR) || (~pte & (PTE_P | PTE_COW)))
	{
		cprintf("%x %d\n", utf->utf_err, pte);
		panic("Panic utf_fault_va at %08x\n",
		utf->utf_fault_va);
	}
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	envid_t envid = sys_getenvid();
	int perm = PTE_P | PTE_W | PTE_U;
	if(sys_page_alloc(envid, (void *)PFTEMP, perm)<0)
	{
		panic("sys_page_alloc error");
	}
	memcpy((void *)PFTEMP, (void *)ROUNDDOWN(utf->utf_fault_va, PGSIZE), PGSIZE);
	if(sys_page_map(envid, (void *)PFTEMP, envid, (void *)ROUNDDOWN(utf->utf_fault_va, PGSIZE),
	(pte & 0xfff & ~PTE_COW) | PTE_W) < 0)
	{
		panic("sys_page_map error");
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	envid_t curenvid = sys_getenvid();
	void *va = (void *)(pn * PGSIZE);
	assert((uintptr_t)va < UTOP);
	int pdx = PDX(va);
	int ptx = PTX(va);
	
	pde_t pde = uvpd[pdx];
	if(!(pde & PTE_P))
	{
		panic("pde does not exist");
	}
	pte_t pte = uvpt[PGNUM(va)];
	int perm;
	if (pte & (PTE_W | PTE_COW))
	{
		perm = (pte & 0xfff & ~PTE_W) | PTE_COW;
	}
	else
	{
		perm = pte & 0xfff;
	}
	
	int err = sys_page_map(curenvid, va, envid, va, perm);
	if(err < 0)
	{
		return err;
	}

	if(pte & (PTE_W | PTE_COW))
	{
		sys_page_map(curenvid, va, curenvid, va, perm);
		if(err < 0)
		{
			return err;
		}
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
// The basic control flow for fork() is as follows:

// The parent installs pgfault() as the C-level page fault handler, using the set_pgfault_handler() function you implemented above.
// The parent calls sys_exofork() to create a child environment.
// For each writable or copy-on-write page in its address space below UTOP, the parent calls duppage, which should map the page copy-on-write into the address space of the child and then remap the page copy-on-write in its own address space. [ Note: The ordering here (i.e., marking a page as COW in the child before marking it in the parent) actually matters! Can you see why? Try to think of a specific case where reversing the order could cause trouble. ] duppage sets both PTEs so that the page is not writeable, and to contain PTE_COW in the "avail" field to distinguish copy-on-write pages from genuine read-only pages.
// The exception stack is not remapped this way, however. Instead you need to allocate a fresh page in the child for the exception stack. Since the page fault handler will be doing the actual copying and the page fault handler runs on the exception stack, the exception stack cannot be made copy-on-write: who would copy it?

// fork() also needs to handle pages that are present, but not writable or copy-on-write.

// The parent sets the user page fault entrypoint for the child to look like its own.
// The child is now ready to run, so the parent marks it runnable.
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t child_envid = sys_exofork();
	if(child_envid)
	{
		//parent
		volatile const struct Env *child_env = &envs[ENVX(child_envid)];
		for(uintptr_t va = 0; va < UTOP; va += PGSIZE)
		{
			if (va == UXSTACKTOP - PGSIZE)
			{
				int err = sys_page_alloc(child_envid, (void *)va, PTE_U | PTE_P | PTE_W);
				if(err < 0)
				{
					panic("sys_page_alloc panic");
				}
				continue;
			}

			if (!(uvpd[PDX(va)] & PTE_P))
			{
				continue;
			}

			pte_t pte = uvpt[PGNUM(va)];
			if(pte & PTE_P)
			{
				int err = duppage(child_envid, PGNUM(va));
				if(err)
				{
					panic("duppage panic");
				}
			}
		}
		sys_env_set_pgfault_upcall(child_envid, thisenv->env_pgfault_upcall);
		sys_env_set_status(child_envid, ENV_RUNNABLE);
	}
	else
	{
	}
	return child_envid;
}

// Challenge!
int
sfork(void)
{
	set_pgfault_handler(pgfault);
	envid_t child_envid = sys_exofork();
	if(child_envid)
	{
		//parent
		volatile const struct Env *child_env = &envs[ENVX(child_envid)];
		for(uintptr_t va = 0; va < UTOP; va += PGSIZE)
		{
			if (va == UXSTACKTOP - PGSIZE)
			{
				int err = sys_page_alloc(child_envid, (void *)va, PTE_U | PTE_P | PTE_W);
				if(err < 0)
				{
					panic("sys_page_alloc panic");
				}
				continue;
			}
			if(va == USTACKTOP - PGSIZE)
			{
				int err = duppage(child_envid, va >> PGSHIFT);
				if(err < 0)
				{
					panic("duppage panic");
				}
				continue;
			}

			if (!(uvpd[PDX(va)] & PTE_P))
			{
				continue;
			}

			pte_t pte = uvpt[PGNUM(va)];
			if(pte & PTE_P)
			{
				int err = sys_page_map(thisenv->env_id, (void *)va, child_envid, 
				(void *)va, pte & 0xfff);
				if(err)
				{
					panic("sys_page_map panic");
				}
			}
		}
		sys_env_set_pgfault_upcall(child_envid, thisenv->env_pgfault_upcall);
		sys_env_set_status(child_envid, ENV_RUNNABLE);
	}
	else
	{
	}
	return child_envid;
}
