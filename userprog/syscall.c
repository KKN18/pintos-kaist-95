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



// Renamed Implementation
int allocate_fd (struct file *f) {
	struct file_info *fi = malloc(sizeof(struct file_info));
	struct thread *curr = thread_current();
	int empty_fd = curr->fd;
	curr->fd += 1;

	fi->file = f;
	fi->fd = empty_fd;
	list_push_back(&curr->file_list, &fi->file_elem);
	
	return empty_fd;
}


struct file_info *search_file_info (int fd) {
	struct thread *curr = thread_current();
	struct list_elem *p = list_begin(&curr->file_list);

	for (p; p != list_end(&curr->file_list); p = list_next(p))
	{
		struct file_info *fi = list_entry(p, struct file_info, file_elem);
		if (fi->fd == fd) return fi;
	}
	return NULL;
}

struct file *search_file (int fd) {
	struct file_info *fi = search_file_info(fd);
	if (!fi) return NULL;
	return fi->file;
}

void delete_file (int fd) {
	struct file_info *fi = search_file_info(fd);
	if (!fi) return;
	file_close(fi->file);
	list_remove(&fi->file_elem);
	free(fi);
}

void assert_valid_useraddr(const void *vaddr) {
   if (!is_user_vaddr(vaddr))  exit(-1);
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
/*
pid_t exec (const char *cmd_line) {
  return process_exec(cmd_line);
}
*/

pid_t exec (const char *file)
{
	tid_t tid;
	struct thread *child;

	// 여기에서 실패하면 스레드 자료 구조 생성 실패입니다.
	if ((tid = process_create_initd (file)) == TID_ERROR)
		return TID_ERROR;

	child = thread_get_child (tid);
	ASSERT (child);

	sema_down (&child->load_sema);

	// 여기에서 실패하면 프로그램 적재 실패입니다.
	if (!child->load_succeeded)
		return TID_ERROR;

	return tid;
}

pid_t sys_fork (const char *thread_name, struct intr_frame *if_) {
	return process_fork(thread_name, if_);
}

bool create(const char *file, unsigned initial_size) {
	if (file == NULL) exit(-1);
	if (strlen(file) == 0) return false;
	bool ret = filesys_create(file, initial_size);
	return ret;
}

bool remove(const char *file) {
	if (file == NULL) return false;
	bool ret = filesys_remove(file);
	return ret;
}

int open(const char *file) {
	if (!file) exit(-1);
	lock_acquire(&file_access);
	struct file *f;
  	f = filesys_open(file); 
	if (!f) 
	{
		lock_release(&file_access);
		return -1;
	}
	int fd = allocate_fd(f);
	lock_release(&file_access);
	return fd;
}

int filesize (int fd) {
	lock_acquire(&file_access);
	struct file *f = search_file(fd);
	if (!f)
	{
		lock_release(&file_access);
		exit(-1);
	}
	int ret = inode_length(file_get_inode(f));
	lock_release(&file_access);
	return ret;
}

int
read (int fd, void *buffer, unsigned size)
{
	if (fd == 0)
	{
		uint64_t *buf = (uint64_t *) buffer;
		unsigned iRead=0;
		while (iRead < size)
		{
			buf[iRead] = input_getc();
			iRead+=1;
		}
		return iRead;
	}
	else
	{
		lock_acquire(&file_access);
		struct file *file = search_file(fd);
		if (!file)
		{
			lock_release(&file_access);
			return -1;
		}
		int iRead = file_read(file, buffer, size); // from file.h
		lock_release (&file_access);
		return iRead;
	}
}

int write (int fd, const void *buffer, unsigned size)
{
    if (fd == 1)
    {
      putbuf (buffer, size); // from stdio.h
      return size;
    }

    lock_acquire(&file_access);
    struct file *file = search_file(fd);
    if (!file)
    {
      lock_release(&file_access);
      return -1;
    }
    int iWrite = file_write(file, buffer, size); // file.h
    lock_release (&file_access);
    return iWrite;
}

void seek (int fd, unsigned position)
{
  struct file *file = search_file(fd);
  if (!file) return;
  file_seek (file, position);  
}

unsigned tell (int fd)
{
  struct file *file = search_file (fd);
  if (!file) exit (-1);
  return file_tell (file);
}

void close (int fd)
{ 
	lock_acquire(&file_access);
  	delete_file (fd);
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
			assert_valid_useraddr(f->R.rdi);
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			// f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			assert_valid_useraddr(f->R.rdi);
			assert_valid_useraddr(f->R.rsi);
			assert_valid_useraddr(f->R.rdx);
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			assert_valid_useraddr(f->R.rdi);
			assert_valid_useraddr(f->R.rsi);
			assert_valid_useraddr(f->R.rdx);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			assert_valid_useraddr(f->R.rdi);
			assert_valid_useraddr(f->R.rsi);
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			assert_valid_useraddr(f->R.rdi);
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			assert_valid_useraddr(f->R.rdi);
			close(f->R.rdi);
			break;
	}
	/* END */

	
}


