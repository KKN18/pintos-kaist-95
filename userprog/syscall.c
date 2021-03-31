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


int open(const char *file) {
	if (file == NULL) exit(-1);
	int result = -1;
	lock_acquire(&file_access);
	result = process_add_file (filesys_open(file));
	lock_release(&file_access);
	return result;
}

int filesize (int fd) {
	struct file *f = process_get_file (fd);
	if (f == NULL) return -1;
	return file_length(f);
}

int
read (int fd, void *buffer, unsigned size)
{
  struct file *f;
  lock_acquire (&file_access);

  if (fd == STDIN_FILENO)
  {
    // 표준 입력
    unsigned count = size;
    while (count--)
      *((char *)buffer++) = input_getc();
    lock_release (&file_access);
    return size;
  }
  if ((f = process_get_file (fd)) == NULL)
    {
      lock_release (&file_access);
      return -1;
    }
  size = file_read (f, buffer, size);
  lock_release (&file_access);
  return size;
}

int write (int fd, const void *buffer, unsigned size)
{
  struct file *f;
  lock_acquire (&file_access);
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      lock_release (&file_access);
      return size;  
    }
  if ((f = process_get_file (fd)) == NULL)
    {
      lock_release (&file_access);
      return 0;
    }
  size = file_write (f, buffer, size);
  lock_release (&file_access);
  return size;
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
  process_close_file (fd);
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
			break;
		case SYS_READ:
			check_user_vaddr(f->R.rdi);
			check_user_vaddr(f->R.rsi);
			check_user_vaddr(f->R.rdx);
			f->R.rdi = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			check_user_vaddr(f->R.rdi);
			check_user_vaddr(f->R.rsi);
			check_user_vaddr(f->R.rdx);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}
	/* END */

	
}


