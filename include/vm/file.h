#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	struct file *file;
	size_t page_read_bytes;
	bool writable;
	off_t offset;
	
};

/* Ryu */
struct mmap_file {
    void *va;				//thread.h, thread.c nextmapid, mmaplist 이름 바꾸기
    struct list_elem elem;
    struct list page_list;
};


void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
