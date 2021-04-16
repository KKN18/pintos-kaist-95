#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/syscall.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct thread_and_if *tif UNUSED);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
struct thread *find_child (tid_t tid);

#endif /* userprog/process.h */
