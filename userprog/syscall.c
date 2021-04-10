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

/* Copy file structure */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};

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
/* Functions used in file related syscalls */
int allocate_fd (struct file *f) {
	struct file_info *fi = malloc(sizeof(struct file_info));
	if (fi == NULL) return -1;
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
/* End for functions used in file related syscalls */

void assert_valid_useraddr(const void *vaddr) {
   if (!is_user_vaddr(vaddr))  exit(-1);
}

/* Start of syscall functions used in syscall handler */
void halt (void) {
	power_off();
}

void exit(int status) {
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
}

int wait(pid_t pid) {
	return process_wait(pid);
}

pid_t exec (const char *file) {
	int ret;
	ret = process_exec(file);
	return ret;
}

pid_t sys_fork (const char *thread_name, struct thread_and_if *tif) {
	pid_t pid;
	struct thread *child;

	if((pid = process_fork(thread_name, tif)) == PID_ERROR)
		return PID_ERROR;
	
	child = thread_get_child(pid);
	sema_down(&child->filecopy_sema);

	/* File copy is ended from now on */
	if(!child->filecopy_success)
		return PID_ERROR;
	
	return pid;
}

bool create(const char *file, unsigned initial_size) {
	if (file == NULL) exit(-1);
	if (strlen(file) == 0) return false;
	return filesys_create(file, initial_size);
}

bool remove(const char *file) {
	if (file == NULL) return false;
	return filesys_remove(file);
}

int open(const char *file) {
	if (!file) exit(-1);
	lock_acquire(&file_access);
	struct file *f = filesys_open(file); 
	if (f == NULL) 
	{
		lock_release(&file_access);
		return -1;
	}
	int fd = allocate_fd(f);
	lock_release(&file_access);
	if (fd == -1)
	{
		file_close(f);
		exit(-1);
	}
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
	int ret = file_length(f);
	lock_release(&file_access);
	return ret;
}

int
read (int fd, void *buffer, unsigned size)
{
	lock_acquire(&file_access);
	if (fd == 0)
	{
		uint64_t *buf = (uint64_t *) buffer;
		unsigned iRead=0;
		while (iRead < size)
		{
			buf[iRead] = input_getc();
			iRead+=1;
		}
		lock_release(&file_access);
		return iRead;
	}
	else
	{
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
	lock_acquire(&file_access);
    if (fd == 1)
    {
      putbuf (buffer, size); // from stdio.h
	  lock_release(&file_access);
      return size;
    }

    struct file *file = search_file(fd);
    if (!file)
    {
      lock_release(&file_access);
      return -1;
    }
	if (file->deny_write)
		file_deny_write(file);
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
/*
int dup2 (int oldfd, int newfd)
{
	struct file *oldfile = search_file(oldfd);
	struct file *newfile = search_file(newfd);
	if (oldfd < 0 || oldfd >= thread_current()->next_fd)
		return -1;
	if (newfile != NULL)
	{
		close(newfile);
	}
	newfile = oldfile;
	return newfd;
}
*/

/* END */

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	/* Our Implementation */
	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			assert_valid_useraddr(f->R.rdi);
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			assert_valid_useraddr(f->R.rdi);
			/* For sys_fork() only */
			struct thread_and_if *tif = malloc(sizeof(struct thread_and_if));
			if (tif == NULL) 
			{
				f->R.rax = TID_ERROR;
				break;
			}
			tif->t = thread_current();
			tif->if_ = malloc(sizeof(struct intr_frame));
			if (tif->if_ == NULL)
			{
				free(tif->if_);
				free(tif);
				f->R.rax = TID_ERROR;
				break;
			}
			memcpy(tif->if_, f, sizeof(struct intr_frame));
			f->R.rax = sys_fork(f->R.rdi, tif);
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
		/*
		case SYS_DUP2:
			assert_valid_useraddr(f->R.rdi);
			assert_valid_useraddr(f->R.rsi);
			f->R.rax = dup2(f->R.rdi, f->R.rsi);
			break;
		*/
		/*
		case default:
			printf("Unknown syscall\n");
			break;
		*/
	}
	/* END */
}


