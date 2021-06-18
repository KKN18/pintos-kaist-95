#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

#define LOG 0

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	if(LOG)
	{
		printf("fat_init\n");	
	}
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	if(LOG)
	{
		printf("fat_open\n");	
	}
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	if(LOG)
	{
		printf("fat_close\n");	
	}
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	if(LOG)
	{
		printf("\nfat_create\n");	
	}
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	// fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	if(LOG)
	{
		printf("fat_boot_create\n");
	}
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	if(LOG)
	{
		printf("fat_fs_init\n");	
	}
	/* TODO: Your code goes here. */
	struct fat_boot *bs = &fat_fs->bs;
	printf("fat sectors : %d\n",bs->fat_sectors);
	fat_fs->fat_length = bs->total_sectors - bs->fat_sectors;
   	printf("fat_start: %d, fat_sectors: %d, fat_length: %d\n", bs->fat_start, bs->fat_sectors, fat_fs->fat_length);
	fat_fs->data_start = bs->fat_start + (bs->fat_sectors / bs->sectors_per_cluster) + 1;
	printf("data_start: %d\n", fat_fs->data_start);
	printf("capacity: %d\n", disk_size(filesys_disk));
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

void fat_print() {
   unsigned int *fat = fat_fs->fat;
   printf("\n-----------fat print--------------\n");
   for (size_t i = 1; i < fat_fs->fat_length; i++)
   {
      if (fat[i] != 0)
         printf("fat[%d] = %d\n", i, fat[i]);
   }
   printf("\n");
}

bool
free_fat_allocate (size_t cnt, disk_sector_t *sectorp) {
	if(LOG)
	{
		printf("free_fat_allocate\n");	
	}
	ASSERT(cnt == 1);

	unsigned int *fat = fat_fs->fat;
	bool isFound = false;
	// cluster_t last_index = fat_fs->fat_length - 1;
	cluster_t free_sector = NULL;

	for(int i = 1; i < fat_fs->fat_length; i++)
	{
		if(fat[i] == 0)
		{
			isFound = true;
			free_sector = cluster_to_sector(i);
			break;
		}
	}

	if(isFound == false)
		PANIC("Fat is full");

	if(free_sector != NULL)
		*sectorp = free_sector;
	
	return free_sector != NULL;
}

void
free_fat_release (disk_sector_t sector, size_t cnt) {
	if(LOG)
	{
		printf("free_fat_release\n");	
	}
	ASSERT(cnt == 1);

	fat_remove_chain((cluster_t)sector, 0);
	return;
}

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	if(LOG)
	{
		printf("fat_create_chain\n");	
	}
	/* TODO: Your code goes here. */
	unsigned int *fat = fat_fs->fat;
	cluster_t cluster = 0;

	// Fails to allocate a new cluster
	if(!free_fat_allocate(1, &cluster))
		return 0;

	if (clst != 0)
		fat[clst] = cluster;

	fat[cluster] = EOChain;
	
	return cluster;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	if(LOG)
	{
		printf("fat_remove_chain\n");	
	}
	/* TODO: Your code goes here. */
	unsigned int *fat = fat_fs->fat;
	cluster_t cur_clst = clst;

	while(fat[cur_clst] != EOChain && cur_clst != 1)
	{
		clst = fat[cur_clst];
		fat[cur_clst] = 0;
		cur_clst = clst;
	}

	fat[cur_clst] = 0;
	
	if(pclst != 0)
	{
		fat[pclst] = EOChain;	
	}
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	if(LOG)
	{
		printf("fat_put\n");	
	}
	/* TODO: Your code goes here. */
	unsigned int *fat = fat_fs->fat;
	fat[clst] = val;
	return;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	if(LOG)
	{
		printf("fat_get\n");	
	}
	/* TODO: Your code goes here. */
	unsigned int *fat = fat_fs->fat;
	return fat[clst];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	if(LOG)
	{
		printf("cluster_to_sector\n");	
	}
	/* TODO: Your code goes here. */
	struct fat_boot *bs = &fat_fs->bs;
	return clst * (bs->sectors_per_cluster);
}

disk_sector_t
fat_to_data_cluster (cluster_t clst) {
	if(LOG)
	{
		printf("fat_to_data_cluster\n");
	}
	
	disk_sector_t data_start = fat_fs->data_start;
	// printf("cluster: %d, fat_to_data_cluster result: %d\n", clst, data_start + clst - 1);
	return data_start + clst - 1;
}