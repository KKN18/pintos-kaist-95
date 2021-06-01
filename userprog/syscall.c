#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "userprog/process.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* Our Implementation */
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "vm/file.h"
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

struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */

	uint32_t unused[124];               /* Not used. */

	bool is_dir;						/* This is directory or not */
};

struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
	/* Our Implementation */
	struct lock *fat_lock;				/* Lock when accessing FAT */
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

void assert_valid_useraddr(const void *vaddr, int8_t *rsp) {
   if (!is_user_vaddr(vaddr))  exit(-1);
   if (!spt_find_page(&thread_current()->spt, pg_round_down(vaddr))) // If the address and rsp value is in stack grow condition, grow it here because we know the rsp value
   {
       if (vaddr < USER_STACK && vaddr > USER_STACK - (1 << 20))  // Stack size is 1MB
      {
         if (rsp == vaddr + 8 || vaddr > rsp)   
         {
            vm_stack_growth(pg_round_down(vaddr));
            return true;
         }
      }
   }
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
   return process_exec(file);
}

pid_t sys_fork (const char *thread_name, struct thread_and_if *tif) {
   pid_t pid;
   struct thread *child;

   pid = process_fork(thread_name, tif);

   if(pid == PID_ERROR)
      return PID_ERROR;
   
   child = find_child(pid); //find child which has tid same with pid
   if(child == NULL)
      return PID_ERROR;
    
   sema_down(&child->filecopy_sema);

   /* File copy is ended from now on */
   if(!child->filecopy_success)
      return PID_ERROR;
   
   return pid;
}

bool create(const char *file, unsigned initial_size) {
   if (file == NULL) exit(-1);
   if (strlen(file) == 0) return false;
   lock_acquire(&file_access);
   bool ret = filesys_create(file, initial_size);
   lock_release(&file_access);
   return ret;
}

bool remove(const char *file) {
   if (file == NULL) return false;
   lock_acquire(&file_access);
   bool ret = filesys_remove(file);
   lock_release(&file_access);
   return ret;
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

   // Wookayin
   // directory handling
   struct inode *inode = file_get_inode(f);
   if(inode != NULL && inode_is_dir(inode)) {
      dir_open( inode_reopen(inode) );
   }

   int fd = allocate_fd(f);
   if (fd == -1)
   {
      file_close(f);
      lock_release(&file_access);
      exit(-1);
   }
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
   int ret = file_length(f);
   lock_release(&file_access);
   return ret;
}

static void not_code_segment(void *addr)
{
   if (0x400000 < addr && addr < 0x400000 + PGSIZE)
      exit(-1);
}

int
read (int fd, void *buffer, unsigned size)
{
   not_code_segment(buffer);
   lock_acquire(&file_access);
   if (fd == 0) //STDIN
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
      // If this is directory, read not allowed.
      if (inode_is_dir(file->inode))
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
    if (fd == 1) //STDOUT
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
    if (inode_is_dir(file->inode))
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
  lock_acquire(&file_access);
  file_seek (file, position);
  lock_release(&file_access);  
}

unsigned tell (int fd)
{
   struct file *file = search_file (fd);
   if (!file) exit (-1);
   lock_acquire(&file_access);
   unsigned res = file_tell (file);
   lock_release(&file_access);
   return res;
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

void *call_mmap (void *addr, size_t length, int writable, int fd, off_t offset) 
{
   if (!is_user_vaddr(addr) || !is_user_vaddr(length) || !is_user_vaddr(writable) || !is_user_vaddr(fd) || !is_user_vaddr(offset))
      return NULL;
   if (fd == 0 || fd == 1)
      return NULL;
   struct file *file = search_file(fd);
   if (file == NULL)
      return NULL;
   lock_acquire(&file_access);
   uint8_t *res = do_mmap(addr, length, writable, file, offset);
   lock_release(&file_access);
   return res;
}

/* System calls for Project #4 */ 
// RYU
bool chdir (const char *dirname) 
{
   struct dir *dir = dir_open_path (dirname);

   if(dir == NULL) {
      return false;
   }
   lock_acquire(&file_access);
   // switch CWD
   dir_close (thread_current()->working_dir);
   thread_current()->working_dir = dir;
   lock_release(&file_access);
   return true;
}

bool mkdir (const char *dir) 
{
   lock_acquire(&file_access);
   int ret = filesys_create_dir(dir);
   lock_release(&file_access);
   return ret;
}

bool readdir (int fd, char *name) 
{
   lock_acquire(&file_access);
   struct file *f = search_file (fd);
   if (f == NULL)
      exit (-1);
   // 내부 아이노드 가져오기 및 디렉터리 열기
   struct inode *inode = file_get_inode (f);
   if (!inode || !inode_is_dir (inode))
   {
      lock_release(&file_access);
      return false;
   }

   struct dir *dir = dir_open (inode);
   if (!dir)
   {
      lock_release(&file_access);
      return false;
   }
      
   int i;
   bool result = true;
   result = dir_readdir (dir, name);
   lock_release(&file_access);
   return result;
}


bool isdir (int fd) 
{
   lock_acquire(&file_access);
   /* RYU */
   struct file *f = search_file(fd);

   if(f == NULL)
      exit(-1);

   bool ret = inode_is_dir(file_get_inode(f));
   lock_release(&file_access);
   return ret;
}

int inumber (int fd)
{
   lock_acquire(&file_access);
   struct file *f = search_file (fd);
   if (f == NULL)
      exit (-1);
   
   disk_sector_t ret = inode_get_inumber(file_get_inode(f));
   lock_release(&file_access);
   return ret;
}
int symlink (const char* target, const char* linkpath)
{
   struct thread *t = thread_current();
   struct sym_link *sym = (struct sym_link *)malloc(sizeof(struct sym_link));
   if (linkpath[0] == '/') linkpath = linkpath + 1;
   strlcpy(sym->linkpath, linkpath, PATH_MAX_LEN + 1);
   strlcpy(sym->path, target, PATH_MAX_LEN + 1);
   list_push_back(&t->sym_list, &sym->sym_elem);

   return 0;
}

void syscall_print(int n)
{
   switch (n)
   {
      case SYS_HALT:
         printf("SYS_HALT\n");
         break;
      case SYS_EXIT:
         printf("SYS_EXIT\n");
         break;
      case SYS_FORK:
         printf("SYS_FORK\n");
         break;
      case SYS_EXEC:
         printf("SYS_EXEC\n");
         break;
      case SYS_WAIT:
         printf("SYS_WAIT\n");
         break;
      case SYS_CREATE:
         printf("SYS_CREATE\n");
         break;
      case SYS_REMOVE:
         printf("SYS_REMOVE\n");
         break;
      case SYS_OPEN:
         printf("SYS_OPEN\n");
         break;
      case SYS_FILESIZE:
         printf("SYS_FILESIZE\n");
         break;
      case SYS_READ:
         printf("SYS_READ\n");
         break;
      case SYS_WRITE:
         printf("SYS_WRITE\n");
         break;
      case SYS_SEEK:
         printf("SYS_SEEK\n");
         break;
      case SYS_TELL:
         printf("SYS_TELL\n");
         break;
      case SYS_CLOSE:
         printf("SYS_CLOSE\n");
         break;
      case SYS_MMAP:
         printf("SYS_MMAP\n");
         break;
      case SYS_MUNMAP:
         printf("SYS_MUMMAP\n");
         break;
      case SYS_CHDIR:
         printf("SYS_CHDIR\n");
         break;
      case SYS_MKDIR:
         printf("SYS_MKDIR\n");
         break;
      case SYS_READDIR:
         printf("SYS_READDIR\n");
         break;
      case SYS_ISDIR:
         printf("SYS_ISDIR\n");
         break;
      case SYS_INUMBER:
         printf("SYS_INUMBER\n");
         break;
      case SYS_SYMLINK:
         printf("SYS_SYMLINK\n");
         break;
   }
}

/* END */

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
   // TODO: Your implementation goes here.
   /* Our Implementation */
   // syscall_print(f->R.rax);
   //printf("MUNMAP : %d, syscall %d\n", SYS_MUNMAP, f->R.rax);
   switch (f->R.rax) {
      case SYS_HALT:
         halt();
         break;
      case SYS_EXIT:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         exit(f->R.rdi);
         break;
      case SYS_FORK:
         assert_valid_useraddr(f->R.rdi, f->rsp);
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
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = exec(f->R.rdi);
         break;
      case SYS_WAIT:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = wait(f->R.rdi);
         break;
      case SYS_CREATE:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = create(f->R.rdi, f->R.rsi);
         break;
      case SYS_REMOVE:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = remove(f->R.rdi);
         break;
      case SYS_OPEN:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = open(f->R.rdi);
         break;
      case SYS_FILESIZE:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = filesize(f->R.rdi);
         break;
      case SYS_READ:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         assert_valid_useraddr(f->R.rsi, f->rsp);
         assert_valid_useraddr(f->R.rdx, f->rsp);
         int8_t *startbuf = f->R.rsi;
         int8_t *endbuf = f->R.rsi + f->R.rdx;
         while(endbuf >= startbuf)     // To prevent page fault during the file read, grow stack properly not to confront such problem
         {
            assert_valid_useraddr(endbuf, f->rsp);
            endbuf -= PGSIZE;
         }
         f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
         break;
      case SYS_WRITE:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         assert_valid_useraddr(f->R.rsi, f->rsp);
         assert_valid_useraddr(f->R.rdx, f->rsp);
         f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
         break;
      case SYS_SEEK:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         assert_valid_useraddr(f->R.rsi, f->rsp);
         seek(f->R.rdi, f->R.rsi);
         break;
      case SYS_TELL:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = tell(f->R.rdi);
         break;
      case SYS_CLOSE:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         close(f->R.rdi);
         break;
      case SYS_MMAP:
         // assert_valid_useraddr(f->R.rdi, f->rsp);
         // assert_valid_useraddr(f->R.rsi, f->rsp);
         // assert_valid_useraddr(f->R.rdx, f->rsp);
         // assert_valid_useraddr(f->R.r10, f->rsp);
         // assert_valid_useraddr(f->R.r8, f->rsp);
         f->R.rax = call_mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
         break;
      case SYS_MUNMAP:
         // assert_valid_useraddr(f->R.rdi, f->rsp);
         do_munmap(f->R.rdi);
         break;
      case SYS_CHDIR:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = chdir(f->R.rdi);
         break;
      case SYS_MKDIR:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = mkdir(f->R.rdi);
         break;
      case SYS_READDIR:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         assert_valid_useraddr(f->R.rsi, f->rsp);
         f->R.rax = readdir(f->R.rdi, f->R.rsi);
         break;
      case SYS_ISDIR:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = isdir(f->R.rdi);
         break;
      case SYS_INUMBER:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         f->R.rax = inumber(f->R.rdi);
         break;
      case SYS_SYMLINK:
         assert_valid_useraddr(f->R.rdi, f->rsp);
         assert_valid_useraddr(f->R.rsi, f->rsp);
         f->R.rax = symlink(f->R.rdi, f->R.rsi);
         break;
      /*
      case default:
         printf("Unknown syscall\n");
         break;
      */
   }
   /* END */
}

