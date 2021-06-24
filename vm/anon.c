/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "threads/mmu.h"
#define LOG 0

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
   .swap_in = anon_swap_in,
   .swap_out = anon_swap_out,
   .destroy = anon_destroy,
   .type = VM_ANON,
};

static size_t PAGE_PER_DISK = PGSIZE / DISK_SECTOR_SIZE;
static struct bitmap *swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
   /* TODO: Set up the swap_disk. */
   swap_disk = disk_get(1, 1);
   if(swap_disk == NULL)
      exit(-1);

   // Handle swap_disk with bitmap
   size_t bit_cnt = disk_size(swap_disk) / PAGE_PER_DISK;
   
   // There is no swapped file at the beginning
   swap_table = bitmap_create(bit_cnt);
   if(swap_table == NULL)
      exit(-1);

   return;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
   /* Set up the handler */
   if(LOG)
   {
      printf("anon_initializer\n");
      printf("   page->va: 0x%lx, kva: 0x%lx\n", page->va, kva);
   }

   page->operations = &anon_ops;
   /* Our Implementation */
   ASSERT(VM_TYPE(type) == VM_ANON);
   page->type = type;
   page->is_loaded = false;
   /* END */

   struct anon_page *anon_page = &page->anon;
   
   /* Our Implementation */
   anon_page->page_read_bytes = 0;
   anon_page->writable = true;
   anon_page->offset = 0;

   // For swap
   anon_page->swap_loc = -1;
   /* END */

   return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
   struct anon_page *anon_page = &page->anon;

   disk_sector_t swap_loc = anon_page->swap_loc;
   size_t start = swap_loc * PAGE_PER_DISK;
   // Read page from each disk sector
   for(size_t i = 0; i < PAGE_PER_DISK; i++) {
      disk_read(swap_disk, start + i,   kva + (DISK_SECTOR_SIZE * i));
   }

   // The file that was in this location is swapped in, so we can use this bit now
   bitmap_set(swap_table, swap_loc, false);

   // Mapping in physical memory
   install_page(page->va, kva, anon_page->writable);
   
   return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
   struct anon_page *anon_page = &page->anon;

   size_t start = 0;
   int cnt = 1;
   bool value = false;
   disk_sector_t swap_loc = bitmap_scan(swap_table, start, cnt, value); 
   ASSERT(swap_loc < BITMAP_ERROR);

   size_t write_start = swap_loc * PAGE_PER_DISK;
   // Write page to each disk sector
   for(size_t i = 0; i < PAGE_PER_DISK; i++) {
      disk_write(swap_disk, write_start + i,
                  (page->frame->kva) + (DISK_SECTOR_SIZE * i));
   }

   // The file is swapped out to this location, so we can't use this bit now
   bitmap_set(swap_table, swap_loc, true);
   // Save swap_loc for later swap_in
   anon_page->swap_loc = swap_loc;

   return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
   struct anon_page *anon_page = &page->anon;
   // Nothing to do
   // No dynamically allocated memory
}

/* Our Implementation of non-static version */
void _anon_destroy (struct page *page) {
   return anon_destroy(page);
}