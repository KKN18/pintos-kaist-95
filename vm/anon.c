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

/* Bitmap of swap slot availablities and corresponding lock */
static struct bitmap *swap_table;
static const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;


/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	if(swap_disk == NULL)
		PANIC("no swap disk");

	swap_table = bitmap_create(disk_size(swap_disk) / SECTORS_PER_PAGE);
	if(swap_table == NULL)
		PANIC("no swap table");


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
	anon_page->swap_index = -1;
	/* END */

	/* What is the return value? */
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t swap_index = anon_page->swap_index;

	// Check the swap region
	if(bitmap_test(swap_table, swap_index) == true) {
		// Still avaliable slot, error
		PANIC("Error, invalid read access to unassigned swap block");
	}

	size_t i;
	for(i = 0; i < SECTORS_PER_PAGE; i++) {
		disk_read(swap_disk, swap_index * SECTORS_PER_PAGE + i,
									kva + (DISK_SECTOR_SIZE * i));
	}

	bitmap_set(swap_table, swap_index, true);
	page->is_swapped = false;
	install_page(page->va, kva, anon_page->writable);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t swap_index = bitmap_scan(swap_table, 0, 1, true);

	if(swap_index == BITMAP_ERROR)
	{
		PANIC("No more swap slot");
	}

	size_t i;
	for(i = 0; i < SECTORS_PER_PAGE; i++) {
		disk_write(swap_disk, swap_index * (SECTORS_PER_PAGE) + i,
						(page->frame->kva) + (DISK_SECTOR_SIZE * i));
	}
	bitmap_set(swap_table, swap_index, false);
	anon_page->swap_index = swap_index;
	page->is_swapped = true;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if(page->is_loaded == true)
	{
		// file_close(file);
	}
}

/* Our Implementation of non-static version */
void _anon_destroy (struct page *page) {
	return anon_destroy(page);
}
