#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif
#define WORD_SIZE 8
#define LOG 0

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	if(LOG)
		printf("process_create_initd: %s\n", file_name);
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Our Implementation */
	char *filename_copy = (char *)calloc(strlen(file_name)+1, sizeof(char));
	if (filename_copy == NULL)
	{
		palloc_free_page (fn_copy);
		return TID_ERROR;
	}
	char *free_ptr = filename_copy;
	strlcpy(filename_copy, file_name, strlen(file_name) + 1);
	char *argptr;
	strtok_r(filename_copy, " ", &argptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (filename_copy, PRI_DEFAULT, initd, fn_copy);
	free(free_ptr);
	/* END */
	if (tid == TID_ERROR)
	{
		palloc_free_page (fn_copy);
	}
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
	if(LOG)
		printf("initd %s\n", f_name);
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct thread_and_if *tif UNUSED) {
	/* Clone current thread to new thread.*/
	tif->t = thread_current();
	return thread_create (name,
			PRI_DEFAULT, __do_fork, tif);
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable = false;

	/* Our Implementation */
	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kern_pte(pte))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if(newpage == NULL) {
		palloc_free_page(newpage);
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	if(is_writable(pte))
		writable = true;

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		// Free newpage and return false?
		palloc_free_page(newpage);
		return false;
	}
	return true;
	/* END */
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread_and_if *tif = (struct thread_and_if *) aux;
	struct thread *parent = (struct thread *) tif->t;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = (struct intr_frame *) tif->if_;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
	
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif
	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	/* Our Implementation */
	struct list_elem *p = list_begin(&parent->file_list);
	for (p; p!=list_end(&parent->file_list); p=list_next(p))
	{
		struct file *f;
		struct file_info *pfi = list_entry(p, struct file_info, file_elem);
		if ((f =file_duplicate(pfi->file))!=NULL)
		{
			struct file_info *fi = malloc(sizeof(struct file_info));
			if (fi == NULL) 
			{
				succ = false;
				goto error;
			}
			fi->file = f;
			fi->fd = pfi->fd;
			list_push_back(&current->file_list, &fi->file_elem);
		}
		else
		{
			succ = false;
			goto error;
		}
	}
	current->fd = parent->fd;
	current->filecopy_success = succ;
	/* fork() of child process should return 0 */
	if_.R.rax = 0;
	free(tif->if_);
	free(tif);
	sema_up(&current->filecopy_sema);

	/* END */
	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->filecopy_success = succ;
	free(tif->if_);
	free(tif);
	sema_up(&current->filecopy_sema);
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	if(LOG)
		printf("process_exec: %s\n", f_name);
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;
	/* We first kill the current context */
	/* Our Implementation */
	// char file_copy[256];
	char *file_copy = (char *)calloc(strlen(file_name)+1, sizeof(char));
	if (file_copy == NULL)
	{
		process_cleanup ();
		return -1;
	}
	strlcpy(file_copy, file_name, strlen(file_name) + 1);
	process_cleanup ();
	/* And then load the binary */
	success = load (file_copy, &_if);
	struct thread *t = thread_current();
	/* If load failed, quit. */
	if (!success)
	{
		if(LOG)
			printf("Load failed. Quit\n");
		free(file_copy);
		return -1;
	}
	free(file_copy);
	if(LOG)
		printf("Load successful, start switched process.\n");
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

/* Our Implementation for Project 2 */
/* Return child for given thread id. Mainly used in syscall.c and process.c */
struct thread *find_child (tid_t tid)
{
	struct thread *curr = thread_current();
	struct list_elem *p = list_begin(&curr->child_list);
	/* If the thred has same tid with the parameter, that is what we are finding */
	for (p; p!=list_end(&curr->child_list); p=list_next(p))
	{
		struct thread *child = list_entry(p, struct thread, child_elem);
		if (child->tid == tid)
			return child;
	}
	return NULL; // when there is no child with same tid, return null
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	/* Our Implementation */
	struct thread *child = find_child(child_tid); //find child which has tid same with child_tid
	if (child == NULL) // if there is no appropriate child in child list
		return -1;
	/* Wait for child to exit. Child calls sema_up at thread_exit() */
	sema_down(&child->wait_sema);
	int exit_status = child->exit_status; //Get child's exit status to return it. we should get value here before child actually exits and clean the value.
	list_remove(&child->child_elem); //remove child from current child list because it will exit and be deleted
	/* Now child can exit continuously and actually be deleted*/
	sema_up(&child->exit_sema);
	return exit_status;
	// END
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	if(LOG)
		printf("process_exit: %s\n", thread_name());
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	struct list_elem *p = list_begin(&curr->file_list);
	for (p; p!=list_end(&curr->file_list);)
	{
		struct file_info *fi = list_entry(p, struct file_info, file_elem);
		file_close(fi->file);
		p = list_remove(&fi->file_elem);
		free(fi);
	}
	curr->fd = 2;
	// ASSERT(file_deny_cnt(curr->prog_file) != 0);
	file_close(curr->prog_file);
	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Our Implementation */
int args_count(char *file_name) {
	char *last;
	char *token;
	char *filename_copy;
	int argc;

	filename_copy = calloc(strlen(file_name)+1, sizeof(char));
	if (filename_copy == NULL)
		return -1;
	char *free_ptr = filename_copy;
	strlcpy(filename_copy, file_name, strlen(file_name) + 1);
	token = strtok_r(filename_copy, " ", &last);
	
	argc = 0;
	while (token != NULL) {
		argc++;
		token = strtok_r(NULL, " ", &last);
	}
	free(free_ptr);
	return argc; 
}

bool pass_arguments(char *file_name, struct intr_frame *if_){
	// Used for strtok_r
	char *token;
	char *last;

	void **rsp = &if_->rsp;

	// Our Implementation
	int argc = args_count(file_name);
	if (argc == -1)
		return false;
	char **argv = (char **)malloc(sizeof(char *) * argc);
	if (argv == NULL)
		return false;
	// Temporarily store file_name on filename_copy
	char *filename_copy = (char *)calloc(strlen(file_name)+1, sizeof(char));
	if (filename_copy == NULL)
	{
		free(argv);
		return false;
	}
	char *free_ptr = filename_copy;
	// char filename_copy[256];

	// store argv
	int cnt = 0;
	int total_len = 0;
	strlcpy(filename_copy, file_name, strlen(file_name) + 1);
	token = strtok_r(filename_copy, " ", &last);
	while(token != NULL) 
	{
		argv[cnt++] = token;
		total_len += strlen(token) + 1;
		token = strtok_r(NULL, " ", &last);
	}

	/* push argv[argc - 1] ~ argv[0] */
	for (int i = argc - 1; i>=0; i--) 
	{
		*rsp -= strlen(argv[i]) + 1;
		strlcpy(*rsp, argv[i], strlen(argv[i]) + 1);
		argv[i] = *rsp;
	}

	/* push word align */
	if(total_len % WORD_SIZE != 0) {
		*rsp -= (WORD_SIZE - (total_len % WORD_SIZE));
	}

	/* push NULL */
	*rsp -= WORD_SIZE;
	**(uint64_t **)rsp = 0;

	/* push address of argv[argc - 1] ~ argv[0] */
	for (int i = argc - 1; i>=0; i--) {
		*rsp -= WORD_SIZE;
		**(uint64_t **)rsp = argv[i];
	}

	/* Save argv, argc on registers */
	if_->R.rsi = *rsp;
   	if_->R.rdi = argc;

	/* push return address */
	*rsp -= WORD_SIZE;
	**(uint64_t **)rsp = 0;

	/* Our Implementation */
	// For debugging
	// hex_dump(*rsp, *rsp, 64, 1);
	/* END */
	free(free_ptr);
	free(argv);
	return true;
}
/* END */

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	if(LOG)
		printf("load %s\n", file_name);
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());
	
	/* Our Implementation */
	// char filename_copy[128];
	char *filename_copy = (char *)calloc(strlen(file_name)+1, sizeof(char));
	if (filename_copy == NULL)
	{
		success = false;
		goto done;
	}

	char *free_ptr = filename_copy;
	strlcpy(filename_copy, file_name, strlen(file_name) + 1);
	char *argptr;
	strtok_r(filename_copy, " ", &argptr);
	/* END */

	// Original Implementation
	/* Open executable file. */
	/*
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	*/
	
	/* Our Implementation */
	lock_acquire(&file_access);
	file = filesys_open (filename_copy);
	if (file == NULL) {
		lock_release(&file_access);
		printf ("load: %s: open failed\n", filename_copy);
		success = false;
		free(free_ptr);
		goto done;
	}
	t->prog_file = file;
	file_deny_write(t->prog_file);
	lock_release(&file_access);
	
	free(free_ptr);
	/* END */

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}
	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;
	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/* Our Implemantation */
	success = pass_arguments((char *)file_name, if_);
	/* END */

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}

	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
bool install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}

static bool
lazy_load_segment (struct page *page, void *aux) {
	if(LOG)
		printf("Lazy_load_segment on page 0x%lx\n", page->va);
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* GOJAE */
	struct frame *frame = page->frame;
    struct container *container = (struct container*) aux;
    struct file *file = container->file;
    size_t page_read_bytes = container->page_read_bytes;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    bool writable = container->writable;
    off_t offset = container->offset;

	// Load anon_page from container
	struct anon_page *anon_page = &page->anon;
	anon_page->page_read_bytes = page_read_bytes;
	anon_page->writable = writable;
	anon_page->offset = offset;

	ASSERT(file != NULL);
    file_seek(file, offset);

    if (file_read(file, frame->kva, page_read_bytes) != (int)page_read_bytes)
    {
        return false;
    }
    memset((frame->kva) + page_read_bytes, 0, page_zero_bytes);
	
	if(LOG)
	{
		printf("	Page(0x%lx) loaded successfully\n", page->va);
	}

	return install_page(page->va, frame->kva, writable);
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	/* Not sure */
	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		/* GOJAE */
		struct container *container = (struct container*) malloc(sizeof(struct container));

        container->file = file;
        container->page_read_bytes = page_read_bytes;
        container->writable = writable;
        container->offset = ofs;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, container))
			return false;
		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	if(LOG)
		printf("setup_stack\n");
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	struct thread *t = thread_current();
	struct page *page;
	success = vm_claim_page(stack_bottom);
	// Mark the page as STACK (VM_MARKER_0)
	if (success)
		if_->rsp = USER_STACK;
	else
	{
		PANIC("setup stack error");
	}
	return success;
}
#endif /* VM */
