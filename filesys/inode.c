#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
/* Our Implementation */
#include "filesys/fat.h"
#include "filesys/directory.h"
#include "filesys/page_cache.h"

#define LOG 0
/* END */

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */

	uint32_t unused[124];               /* Not used. */

	bool is_dir;						/* This is directory or not */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
	/* Our Implementation */
	bool is_sym;
	struct lock *fat_lock;				/* Lock when accessing FAT */
};

/* For Debug */
int deny_cnt (struct inode *inode){
	return inode->deny_write_cnt;
}
/* END */

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	if(LOG)
	{
		printf("byte_to_sector\n");
	}

	ASSERT (inode != NULL);
	// for문으로 pos / DISK_SECTOR_SIZE 만큼 돌면서
	// FAT을 traverse하는 코드

	lock_acquire(&inode->fat_lock);

	disk_sector_t start = inode->data.start;
	cluster_t temp = (cluster_t) start;

	int cnt = pos / DISK_SECTOR_SIZE;

	if (pos >= inode->data.length)
	{
		lock_release(&inode->fat_lock);
		return -1;
	}
	while(cnt--)
	{
		temp = fat_get(temp);
	}
	lock_release(&inode->fat_lock);

	return cluster_to_sector(temp);
	/* Original Code */
	// if (pos < inode->data.length)
	// 	return inode->data.start + pos / DISK_SECTOR_SIZE;
	// else
	// 	return -1;
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	if(LOG)
	{
		printf("inode_init\n");
	}
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
/* Our Implementation */
// Also get is_dir in inode_create()
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir) {
	if(LOG)
	{
		printf("inode_create\n");
	}
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->is_dir = is_dir;
		/*
		if (free_map_allocate (sectors, &disk_inode->start)) {
			page_cache_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++)
					page_cache_write (filesys_disk, disk_inode->start + i, zeros);
			}
			success = true;
		}
		*/
		static char zeros[DISK_SECTOR_SIZE];
		disk_sector_t start;
		disk_sector_t temp;

		fat_put(sector, EOChain);

		if((start = fat_create_chain(0)) != 0)
		{
			disk_inode->start = start;
			// printf("	start: %d\n", start);
			temp = start;
			// Write disk_inode
			page_cache_write (filesys_disk, fat_to_data_cluster(sector), disk_inode);
			// Write first sector of file
			page_cache_write (filesys_disk, fat_to_data_cluster(disk_inode->start), zeros);
		}
		else
		{
			return false;
		}

		ASSERT(start != 0);

		// printf("	sectors: %d\n", sectors);

		for(size_t i = 1; i < sectors; i++)
		{
			// ASSERT(0);
			if((temp = fat_create_chain(temp)) != 0)
			{
				page_cache_write(filesys_disk, fat_to_data_cluster(cluster_to_sector(temp)), zeros);
			}
			else
			{
				fat_remove_chain(start, 0);
				return false;
			}
		}

		success = true;
		// free (disk_inode);
	}
	// fat_print();
	// ASSERT(0);
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	if(LOG)
	{
		printf("inode_open\n");
	}
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode;
		}
	}
	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	inode->is_sym = false;
	page_cache_read (filesys_disk, fat_to_data_cluster(inode->sector), &inode->data);
	// printf("start = %d\n", inode->data.start);
	lock_init(&inode->fat_lock);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if(LOG)
	{
		printf("inode_reopen\n");
	}
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

// void
// inode_all_write_to_disk (const struct inode *inode) {
//    if(LOG)
//    {
//       printf("inode_all_write_to_disk\n");
//    }

//    ASSERT (inode != NULL);
//    lock_acquire(&inode->fat_lock);

//    disk_sector_t start = inode->data.start;
//    cluster_t temp = (cluster_t) start;

//    while(temp != EOChain)
//    {
//       //printf("temp = %d\n", temp);
// 	  page_cache_write(filesys_disk, fat_to_data_cluster(temp), )
//       temp = fat_get(temp);
//       //page_cache_write() on cluster_to_sector(temp);
//    }
//    // printf("read sector_idx %d\n", temp);
//    lock_release(&inode->fat_lock);

//    return;
// }

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	if(LOG)
	{
		printf("inode_close\n");
	}
	ASSERT(inode != NULL);
	/* Ignore null pointer. */
	if (inode == NULL)
		return;
	
	// printf("Close sector %d length %d\n", inode->sector, inode->data.length);
	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);
		page_cache_write(filesys_disk, fat_to_data_cluster(inode->sector), &inode->data);
		/* Deallocate blocks if removed. */
		if (inode->removed) {
			// printf("inode removed\n");
			free_fat_release (inode->sector, 1);
			fat_remove_chain(inode->data.start, 0);
		}
		free (inode);
		// printf("free inode\n");
	}
	// printf("finish inode close\n");
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	if(LOG)
	{
		printf("inode_remove\n");
	}
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	if(LOG)
	{
		printf("inode_read_at\n");
		printf("	sector_number: %d\n", inode->sector);
		printf("	size: %d, offset: %d\n", size, offset);
	}
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;
	//printf("	length %d offset %d\n", inode_length(inode), offset);
	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);

		// Read beyond file length
		if(sector_idx == -1)
			break;

		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			page_cache_read (filesys_disk, fat_to_data_cluster(sector_idx), buffer + bytes_read);
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			page_cache_read (filesys_disk, fat_to_data_cluster(sector_idx), bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}

	free (bounce);

	return bytes_read;
}

struct inode *
sector_inode_open (disk_sector_t sector) {
	if(LOG)
	{
		printf("inode_open\n");
	}
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector && inode->is_sym == false) {
			inode_reopen (inode);
			return inode;
		}
	}
	
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;
	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	inode->is_sym = false;
	page_cache_read (filesys_disk, fat_to_data_cluster(inode->sector), &inode->data);
	// printf("start = %d\n", inode->data.start);
	lock_init(&inode->fat_lock);
	return inode;
}

void inode_update (struct inode *sym_inode)
{
	ASSERT(sym_inode -> is_sym == true);
	
	disk_sector_t sector = sym_inode->sector;
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			page_cache_read(filesys_disk, fat_to_data_cluster(sector), &inode->data);
		}
	}
	
	
	// printf("af update length %d\n", inode->data.length);
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	if(LOG)
	{
		printf("inode_write_at\n");
	}
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		// printf("After byte_to_sector %d\n", sector_idx);
		/* Our Implementation */
		// if byte_to_sector return -1, fat_creat_chain()
		// Offset >= Data.length
	  if(sector_idx == -1) // create할때도 -1?
		{
			// ASSERT(0);
			lock_acquire(&inode->fat_lock);
			disk_sector_t start = inode->data.start;
			cluster_t temp = (cluster_t) start;
			cluster_t prev = temp;
			// Arrive at the last sector
			while(temp != EOChain)
			{
				prev = temp;
				temp = fat_get(temp);
				// printf("temp %d\n", temp);
			}
			// fat[temp] = EOChain
			temp = prev;

			int inode_start = inode_length(inode) - (inode_length(inode) % DISK_SECTOR_SIZE);
			int add_inode = ((offset + size) - inode_start) / DISK_SECTOR_SIZE;

			static char zeros[DISK_SECTOR_SIZE];

			for(int i = 0; i < add_inode; i++)
			{
				if((temp = fat_create_chain(temp)) == 0)
					PANIC("Write beyond panic");

				page_cache_write(filesys_disk, fat_to_data_cluster(cluster_to_sector(temp)), zeros);
			}
			struct inode_disk *disk_inode = &inode->data;
			// page_cache_read (filesys_disk, fat_to_data_cluster(inode->sector), disk_inode);
			disk_inode->length = offset + size;
			disk_inode->magic = INODE_MAGIC;
			disk_inode->is_dir = inode->data.is_dir;
			page_cache_write (filesys_disk, fat_to_data_cluster(inode->sector), disk_inode);
			// printf("	temp %d offset %d size %d\n", temp, offset, size);
			lock_release(&inode->fat_lock);
			sector_idx = byte_to_sector (inode, offset);
		}

		int sector_ofs = offset % DISK_SECTOR_SIZE;
		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			page_cache_write (filesys_disk, fat_to_data_cluster(sector_idx), buffer + bytes_written);
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left)
			{
				// printf("chunk size %d sector left %d\n", chunk_size, sector_left);
				page_cache_read (filesys_disk, fat_to_data_cluster(sector_idx), bounce);
			}
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			// printf("sector_idx %d\n", sector_idx);
			page_cache_write (filesys_disk, fat_to_data_cluster(sector_idx), bounce);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);
	// printf("finish inode_write_at\n");

	if (inode->is_sym == true)
	{
		inode_update(inode);
	}

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode)
{
	if(LOG)
	{
		printf("inode_deny_write\n");
	}
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	if(LOG)
	{
		printf("inode_allow_write\n");
	}
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	if(LOG)
	{
		printf("inode_length\n");
	}
	return inode->data.length;
}


bool
inode_is_dir (const struct inode *inode)
{
	if(LOG)
	{
		printf("inode_is_dir\n");
	}
	if (inode == NULL)
		return false;
	struct inode_disk inode_disk;
	if (inode->removed)		// To prevent error such as dir-rm-cwd test case
		return false;
	return inode->data.is_dir;
}

bool
inode_is_removed (struct inode *inode)
{
   return inode->removed;
}

struct inode_disk *data_open (disk_sector_t sector)
{
	struct inode_disk *inode_disk = malloc(sizeof (struct inode_disk ));
	if (inode_disk == NULL)
		PANIC("inode_disk malloc fail");
	page_cache_read (filesys_disk, fat_to_data_cluster(sector), inode_disk);
	ASSERT(inode_disk != NULL);
	return inode_disk;
} 

/* Called in filesys_done */
void
inode_all_remove (void)
{
	struct list_elem *p;
	// ASSERT(open_inodes != NULL);
	// printf("Open inodes : %d\n", list_size(&open_inodes));
	for(p = list_begin(&open_inodes); p!=list_end(&open_inodes);) 
	{
		struct inode *inode = list_entry(p, struct inode, elem);
		p=list_next(p);
		inode_close(inode);
		// list_remove(&inode->elem);
		// free_fat_release (inode->sector, 1);
		// page_cache_write(filesys_disk, fat_to_data_cluster(inode->sector), &inode->data);
		// fat_remove_chain(inode->data.start, 0);
		// free(inode);
	}
	// printf("After remove Open inodes : %d\n", list_size(&open_inodes));

	// struct inode *inode = malloc(sizeof (struct inode));
	// inode->sector = 229;
	// inode->open_cnt = 1;
	// inode->deny_write_cnt = 0;
	// inode->removed = false;
	// inode->is_sym = false;
	// lock_init(&inode->fat_lock);
	// page_cache_read(filesys_disk, fat_to_data_cluster(inode->sector), &inode->data);
	// struct inode *temp_inode;
	// printf("Okay\n");
	// if (dir_lookup(dir_open(inode), "file", &temp_inode))
	// {
	// 	printf("Found file\n");
	// }
	// else printf("No file\n");

	return;
}

bool inode_is_sym (struct inode *inode)
{
	return inode->is_sym;
}



bool sym_inode_create (disk_sector_t sector, const char *sympath, struct dir *dir)
{
	struct inode_disk *data = data_open(sector);
	disk_sector_t new_sector = sector;
	fat_put(new_sector, EOChain);
	struct inode *inode = malloc(sizeof(struct inode));
	inode->sector = new_sector;
	inode->open_cnt = 1;
	inode->removed = false;
	inode->deny_write_cnt = 0;
	inode->is_sym = true;
	
	struct inode_disk *new_data = &inode->data;
	new_data->start = data->start;
	new_data->length = data->length;
	new_data->is_dir = data->is_dir;

	lock_init(&inode->fat_lock);

	list_push_front(&open_inodes, &inode->elem);

	dir_add (dir, sympath, new_sector, data->is_dir);
	dir_close (dir);
	free(data);
	return true;
}

void set_sym_inode (struct inode *inode)
{
	inode->is_sym = true;
}