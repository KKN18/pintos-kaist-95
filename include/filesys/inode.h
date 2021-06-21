#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, bool);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
/* Our Implementation */
// RYU
bool inode_is_dir (const struct inode *inode);

// WOOKAYIN
bool inode_is_removed (const struct inode *inode);

int deny_cnt (struct inode *inode); /* For Debug */
// bool sym_inode_create (disk_sector_t sector, const char *sympath, struct dir *dir);
struct inode * sector_inode_open (disk_sector_t sector);
bool inode_is_sym (struct inode *inode);
void inode_all_remove (void);
void set_sym_inode (struct inode *inode);

#endif /* filesys/inode.h */
