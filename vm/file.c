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
   // Our implementation doesn't require this function
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
   file_read_at(file_page->file, kva, file_page->page_read_bytes, file_page->offset);
   memset (kva + file_page->page_read_bytes, 0, PGSIZE - file_page->page_read_bytes);
   page->is_loaded = true;
   // printf("FILE SWAP IN VA 0x%lx KVA 0x%lx\n", page->va, kva);
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
   page->is_loaded = false;
   // printf("FILE SWAP OUT VA 0x%lx KVA 0x%lx\n", page->va, page->frame->kva);
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


struct mmap_va *find_mmap_file (void *addr) {
   struct thread *t = thread_current();
   struct list *mmap_list = &t->mmap_list;
   struct list_elem *e = list_begin(mmap_list);
   struct list_elem *nexte;
   for (e; e != list_end(mmap_list); e = nexte)
   {
      nexte = list_next(e);
      struct mmap_va *mmap_va = list_entry (e, struct mmap_va, mmaplist_elem);
      ASSERT(mmap_va != NULL);
      if (mmap_va->start_va == addr)
         return mmap_va;
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
   uint8_t *map_addr = addr;
   struct mmap_va *mmap_va = (struct mmap_va *)malloc(sizeof(struct mmap_va));
   if (mmap_va == NULL) return NULL;
   memset(mmap_va, 0, sizeof(struct mmap_va));
   mmap_va->start_va = addr;
   list_push_back (&thread_current()->mmap_list, &mmap_va->mmaplist_elem);
   list_init(&mmap_va->page_list);
   struct file *refile;
   while(length > 0) {
      size_t read_bytes = length < PGSIZE ? length : PGSIZE;
      size_t zero_bytes = PGSIZE - read_bytes;
      // printf("addr 0x%lx pg_round 0x%lx\n", addr, pg_round_down(addr));
      struct temp *temp = (struct temp*) malloc(sizeof(struct temp));
      refile = file_reopen(file);
      temp->file = refile;
      ASSERT(temp->file != NULL);
      temp->page_read_bytes = read_bytes;
      temp->writable = writable;
      temp->offset = offset;
      
      // printf("MMAP VA 0x%lx\n", addr);
      if (!vm_alloc_page_with_initializer (VM_FILE, addr, 
         writable, lazy_load_segment, temp))
         return NULL;

      struct page* page = spt_find_page(&thread_current()->spt, addr); 
      ASSERT(page != NULL);
      list_push_back(&mmap_va->page_list, &page->mmap_elem);

      length -= read_bytes;
      addr += PGSIZE;
      offset += read_bytes;
   }
   return map_addr; 
}


/* Do the munmap */
void
do_munmap (void *addr) {
   struct mmap_va *mmap_va = find_mmap_file(addr);
   if (mmap_va == NULL) exit(-1);
   struct list *page_list = &mmap_va->page_list;
   struct list_elem *e = list_begin(page_list);
   struct list_elem *nexte;
   for (e; e != list_end(page_list); e = nexte)
   {
      nexte = list_next(e);
      struct page *page = list_entry(e, struct page, mmap_elem);
      list_remove(e);
      if (page->is_loaded)
      {
         struct list_elem *frame_elem = &page->frame->elem;
         list_remove(frame_elem);
      }
      spt_remove_page(&thread_current()->spt, page);
   }
   list_remove (&mmap_va->mmaplist_elem);
   free(mmap_va);
}