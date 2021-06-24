/* page_cache.c: Implementation of Page Cache (Buffer Cache). */

#include "vm/vm.h"
#include "filesys/page_cache.h"
#define LOG 0

static bool page_cache_readahead (struct page *page, void *kva);
static void page_cache_writeback (struct page_cache_entry *entry);
// static void page_cache_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE
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

	lock_init(&page_cache_lock);
	
	// For second-chance algorithm
	clock_ptr = 0;

	for(int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		page_cache[i].loaded = false;
	}
	
	return;
}

// NOT USED
/* Initialize the page cache */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &page_cache_op;
	return false;
}

// NOT USED
/* Utilze the Swap in mechanism to implement readhead */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

// NOT USED
/* Worker thread for page cache */
static void
page_cache_kworkerd (void *aux) {
}

/* Used to set cache entry information */
static void set_entry 
(struct page_cache_entry *entry, bool loaded, bool flag,
bool dirty, disk_sector_t sec_no)
{
	entry->loaded = loaded;
	entry->flag = flag;
	entry->dirty = dirty;
	entry->sec_no = sec_no;
	return;
}

/* Find page cache entry with certain sec_no */
static struct page_cache_entry *
page_cache_lookup (disk_sector_t sec_no)
{
	if(LOG)
	{
		printf("pagecache lookup\n");
	}

	for (int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		if (page_cache[i].loaded == false) 	// No entry
			continue;
		
		if (page_cache[i].sec_no == sec_no) 
		{
			// Found it
			return &(page_cache[i]);
		}

	}
	return NULL; // No such entry in cache
}

/* Find empty entry in page cache */
static struct page_cache_entry *
page_cache_get_empty_entry (void) {
	if(LOG)
	{
		printf("pagecache get empty entry\n");
	}
	
	for (int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		if (page_cache[i].loaded == false) 
		{
			// Encounter empty entry then return its pointer
			return &(page_cache[i]);	
		}
	}

	return NULL;
}

/* Return avaliable page cache entry. 
   If page cache is full, evict one. */
static struct page_cache_entry *
page_cache_evict (void)
{
	if(LOG)
	{
		printf("pagecache evict\n");
	}

	struct page_cache_entry *victim;

	// Eviction Policy:
	// Approximation of LRU, Second chance algorithm
	for(int i = 0; ; i++)
	{
		if(page_cache[clock_ptr].flag)
			page_cache[clock_ptr].flag = false;
		else
			break;

		if(clock_ptr >= (PAGE_CACHE_SIZE - 1))
			clock_ptr -= (PAGE_CACHE_SIZE - 1);
		else clock_ptr++;

		// Prevent possible infinite loop
		if(i > 10 * PAGE_CACHE_SIZE)
			PANIC("Eviction may have caused infinite loop");
	}

	victim = &page_cache[clock_ptr];
	page_cache_writeback (victim);
	victim->loaded = false;

	return victim;
}

/* disk_read with page cache support */
void page_cache_read (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	if(LOG)
	{
		printf("pagecache read\n");
	}

	lock_acquire (&page_cache_lock);

	struct page_cache_entry *target;
	if ((target = page_cache_lookup (sec_no)))	// If the block is already in page cache, just copy the cache to the buffer
	{
		target->flag = true;
		memcpy (buffer, target->buffer, DISK_SECTOR_SIZE);
	} 
	else if ((target = page_cache_get_empty_entry()) != NULL) // If not, first read block to page cache and copy it to the buffer
	{
		set_entry(target, true, true, false, sec_no);
		disk_read (d, sec_no, target->buffer);
		memcpy (buffer, target->buffer, DISK_SECTOR_SIZE);
	}
	else // If page cache is full, evict victim and read block to its cache
	{
		struct page_cache_entry *victim = page_cache_evict ();
		set_entry(victim, true, true, false, sec_no);
		disk_read (d, sec_no, victim->buffer);
		memcpy (buffer, victim->buffer, DISK_SECTOR_SIZE);
	}
	lock_release (&page_cache_lock);
	return;
}

/* disk_write with page_cache support */
void page_cache_write (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	lock_acquire (&page_cache_lock);
	struct page_cache_entry *target;
	if ((target = page_cache_lookup(sec_no)))	// If the block is already in the cache, just copy buffer to the cache
	{
		target->flag = true;
		target->dirty = true;
		memcpy (target->buffer, buffer, DISK_SECTOR_SIZE);
	} 
	else if ((target = page_cache_get_empty_entry()) != NULL)	// If not, find empty cache and write buffer to it
	{
		set_entry(target, true, true, true, sec_no);	
		memcpy (target->buffer, buffer, DISK_SECTOR_SIZE);
	}
	else	// If the cache is full, evict victim and write buffer to its cache
	{
		struct page_cache_entry *victim = page_cache_evict();
		set_entry(victim, true, true, true, sec_no);	
		memcpy (victim->buffer, buffer, DISK_SECTOR_SIZE);
	}
	lock_release (&page_cache_lock);
	return;
}

void
page_cache_destroy (void)
{
	if(LOG)
	{
		printf("pagecache destroy\n");
	}

	lock_acquire (&page_cache_lock);
	for (int i = 0; i < PAGE_CACHE_SIZE; i++)	// Write all caches to disk and destroy page cache
	{
		page_cache_writeback(&(page_cache[i]));
	}
	lock_release (&page_cache_lock);
	return;
}


static void
page_cache_writeback (struct page_cache_entry *entry)	// Write cache contents back to disk
{
	if(LOG)
	{
		printf("pagecache writeback\n");
	}

	if(entry->loaded == false)	// No entry to write back to disk then just return
		return;

	if (entry->dirty) {	// If not dirty, don't need to write back to disk because contents are not changed
		disk_write (filesys_disk, entry->sec_no, entry->buffer);
	}

	entry->dirty = false;
	return;
}
/* END */