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
/* Our Implementation */
struct thread *find_child (tid_t tid);
bool install_page (void *upage, void *kpage, bool writable);
bool lazy_load_segment (struct page *page, void *aux);
/* END */

struct temp {
    struct file *file;
    size_t page_read_bytes;
    bool writable;
    off_t offset;
};


#endif /* userprog/process.h */
