#ifndef FILESYS_PAGE_CACHE_H
#define FILESYS_PAGE_CACHE_H
#include "vm/vm.h"
#include <stdbool.h>
#include <debug.h>
#include "devices/disk.h"
#include "filesys/filesys.h"

/* Our Implementation */
#define PAGE_CACHE_SIZE 64
#define SECTOR_PER_PAGE 8   // 512 / 64 = 8
/* END */

struct page;
enum vm_type;

static struct lock page_cache_lock;
static int page_cache_count;
static int clock_ptr;

struct page_cache_entry {
    bool loaded;
    bool dirty;
    bool flag;
    disk_sector_t sec_no;
    char buffer[DISK_SECTOR_SIZE];
};

static struct page_cache_entry page_cache[PAGE_CACHE_SIZE];

void page_cache_init (void);
bool page_cache_initializer (struct page *page, enum vm_type type, void *kva);

/* Our Implementation */
void page_cache_destroy (void);

void page_cache_read (struct disk *d, disk_sector_t sec_no, const void *buffer);

void page_cache_write (struct disk *d, disk_sector_t sec_no, const void *buffer);
/* END */

#endif
