/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/synch.h"
/* Our Implementation */
#include "vm/uninit.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#define LOG 0

#include "lib/kernel/hash.h"
#define list_elem_to_hash_elem(LIST_ELEM)                       \
   list_entry(LIST_ELEM, struct hash_elem, list_elem)

/* For synchronization */
static struct lock vm_lock;
static struct lock swap_lock;
static struct lock copy_lock;

/* Frame list */
static struct list vm_frames;

/* Our Implementation */
static bool add_map (struct page *page, void *kva)
{
	if(LOG)
		printf("add_map for page(0x%lx), kva: 0x%lx\n", page->va, kva);
	uint64_t *pml4 = thread_current()->pml4;
	bool writable = true;
	if(page->type == VM_ANON)
		writable == page->anon.writable;
	bool res = install_page(page->va, kva, writable);
	return res;
}

/* Functionality required by hash table*/
uint64_t
spt_hash (const struct hash_elem *he, void *aux UNUSED)
{
  struct page *page;
  page = hash_entry (he, struct page, elem);
  return hash_bytes (&page->va, sizeof(page->va));
}

/* Functionality required by hash table*/
bool
spt_less (const struct hash_elem *hea,
               const struct hash_elem *heb,
	       void *aux UNUSED)
{
  struct page *pagea;
  struct page *pageb;
 
  pagea = hash_entry (hea, struct page, elem);
  pageb = hash_entry (heb, struct page, elem);

  return pagea->va < pageb->va;
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
	lock_init (&swap_lock);
	lock_init (&copy_lock);
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

/* Our Implementation */
static struct frame *vm_frame_next(void);

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
		page = (struct page *)malloc(sizeof(struct page));
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
	struct hash_elem *result;
	if (page == NULL)
		return succ;
	
	ASSERT (pg_ofs (page->va) == 0);
	ASSERT (spt_find_page(spt, page->va) == NULL);

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
	hash_delete(&spt->hash_table, &page->elem);
	vm_dealloc_page (page);
	return true;
}
/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	struct thread *t;
	struct list_elem *vm_frames_ptr = list_front(&vm_frames);
	 /* TODO: The policy for eviction is up to you. */

	/* Our Policy */
	/* At the moment when we need victim, we search frames with the order they pushed 
	   to the frame list. If the frame is not recently accessed, evict it. 
	   If the frame is recently accessed, set the access bit false.
	   Even though we searched whole items in the list but nothing evicted, 
	   we can get victim in second search because we set the access bit false previously. */

	size_t len = list_size(&vm_frames); 
	for(int i = 0; ; i++)
	{
		// Behaves like circular linked list
		if (vm_frames_ptr == NULL || vm_frames_ptr == list_back(&vm_frames))
			vm_frames_ptr = list_front (&vm_frames);
		else
			vm_frames_ptr = list_next (vm_frames_ptr);
		
		victim = list_entry(vm_frames_ptr, struct frame, elem);
		// Stack is not a candidate for eviction
		if (victim->page->type == VM_MARKER_0)
		{
			continue;
		}
	
		t = thread_get_by_id(victim->tid);
	
		// printf("VA 0x%lx ", victim->page->va);
		if(pml4_is_accessed(t->pml4, victim->page->va))
			pml4_set_accessed(t->pml4, victim->page->va, false);	// Set Access bit to false
		else	// If the frame is not recently accessed, evict it
			break;
		
		// Avoid possible infinite loop
		if(i > len * 10)
			PANIC("Eviction may have caused infinite loop");
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	struct thread *t = thread_get_by_id(victim->tid);
	/* TODO: swap out the victim and return the evicted frame. */
	ASSERT(victim != NULL && t != NULL);
	pml4_clear_page(t->pml4, victim->page->va);		// Remove the map between VA and KVA of the frame

	// Call swap_out
	lock_acquire(&swap_lock);
	swap_out(victim->page);
	lock_release(&swap_lock);

	return victim;
}


/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	if(LOG)
	{
		printf("vm_get_frame\n");
	}
	/* TODO: Fill this function. */
	struct frame *frame = calloc(1, sizeof *frame);
	
	if(frame == NULL)
	{
		free(frame);
		return NULL;
	}
	
	uint8_t *kva = palloc_get_page (PAL_USER | PAL_ZERO);
	
	if (kva != NULL)
	{
		frame->kva = kva;
		frame->tid = thread_current()->tid;
		lock_acquire (&vm_lock);
		list_push_back (&vm_frames, &frame->elem);
		lock_release (&vm_lock);
	}
	else
	{
		free(frame);
		frame = vm_evict_frame();
		frame->tid = thread_current()->tid;
	}
	ASSERT (frame != NULL);
	return frame;
}

/* Growing the stack. */
void
vm_stack_growth (void *addr UNUSED) {
	struct thread *t = thread_current();
	if(vm_claim_page(pg_round_down(addr)))
	{
		struct page *page = spt_find_page(&t->spt, pg_round_down(addr));
		page->type = VM_MARKER_0;
		page->is_loaded = true;
		return true;
	}
	return false;
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
	/* Page fault is TRUE page fault */
  	if (addr == NULL || !not_present || !is_user_vaddr(addr))
	{
		ASSERT(0);
		exit(-1);
	}

	if(LOG)
	{
		printf("\nvm_try_handle_fault: Page fault in (%s)\n", thread_name());
		printf("	Fault addr: 0x%lx\n", addr);
		printf("	Fault page: 0x%lx\n", pg_round_down(addr));
	}

	page = spt_find_page(spt, pg_round_down(addr));

	if(page == NULL)
	{
		if (addr < USER_STACK && addr > USER_STACK - (1 << 20))  // Stack size 1MB
		{
			if (f->rsp == addr + 8 || addr > f->rsp)
			{
				vm_stack_growth(pg_round_down(addr));
				return true;
			}
		}
		ASSERT(0);
		exit(-1);
	}
	
	if(LOG)
	{
		if(page != NULL)
			printf("	Found fault page in spt\n");
		else
			printf("	Not found in spt\n");
	}

	bool res = false;
	
	bool holdlock = lock_held_by_current_thread(&file_access);
	if (!holdlock)
	{
		lock_acquire(&file_access);
	}
	res = vm_do_claim_page(page);
	if (!holdlock)
	{
		lock_release(&file_access);
	}
	if(res)
		page->is_loaded = true;

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
	struct page *page = (struct page *)malloc(sizeof(struct page));
	ASSERT(page != NULL);
	/* TODO: Fill this function */
	page->va = va;
	page->operations = &page_op;	// When page calls swap_in, it goes to add_map
	spt_insert_page (&thread_current()->spt, page);
	lock_acquire(&file_access);
	bool ret = vm_do_claim_page (page);
	lock_release(&file_access);
	return ret;
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	if(LOG)
		printf("vm_do_claim_page\n");
	struct frame* frame = vm_get_frame();
	if (frame == NULL)
	{
		/* Free page in vm_claim_page */
		free(page);
		return false;
	}
	if (page == NULL) 
		return false;
	
	/* Set links */
	frame->page = page;
	page->frame = frame;
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->hash_table, spt_hash, spt_less, NULL);
	return;
}

static void copy_page (struct hash_elem *e, struct supplemental_page_table *dst)
{
	lock_acquire(&copy_lock);
	struct thread *t = thread_current();
	struct page *page = hash_entry(e, struct page, elem);
	struct page *newpage = (struct page *)malloc(sizeof(struct page));

	memcpy(newpage, page, sizeof(struct page));

	if (newpage == NULL) {
		free(newpage);
		PANIC("not enough memory");
	}
	ASSERT(page != NULL);

	/* Insert to child's spt */
	/* Only allocate physical memory if loaded */
	spt_insert_page(dst, newpage);
	newpage->operations = &page_op;
	lock_acquire(&file_access);
	vm_do_claim_page(newpage);
	lock_release(&file_access);
	if(page->is_loaded)
	{
		/* Copy physical memory */
		memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);
	}
	lock_release(&copy_lock);
	return;
}

static void kill_page (struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, elem);
	destroy(page);
	free(page);
	return;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
      struct supplemental_page_table *src UNUSED) {
	ASSERT(dst->hash_table.elem_cnt == 0);
	size_t i;
	struct hash *h = &src->hash_table;
	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			copy_page (list_elem_to_hash_elem (elem), dst);
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash *h = &spt->hash_table;
	struct list *buckets = h->buckets;
	size_t i;
	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		while (!list_empty (bucket)) {
			struct list_elem *list_elem = list_pop_front (bucket);
			struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
			kill_page (hash_elem, h->aux);
		}
		list_init (bucket);
	}
	h->elem_cnt = 0;
	return;
}
