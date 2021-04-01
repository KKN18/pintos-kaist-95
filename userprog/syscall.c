#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* Our Implementation */
#include "filesys/inode.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
struct lock file_access;
/* END */

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* Our Implementation */
	lock_init(&file_access);
	/* END */

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* Our Implementation */
int
add_file (struct file *file_name)
{
  struct process_file *process_file_ptr = malloc(sizeof(struct process_file));
  if (!process_file_ptr)
  {
    return -1;
  }
  
  process_file_ptr->file = file_name;
  process_file_ptr->fd = thread_current()->fd;
  thread_current()->fd++;
  list_push_back(&thread_current()->file_list, &process_file_ptr->elem);
  return process_file_ptr->fd;
  
}

struct file*
get_file (int filedes)
{
  struct thread *t = thread_current();
  struct list_elem* next;
  struct list_elem* e = list_begin(&t->file_list);
  
  for (; e != list_end(&t->file_list); e = next)
  {
    next = list_next(e);
    struct process_file *process_file_ptr = list_entry(e, struct process_file, elem);
    if (filedes == process_file_ptr->fd)
    {
      return process_file_ptr->file;
    }
  }
  return NULL; // nothing found
}

void
process_close_file (int file_descriptor)
{
  struct thread *t = thread_current();
  struct list_elem *next;
  struct list_elem *e = list_begin(&t->file_list);
  
  for (;e != list_end(&t->file_list); e = next)
  {
    next = list_next(e);
    struct process_file *process_file_ptr = list_entry (e, struct process_file, elem);
    if (file_descriptor == process_file_ptr->fd)
    {
      file_close(process_file_ptr->file);
      list_remove(&process_file_ptr->elem);
      free(process_file_ptr);
    //   if (file_descriptor != CLOSE_ALL_FD)
    //   {
    //     return;
    //   }
    }
  }
}

void check_user_vaddr(const void *vaddr) {
   if (!is_user_vaddr(vaddr))
      exit(-1);
}

void halt (void) {
	power_off();
}

void exit(int status) {
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

int wait(pid_t pid) {
	return process_wait(pid);
}

pid_t exec (const char *cmd_line) {
  return process_exec(cmd_line);
}

pid_t fork (const char *thread_name) {
	return process_fork(thread_name);
}

bool create(const char *file, unsigned initial_size) {
	if (file == NULL) exit(-1);
	if (strlen(file) == 0) exit(-1);
	bool ret = filesys_create(file, initial_size);
	return ret;
}

bool remove(const char *file) {
	if (file == NULL) exit(-1);
	bool ret = filesys_remove(file);
	return ret;
}


int open(const char *file_name) {
	lock_acquire(&file_access);
	struct file *file_ptr;
  	file_ptr = filesys_open(file_name); // from filesys.h
	// printf("file length : %d\n", file_length(file_ptr));
	if (!file_ptr)
	{
		lock_release(&file_access);
	}
	int filedes = add_file(file_ptr);
	// printf("Length : %d\n", inode_length(file_get_inode()));
	lock_release(&file_access);
	return filedes;
}

int filesize (int fd) {
	lock_acquire(&file_access);
	struct file *file_ptr = get_file(fd);
	if (!file_ptr)
	{
		lock_release(&file_access);
		return -1;
	}
	int filesize = inode_length(file_get_inode(file_ptr));
	lock_release(&file_access);
	return filesize;
}

int
read (int fd, void *buffer, unsigned size)
{
	if (size <= 0)
	{
		return size;
	}
	// printf("read size : %d\n", size);
	if (fd == 0)
	{
		unsigned i = 0;
		uint64_t *local_buf = (uint64_t *) buffer;
		for (;i < size; i++)
		{
		// retrieve pressed key from the input buffer
			local_buf[i] = input_getc(); // from input.h
		}
		return size;
	}
	
	/* read from file */
	lock_acquire(&file_access);
	struct file *file_ptr = get_file(fd);
	if (!file_ptr)
	{
		lock_release(&file_access);
		return -1;
	}
	int bytes_read = file_read(file_ptr, buffer, size); // from file.h
	lock_release (&file_access);
	return bytes_read;
}

int write (int fd, const void *buffer, unsigned size)
{
	if (size <= 0)
    {
      return size;
    }
    if (fd == 1)
    {
      putbuf (buffer, size); // from stdio.h
      return size;
    }
    
    // start writing to file
    lock_acquire(&file_access);
    struct file *file_ptr = get_file(fd);
    if (!file_ptr)
    {
      lock_release(&file_access);
      return -1;
    }
    int bytes_written = file_write(file_ptr, buffer, size); // file.h
    lock_release (&file_access);
    return bytes_written;
}

void seek (int fd, unsigned position)
{
  struct file *f = process_get_file (fd);
  if (f == NULL)
    return;
  file_seek (f, position);  
}

unsigned tell (int fd)
{
  struct file *f = process_get_file (fd);
  if (f == NULL)
    exit (-1);
  return file_tell (f);
}

void close (int fd)
{ 
	lock_acquire(&file_access);
  	process_close_file (fd);
	lock_release(&file_access);
}

/* END */



/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	/* Our Implementation */
	// printf ("syscall num : %d\n", f->R.rax);
	// printf ("system call!\n");

	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			check_user_vaddr(f->R.rdi);
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			// f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:
			check_user_vaddr(f->R.rdi);
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			check_user_vaddr(f->R.rdi);
			check_user_vaddr(f->R.rsi);
			check_user_vaddr(f->R.rdx);
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			check_user_vaddr(f->R.rdi);
			check_user_vaddr(f->R.rsi);
			check_user_vaddr(f->R.rdx);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			check_user_vaddr(f->R.rdi);
			check_user_vaddr(f->R.rsi);
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			check_user_vaddr(f->R.rdi);
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			check_user_vaddr(f->R.rdi);
			close(f->R.rdi);
			break;
	}
	/* END */

	
}


