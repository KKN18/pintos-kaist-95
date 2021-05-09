#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;
typedef int32_t off_t;

struct anon_page {
    
    struct file *file;
    size_t page_read_bytes;
    bool writable;
    off_t offset;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

/* Our Implementation */
void _anon_destroy (struct page *page);

#endif
