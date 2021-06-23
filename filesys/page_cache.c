/* page_cache.c: Implementation of Page Cache (Buffer Cache). */

#include "vm/vm.h"
#include "filesys/page_cache.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

#define LOG 0

/* DO NOT MODIFY this struct */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

/* The initializer of file vm */
void
page_cache_init (void) {
	/* TODO: Create a worker daemon for page cache with page_cache_kworkerd */
	if(LOG)
	{
		printf("pagecache init\n");
	}
	/* TODO: Create a worker daemon for page cache with page_cache_kworkerd */
	// page_cache_workerd = thread_create("page cache worker", PRI_DEFAULT, , )

	lock_init(&page_cache_lock);

	// Create 8 Pages (8 sectors for each page, total of 64 sectors)
	// And put it into spt (maybe call vm_alloc_page_with_initializer() here)
	// But where? What is the address for it?
	for(int i = 0; i < BUFFER_CACHE_SIZE; i++)
	{
		cache[i].occupied = false;
	}
	
	// list_init(&page_cache_list);

	return;
}

/* Initialize the page cache */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &page_cache_op;

}

/* Utilze the Swap in mechanism to implement readhead */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* Utilze the Swap out mechanism to implement writeback */
static bool
page_cache_writeback (struct page *page) {
}

/* Destory the page_cache. */
static void
page_cache_destroy (struct page *page) {
}

/* Worker thread for page cache */
static void
page_cache_kworkerd (void *aux) {
}

/* WOOKAYIN Implementation BELOW */
/**
 * An internal method for flushing back the cache entry into disk.
 * Must be called with the lock held.
 */
static void
page_cache_flush (struct page_cache_entry *entry)
{
	if(LOG)
	{
		printf("pagecache flush\n");
	}
	ASSERT(lock_held_by_current_thread(&page_cache_lock));
	ASSERT(entry != NULL && entry->occupied == true);

	if (entry->dirty) {
		disk_write (filesys_disk, entry->sec_no, entry->buffer);
		entry->dirty = false;
	}
}

void
page_cache_close (void)
{
	if(LOG)
	{
		printf("pagecache close\n");
	}
	// flush buffer cache entries
	lock_acquire (&page_cache_lock);

	size_t i;
	for (i = 0; i < BUFFER_CACHE_SIZE; ++ i)
	{
		if (cache[i].occupied == false) continue;
		page_cache_flush( &(cache[i]) );
	}

	lock_release (&page_cache_lock);
}

/**
 * Lookup the cache entry, and returns the pointer of buffer_cache_entry_t,
 * or NULL in case of cache miss. (simply traverse the cache entries)
 */
static struct page_cache_entry *
page_cache_lookup (disk_sector_t sec_no)
{
	if(LOG)
	{
		printf("pagecache init\n");
	}
	size_t i;
	for (i = 0; i < BUFFER_CACHE_SIZE; ++ i)
	{
		if (cache[i].occupied == false) continue;
		if (cache[i].sec_no == sec_no) {
		// cache hit.
		return &(cache[i]);
		}
	}
	return NULL; // cache miss
}

/**
 * Obtain a free cache entry slot.
 * If there is an unoccupied slot already, return it.
 * Otherwise, some entry should be evicted by the clock algorithm.
 */
static struct page_cache_entry *
page_cache_evict (void)
{
	if(LOG)
	{
		printf("pagecache evict\n");
	}

	ASSERT (lock_held_by_current_thread(&page_cache_lock));

	// clock algorithm
	static size_t clock = 0;
	while (true) {
		if (cache[clock].occupied == false) {
		// found an empty slot -- use it
		return &(cache[clock]);
		}

		if (cache[clock].access) {
			// give a second chance
			cache[clock].access = false;
		}
		else break;

		clock ++;
		clock %= BUFFER_CACHE_SIZE;
	}

	// evict cache[clock]
	struct page_cache_entry *slot = &cache[clock];
	if (slot->dirty) {
		// write back into disk
		page_cache_flush (slot);
	}

	slot->occupied = false;
	return slot;
}


void page_cache_read (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	if(LOG)
	{
		printf("pagecache read\n");
	}

	lock_acquire (&page_cache_lock);

	struct page_cache_entry *slot = page_cache_lookup (sec_no);
	if (slot == NULL) {
		// cache miss: need eviction.
		slot = page_cache_evict ();
		ASSERT(slot != NULL && slot->occupied == false);

		// fill in the cache entry.
		slot->occupied = true;
		slot->sec_no = sec_no;
		slot->dirty = false;
		disk_read (d, sec_no, slot->buffer);
	}

	// copy the buffer data into memory.
	slot->access = true;
	memcpy (buffer, slot->buffer, DISK_SECTOR_SIZE);

	lock_release (&page_cache_lock);
	}

void page_cache_write (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	lock_acquire (&page_cache_lock);

	struct page_cache_entry *slot = page_cache_lookup (sec_no);
	if (slot == NULL) {
		// cache miss: need eviction.
		slot = page_cache_evict ();
		ASSERT(slot != NULL && slot->occupied == false);

		// fill in the cache entry.
		slot->occupied = true;
		slot->sec_no = sec_no;
		slot->dirty = false;
		disk_read (d, sec_no, slot->buffer);
	}

	// copy the data form memory into the buffer cache.
	slot->access = true;
	slot->dirty = true;
	memcpy (slot->buffer, buffer, DISK_SECTOR_SIZE);

	lock_release (&page_cache_lock);
}

/* END */