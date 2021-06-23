#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir {
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
	disk_sector_t inode_sector;         /* Sector number of header. */
	char name[NAME_MAX + 1];            /* Null terminated file name. */
	bool in_use;                        /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
 * given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
 * it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		// PANIC("dir open fail\n");
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* Opens the root directory and returns a directory for it.
 * Return true if successful, false on failure. */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
 * Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void 
dir_close (struct dir *dir) {
	if (dir != NULL) {
		// printf("dir close\n");
		ASSERT(dir->inode != NULL);
		inode_close (dir->inode);
		free (dir);
	}
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* Searches DIR for a file with the given NAME.
 * If successful, returns true, sets *EP to the directory entry
 * if EP is non-null, and sets *OFSP to the byte offset of the
 * directory entry if OFSP is non-null.
 * otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		{
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
		}
	// printf("lookup fail\n");
	return false;
}

/* Searches DIR for a file with the given NAME
 * and returns true if one exists, false otherwise.
 * On success, sets *INODE to an inode for the file, otherwise to
 * a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if(strcmp(name, ".") == 0) 	// Given name is current directory
	{
		*inode = inode_reopen(dir->inode);
	}
	else if (strcmp(name, "..") == 0)	// Given name is parent directory
	{
		// Like in lookup function, we can read second entry of inode using inode_read_at function
		// We add parent directory information in the second entry at filesys_create()
		inode_read_at(dir->inode, &e, sizeof e, sizeof e);  
		*inode = inode_open(e.inode_sector);
	}
	else if (lookup (dir, name, &e, NULL))	// Neither self dir nor parent dir
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;
		

	return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
 * file by that name.  The file's inode is in sector
 * INODE_SECTOR.
 * Returns true if successful, false on failure.
 * Fails if NAME is invalid (i.e. too long) or a disk or memory
 * error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector, bool is_dir) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* Check NAME for validity. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* Check that NAME is not in use. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* Set OFS to offset of free slot.
	 * If there are no free slots, then it will be set to the
	 * current end-of-file.

	 * inode_read_at() will only return a short read at end of file.
	 * Otherwise, we'd need to verify that we didn't get a short
	 * read due to something intermittent such as low memory. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* Write slot. */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
done:
	return success;
}

/* Removes any entry for NAME in DIR.
 * Returns true if successful, false on failure,
 * which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (!strcmp (name, "."))	// Cannot remove self directory
    	return false;

	if (!strcmp (name, ".."))	// Cannot remove parent directory
		return false;

	/* Find directory entry. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* Open inode. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	// Cannot remove nonempty directory
	if (inode_is_dir (inode)) {
		struct dir *inode_dir = dir_open (inode);
		if (!dir_is_empty (inode_dir))
		{
			dir_close (inode_dir);
			goto done;
		}
		dir_close (inode_dir);
  	}

	/* Erase directory entry. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* Remove inode. */
	inode_remove (inode);
	success = true;

done:
	// printf("dir remove\n");
	inode_close (inode);
	return success;
}

/* Reads the next directory entry in DIR and stores the name in
 * NAME.  Returns true if successful, false if the directory
 * contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	if(dir_is_empty(dir))	// Directory contains no entries
		return false;

	if(dir->pos == 0)	
		dir->pos += 2 * sizeof e;	// Initially self, parent directory exist
	
	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		//printf("pos %d\n", dir->pos);
		dir->pos += sizeof e;
		if (e.in_use) {	// Found valid entry
			strlcpy (name, e.name, NAME_MAX + 1);	// Copy entry's name
			return true;
		}
		//printf("here\n");
	}
	//printf("READDIR END\n");
	return false;	// No entries in used
}

bool
dir_is_empty (struct dir *dir)
{
	struct dir_entry e;
	size_t ofs = 2 * sizeof e; // Initially self, parent directory exist

	for (ofs; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
		ofs += sizeof e)
	{
		if (e.in_use)	// Used entry exists
			return false;
	}
	return true;
}

struct dir *get_directory(const char *dirname)
{
	char buffer[PATH_MAX_LEN + 1];
	struct dir *dir = get_dir_and_filename(dirname, buffer);
	
	struct inode *inode;
	if (!dir_lookup(dir, buffer, &inode))
		return NULL;
	dir_close(dir);
	if (inode_is_dir(inode))
		dir = dir_open(inode);
	return dir;
}

struct dir *get_dir_from_sym (struct inode *sym_inode)
{
	struct inode *inode = sector_inode_open (inode_get_inumber(sym_inode));
	struct dir *dir = dir_open(inode);
	return dir;
}