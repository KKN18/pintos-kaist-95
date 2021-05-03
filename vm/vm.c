/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/synch.h"
/* Our Implementation */
#include "vm/uninit.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#define LOG 1
/* END */

static struct lock vm_lock;
static struct lock eviction_lock;

/* Our Implementation */
static bool add_map (struct page *page, void *kva)
{
	if(LOG)
		printf("add_map for page(0x%lx), kva: 0x%lx\n", page->va, kva);
	uint64_t *pml4 = thread_current()->pml4;
	bool res = install_page(page->va, kva, true);
	return res;
}

/* CODYJACK */
/* Functionality required by hash table*/
unsigned
suppl_pt_hash (const struct hash_elem *he, void *aux UNUSED)
{
  const struct page *page;
  page = hash_entry (he, struct page, elem);
  return hash_bytes (&page->va, sizeof(page->va));
}

/* Functionality required by hash table*/
bool
suppl_pt_less (const struct hash_elem *hea,
               const struct hash_elem *heb,
	       void *aux UNUSED)
{
  const struct page *pagea;
  const struct page *pageb;
 
  pagea = hash_entry (hea, struct page, elem);
  pageb = hash_entry (heb, struct page, elem);

  return (pagea->va - pageb->va) < 0;
}
/* END */

static const struct page_operations page_op = {
	.swap_in = add_map
};

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init (&vm_frames);
	lock_init (&vm_lock);
	lock_init (&eviction_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;

	/* Check wheter the upage is already occupied or not. */
	if ((page = spt_find_page (spt, upage)) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		page = palloc_get_page(PAL_USER | PAL_ZERO);
		if (VM_TYPE(type) == VM_ANON)
			uninit_new(page, upage, init, type, aux, anon_initializer);
		else // VM_TYPE(type) == VM_FILE
			uninit_new(page, upage, init, type, aux, file_backed_initializer);

		/* TODO: Insert the page into the spt. */
		spt_insert_page (spt, page);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	if(LOG)
	{
		printf("spt_find_page: 0x%lx\n", va);
	}
	struct page page;
	/* TODO: Fill this function. */
	/* CODYJACK */
	struct hash ht = spt->hash_table;
	struct hash_elem *e;

	page.va = va;
	ASSERT (pg_ofs (page.va) == 0);
	if(LOG)
		printf("	Before find:\n");
	e = hash_find(&ht, &page.elem);
	if(LOG)
	{
		printf("	After find: ");
		if(e != NULL)
			printf("Found it.\n");
		else
			printf("Not found.\n");
	}
		
	return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	/* CODYJACK */
	struct hash_elem *result;
	if (page == NULL)
		return succ;
	
	ASSERT (pg_ofs (page->va) == 0);
	result = hash_insert(&spt->hash_table, &page->elem);
	if (result != NULL)
		return succ;
	if(LOG)
		printf("spt_insert_page: 0x%lx\n", page->va);
	succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	if(LOG)
	{
		printf("vm_get_frame\n");
	}
	/* TODO: Fill this function. */
	/* Not sure about this flag */
	frame = palloc_get_page (PAL_USER | PAL_ZERO);
	if (frame != NULL)
	{
		frame->tid = thread_current()->tid;
		lock_acquire (&vm_lock);
		list_push_back (&vm_frames, &frame->elem);
		lock_release (&vm_lock);
	}
	else
	{
		palloc_free_page(frame);
		PANIC ("todo");
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct thread *t = thread_current();
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// ASSERT(addr != NULL);
	if(LOG)
	{
		printf("\nvm_try_handle_fault: Page fault in (%s)\n", thread_name());
		printf("	Fault addr: 0x%lx\n", addr);
		printf("	Fault page: 0x%lx\n", pg_round_down(addr));
	}
	page = spt_find_page(spt, pg_round_down(addr));	
	if(LOG)
	{
		if(page != NULL)
			printf("	Found fault page in spt\n");
		else
			printf("	Not found in spt\n");
	}
	// Same with vm_do_claim_page
	struct frame *frame = vm_get_frame();
	if (frame == NULL) return false;
	if (page == NULL) return false;
	/* Set links */
	frame->page = page;
	frame->kva = frame;
	page->frame = frame;
	// Call lazy_load_segment
	swap_in (page, frame->kva);
	if(LOG)
		printf("	Lazy_load_segment return\n");
	bool res = install_page(page->va, frame, true);
	// hash_replace(&spt->hash_table, &page->elem);
	return res;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	if(LOG)
	{
		printf("vm_claim_page on va: 0x%lx\n", va);
	}
	struct page *page = palloc_get_page(PAL_USER | PAL_ZERO);
	/* TODO: Fill this function */
	page->va = va;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	if(LOG)
		printf("vm_do_claim_page\n");
	struct frame* frame = vm_get_frame();
	if (frame == NULL) 
		return false;
	if (page == NULL) 
		return false;
	/* Set links */
	frame->page = page;
	frame->kva = frame;
	page->frame = frame;
	page->operations = &page_op;
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* CODYJACK */
	hash_init (&spt->hash_table, suppl_pt_hash, suppl_pt_less, NULL);
	return;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
