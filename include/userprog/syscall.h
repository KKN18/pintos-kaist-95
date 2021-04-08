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
//

struct lock file_access;

void syscall_init (void);

#endif /* userprog/syscall.h */
