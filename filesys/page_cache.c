/* page_cache.c: Implementation of Page Cache (Buffer Cache). */

#include "vm/vm.h"
#include "filesys/page_cache.h"
#define LOG 0

static bool page_cache_readahead (struct page *page, void *kva);
static void page_cache_writeback (struct page_cache_entry *entry);
static void page_cache_destroy (struct page *page);

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
	clock_ptr = 0;

	for(int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		cache[i].loaded = false;
	}
	
	return;
}

/* Initialize the page cache */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &page_cache_op;
	return false;
}

/* Utilze the Swap in mechanism to implement readhead */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* Destory the page_cache. */
static void
page_cache_destroy (struct page *page) {
}

/* Worker thread for page cache */
static void
page_cache_kworkerd (void *aux) {
}

static void set_entry 
(struct page_cache_entry *entry, bool loaded, 
bool dirty, disk_sector_t sec_no)
{
	entry->loaded = loaded;
	entry->dirty = dirty;
	entry->sec_no = sec_no;
	return;
}

static void
page_cache_writeback (struct page_cache_entry *entry)
{
	if(LOG)
	{
		printf("pagecache writeback\n");
	}

	if(entry->loaded == false)
		return;

	if (entry->dirty) {
		disk_write (filesys_disk, entry->sec_no, entry->buffer);
	}

	entry->dirty = false;

	return;
}

void
page_cache_close (void)
{
	if(LOG)
	{
		printf("pagecache close\n");
	}

	lock_acquire (&page_cache_lock);

	for (int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		page_cache_writeback(&(cache[i]));
	}

	lock_release (&page_cache_lock);
	return;
}

static struct page_cache_entry *
page_cache_lookup (disk_sector_t sec_no)
{
	if(LOG)
	{
		printf("pagecache init\n");
	}

	for (int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		if (cache[i].loaded == false) 
			continue;
		if (cache[i].sec_no == sec_no) {
			return &(cache[i]);
		}
	}
	return NULL; // cache miss
}

static struct page_cache_entry *
page_cache_get_empty_entry (void) {
	
	for (int i = 0; i < PAGE_CACHE_SIZE; i++)
	{
		if (cache[i].loaded == false) 
			return &(cache[i]);
	}

	return NULL;
}

static struct page_cache_entry *
page_cache_evict (void)
{
	if(LOG)
	{
		printf("pagecache evict\n");
	}

	struct page_cache_entry *victim;

	if((victim = page_cache_get_empty_entry()) != NULL)
		return victim;

	// Approximate LRU
	for(int i = 0; ; i++)
	{
		if(cache[clock_ptr].flag)
			cache[clock_ptr].flag = false;
		else
			break;

		if(clock_ptr >= (PAGE_CACHE_SIZE - 1))
			clock_ptr -= (PAGE_CACHE_SIZE - 1);
		else clock_ptr++;

		if(i > 10 * PAGE_CACHE_SIZE)
			PANIC("Eviction may have caused infinite loop");
	}

	victim = &cache[clock_ptr];
	if (victim->dirty) {
		page_cache_writeback (victim);
	}

	victim->loaded = false;
	return victim;
}

void page_cache_read (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	if(LOG)
	{
		printf("pagecache read\n");
	}

	lock_acquire (&page_cache_lock);

	struct page_cache_entry *victim = page_cache_lookup (sec_no);
	if (victim == NULL) {
		victim = page_cache_evict ();
		set_entry(victim, true, sec_no, false);
		disk_read (d, sec_no, victim->buffer);
	}

	victim->flag = true;
	memcpy (buffer, victim->buffer, DISK_SECTOR_SIZE);

	lock_release (&page_cache_lock);
	return;
}

void page_cache_write (struct disk *d, disk_sector_t sec_no, const void *buffer)
{
	lock_acquire (&page_cache_lock);

	struct page_cache_entry *victim = page_cache_lookup (sec_no);
	if (victim == NULL) {
		victim = page_cache_evict ();
		set_entry(victim, true, false, sec_no);
		disk_read (d, sec_no, victim->buffer);
	}

	victim->flag = true;
	victim->dirty = true;
	memcpy (victim->buffer, buffer, DISK_SECTOR_SIZE);

	lock_release (&page_cache_lock);
	return;
}

/* END */