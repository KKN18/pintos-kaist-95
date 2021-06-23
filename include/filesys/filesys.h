#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include <list.h>

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

// RYU
#define PATH_MAX_LEN 20

/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size, bool is_dir);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
/* Our Implementation */
// RYU
struct dir *get_dir_and_filename (const char *, char *);

struct sym_link {
    char linkpath[PATH_MAX_LEN];
    char path[PATH_MAX_LEN];
    struct list_elem sym_elem;
};
bool filesys_symlink (const char *target, const char *linkpath);
#endif /* filesys/filesys.h */
