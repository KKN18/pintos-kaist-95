/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
/* Our Implementation */
#include "userprog/process.h"
#include "threads/mmu.h"
#include "filesys/file.h"
/* END */

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
   .swap_in = file_backed_swap_in,
   .swap_out = file_backed_swap_out,
   .destroy = file_backed_destroy,
   .type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
   
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
   /* Set up the handler */
   page->operations = &file_ops;
   ASSERT(VM_TYPE(type) == VM_FILE);
   page->type = type;
   page->is_loaded = false;
   struct file_page *file_page = &page->file;
   file_page->page_read_bytes = 0;
   file_page->writable = true;
   file_page->offset = 0;
   return;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
   struct file_page *file_page UNUSED = &page->file;
   install_page(page->va, kva, file_page->writable);
   file_read_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
   return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
   struct file_page *file_page UNUSED = &page->file;
   if (pml4_is_dirty(thread_current()->pml4, page->va))
   {
      file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
   }
   pml4_set_dirty(thread_current()->pml4, page->va, false);
   return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
   struct file_page *file_page UNUSED = &page->file;
   if (page->is_loaded && pml4_is_dirty(thread_current()->pml4, page->va))
   {
      file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
   }
   page->is_loaded = false;
   file_close(file_page->file);
}

/* RYU */
struct mmap_file *find_mmap_file (void *addr) {
   struct list_elem *e;
   struct thread *t = thread_current();
   for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e))
   {
      struct mmap_file *f = list_entry (e, struct mmap_file, elem);
      if (f->va == addr)
         return f;
   }
   return NULL;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
      struct file *file, off_t offset) {
   if (addr == 0) return NULL;
   if (pg_ofs (addr) != 0) return NULL;
   if (length == 0) return NULL;
   if (find_mmap_file(addr) != NULL) return NULL;
   if (offset > PGSIZE) return NULL;
   int8_t *map_addr = addr;
   /* Ryu */
   struct mmap_file *mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
   if (mmap_file == NULL) return NULL;
   memset(mmap_file, 0, sizeof(struct mmap_file));
   list_init(&mmap_file->page_list);
   mmap_file->va = addr;

   struct file *refile;
   list_push_back (&thread_current()->mmap_list, &mmap_file->elem);
   while(length > 0) {
      size_t read_bytes = length < PGSIZE ? length : PGSIZE;
      size_t zero_bytes = PGSIZE - read_bytes;

      struct container *container = (struct container*) malloc(sizeof(struct container));
      refile = file_reopen(file);
      container->file = refile;
      ASSERT(container->file != NULL);
      container->page_read_bytes = read_bytes;
      container->writable = writable;
      container->offset = offset;

      if (!vm_alloc_page_with_initializer (VM_FILE, addr,
         writable, call_lazy_load_segment, container))
         return NULL;

      struct page* page = spt_find_page(&thread_current()->spt, addr);
      ASSERT(page != NULL);
      list_push_back(&mmap_file->page_list, &page->mmap_elem);

      length -= read_bytes;
      addr += PGSIZE;
      offset += read_bytes;
   }
   
   return map_addr; 
}


/* Do the munmap */
void
do_munmap (void *addr) {
   struct mmap_file *f = find_mmap_file(addr);
   if (f == NULL) exit(-1);
   struct list_elem *e;
   int i=0;
   for (e = list_begin(&f->page_list); e != list_end(&f->page_list);)
   {
      struct page *page = list_entry(e, struct page, mmap_elem);
      e = list_remove(e);
      spt_remove_page(&thread_current()->spt, page);
   }
   list_remove (&f->elem);
   free(f);
}