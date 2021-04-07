#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"
#include "lib/kernel/list.h"



struct lock file_access;

void syscall_init (void);

#endif /* userprog/syscall.h */
