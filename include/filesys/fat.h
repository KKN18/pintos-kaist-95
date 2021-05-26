#ifndef FILESYS_FAT_H
#define FILESYS_FAT_H

#include "devices/disk.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t cluster_t;  /* Index of a cluster within FAT. */

#define FAT_MAGIC 0xEB3C9000 /* MAGIC string to identify FAT disk */
#define EOChain 0x0FFFFFFF   /* End of cluster chain */

/* Sectors of FAT information. */
#define SECTORS_PER_CLUSTER 1 /* Number of sectors per cluster */
#define FAT_BOOT_SECTOR 0     /* FAT boot sector. */

// We modified it to 0
#define ROOT_DIR_CLUSTER 0    /* Cluster for the root directory */

void fat_init (void);
void fat_open (void);
void fat_close (void);
void fat_create (void);
void fat_close (void);

cluster_t fat_create_chain (
    cluster_t clst /* Cluster # to stretch, 0: Create a new chain */
);
void fat_remove_chain (
    cluster_t clst, /* Cluster # to be removed */
    cluster_t pclst /* Previous cluster of clst, 0: clst is the start of chain */
);
cluster_t fat_get (cluster_t clst);
void fat_put (cluster_t clst, cluster_t val);
disk_sector_t cluster_to_sector (cluster_t clst);

/* Our Implementation */
bool free_fat_allocate (size_t cnt, disk_sector_t *sectorp);
void free_fat_release (disk_sector_t sector, size_t cnt);
void fat_print();
disk_sector_t fat_to_data_cluster (cluster_t clst);

#endif /* filesys/fat.h */
