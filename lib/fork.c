// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);

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
	//tf->tf_err & 2 == rakstisana
	if (!((err & 2) && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("access is not a write or not to a copy-on-write-page");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP, PTE_U | PTE_P | PTE_W)) < 0)
		panic("pgfault cannot allocate new writable page");

	memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);	

	if((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_U | PTE_P | PTE_W)) < 0)
		panic("pgfault cannot copy date to the new page");

	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("pgfault cannot unmap the old page");

	//panic("pgfault not implemented");
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
	//panic("duppage not implemented");

	// duppage should map the page copy-on-write into the address space of the child 
	// and then remap the page copy-on-write in its own address space. 
	// duppage sets both PTEs so that the page is not writeable, 
	// and to contain PTE_COW in the "avail" field to distinguish copy-on-write pages 
	// from genuine read-only pages.


	// If the page is writable or copy-on-write, the new mapping must be created copy-on-write,
	// and then our mapping must be marked copy-on-write as well.
	if (uvpt[pn] & PTE_W || uvpt[pn] & PTE_COW) {
		// map the child
		if ((r = sys_page_map(thisenv->env_id, (void *)(pn*PGSIZE), envid, (void *)(pn*PGSIZE), PTE_COW | PTE_U | PTE_P)) < 0)
		panic("duppdage cannot map the child page: %e", r);

		// map current environment
		if ((r = sys_page_map(thisenv->env_id, (void *)(pn*PGSIZE), thisenv->env_id, (void *)(pn*PGSIZE), PTE_COW | PTE_U | PTE_P)) < 0)
		panic("duppdage cannot map the current environment page: %e", r);
	}
	else {
		// Do not give COW permissions
		if ((r = sys_page_map(thisenv->env_id, (void *)(pn*PGSIZE), envid, (void *)(pn*PGSIZE), PTE_U | PTE_P)) < 0)
		panic("duppdage cannot map the child page: %e", r);
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
	// LAB 4: Your code here.
	//panic("fork not implemented");
	envid_t child_id;
	int r;

	// 1. The parent installs pgfault() as the C-level page fault handler, 
	// using the set_pgfault_handler() function you implemented above.
	set_pgfault_handler(pgfault);

	// 2. The parent calls sys_exofork() to create a child environment.
	child_id = sys_exofork();

	if (child_id < 0)
		panic("fork cannot create child environment: %e", child_id);

	if (child_id == 0) {
		// If this is the child process, fix "thisenv"
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// 3. For each writable or copy-on-write page in its address space below UTOP, 
	// the parent calls duppage
	// fork() also needs to handle pages that are present, but not writable or copy-on-write.
	int i;
	for (i = 0; i < USTACKTOP; i+= PGSIZE) {
		if (((uvpd[PDX(i)] & PTE_P) == PTE_P) && ((uvpt[PGNUM(i)] & PTE_P) == PTE_P))
			duppage(child_id, PGNUM(i));
	}	
	
	//   Neither user exception stack should ever be marked copy-on-write,
	//   so you must allocate a new page for the child's user exception stack.
	if ((r = sys_page_alloc(child_id, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W)) < 0)
		panic("fork cannot allocate page for child's user exception stack: %e", r);
	
	
	// 4. The parent sets the user page fault entrypoint for the child to look like its own.
	if ((r = sys_env_set_pgfault_upcall(child_id, _pgfault_upcall)) < 0)
		panic("fork cannot set user fault entrypoint for the child: %e", r);


	// 5. The child is now ready to run, so the parent marks it runnable.
	if ((r = sys_env_set_status(child_id, ENV_RUNNABLE)) < 0)
		panic("fork cannot set child environment as runnable: %e", r);

	return child_id;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
