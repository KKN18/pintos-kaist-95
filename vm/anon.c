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
	swap_table = bitmap_create(bit_cnt);
	if(swap_table == NULL)
		exit(-1);

	// We can use any spot
	bitmap_set_all(swap_table, true);
	return;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	if(LOG)
	{
		printf("anon_initializer\n");
		printf("	page->va: 0x%lx, kva: 0x%lx\n", page->va, kva);
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
		disk_read(swap_disk, start + i,	kva + (DISK_SECTOR_SIZE * i));
	}

	// Set avaliability as true (We can use this index in the future.)
	bitmap_set(swap_table, swap_loc, true);
	page->is_swapped = false;

	// Mapping in physical memory
	install_page(page->va, kva, anon_page->writable);
	
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t swap_loc = bitmap_scan(swap_table, 0, 1, true); // start = 0, cnt = 1, value = true
	ASSERT(swap_loc < BITMAP_ERROR);

	size_t start = swap_loc * PAGE_PER_DISK;
	// Write page to each disk sector
	for(size_t i = 0; i < PAGE_PER_DISK; i++) {
		disk_write(swap_disk, start + i,
						(page->frame->kva) + (DISK_SECTOR_SIZE * i));
	}

	// Set avaliability as false.
	bitmap_set(swap_table, swap_loc, false);
	// Save swap_loc for later swap_in
	anon_page->swap_loc = swap_loc;
	page->is_swapped = true;

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
