#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"
#include "lib/kernel/list.h"

/* Our Implementation */
// For passing if_ to sys_fork
struct thread_and_if {
	struct thread *t;
	struct intr_frame *if_;
};
// END

struct file_info {
    struct file *file;          // target file
    int fd;                     // located file descriptor
    struct list_elem file_elem; // file_list element
};

struct lock file_access;

void syscall_init (void);

#endif /* userprog/syscall.h */
