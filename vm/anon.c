/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
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

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
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
	/* END */

	/* What is the return value? */
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	return false;
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
