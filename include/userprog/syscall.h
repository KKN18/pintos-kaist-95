#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"
#include "lib/kernel/list.h"



// Renamed Implementation
struct file_info {
    struct file *file;          // target file
    int fd;                     // located file descriptor
    struct list_elem file_elem; // file_list element
};
// END

void syscall_init (void);

#endif /* userprog/syscall.h */
