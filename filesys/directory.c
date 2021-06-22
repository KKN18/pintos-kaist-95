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

	// Wookayin
	if(strcmp(name, ".") == 0) 
	{
		*inode = inode_reopen(dir->inode);
	}
	else if (strcmp(name, "..") == 0)
	{
		inode_read_at(dir->inode, &e, sizeof e, 0);
		*inode = inode_open(e.inode_sector);
	}
	else if (lookup (dir, name, &e, NULL))
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

	if(is_dir)
	{
		/* e is a parent-directory-entry here */
		struct dir *child_dir = dir_open( inode_open(inode_sector) );
		if(child_dir == NULL) goto done;
		e.inode_sector = inode_get_inumber( dir_get_inode(dir) );
		if (inode_write_at(child_dir->inode, &e, sizeof e, 0) != sizeof e) {
			dir_close (child_dir);
			goto done;
	}
	dir_close (child_dir);
	}

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

	// RYU
	if (!strcmp (name, ".") || !strcmp (name, ".."))
    	return false;

	/* Find directory entry. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* Open inode. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	// WOOKAYIN
	// Don't erase non-empty directory
	if (inode_is_dir (inode)) {
		// target : the directory to be removed. (dir : the base directory)
		struct dir *target = dir_open (inode);
		bool is_empty = dir_is_empty (target);
		dir_close (target);
		if (!is_empty) goto done; // can't delete
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

	if(dir_is_empty(dir))
		return false;

	if(dir->pos == 0)
		dir->pos += 2 * sizeof e;
	
	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		//printf("pos %d\n", dir->pos);
		dir->pos += sizeof e;
		if (e.in_use) {
			//printf("name %s\n", e.name);
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
		//printf("here\n");
	}
	//printf("READDIR END\n");
	return false;
}

// WOOKAYIN
/* Returns whether the DIR is empty. */
bool
dir_is_empty (const struct dir *dir)
{
	struct dir_entry e;
	off_t ofs;

	for (ofs = 2 * sizeof e; /* 0-2*pos is for parent directory */
		inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
		ofs += sizeof e)
	{
		if (e.in_use)
			return false;
	}
	return true;
}

//WOOKAYIN
struct dir *
dir_open_path (const char *path)
{
	char *last;
	char *token;
	char *path_copy;
	struct dir *dir = NULL;
	struct inode *inode = NULL;

	path_copy = calloc(strlen(path)+1, sizeof(char));
	
	if(path_copy == NULL)
	{
		PANIC("Fail malloc");
	}

	strlcpy(path_copy, path, strlen(path) + 1);

	if(path[0] == '/') {
		dir = dir_open_root();
	}
	else {
		struct thread *t = thread_current();
		if (t->working_dir == NULL)
			dir = dir_open_root();
		else {
			dir = dir_reopen( t->working_dir );
		}
	}

	token = strtok_r(path_copy, "/", &last);
	while(token != NULL) {
		if(!dir_lookup(dir, token, &inode)) {
			dir_close(dir);
			return NULL; // such directory not exist
		}

		struct dir *next = dir_open(inode);
		if(next == NULL) {
			dir_close(dir);
			return NULL;
		}

		dir_close(dir);
		dir = next;

		token = strtok_r(NULL, "/", &last);
	}

	if (inode_is_removed (dir_get_inode(dir))) {
		dir_close(dir);
		return NULL;
	}

	free(path_copy);

	return dir;
}

/* Opens the directory for given path. */
static struct dir *
_dir_open_path (const char *path)
{
   // copy of path, to tokenize
   int l = strlen(path);
   char s[l + 1];
   strlcpy(s, path, l + 1);

   // relative path handling
   struct dir *curr;
   if(path[0] == '/') { // absolute path
      curr = dir_open_root();
   }
   else { // relative path
      struct thread *t = thread_current();
      if (t->working_dir == NULL) // may happen for non-process threads (e.g. main)
         curr = dir_open_root();
      else {
         curr = dir_reopen( t->working_dir );
      }
   }

   // tokenize, and traverse the tree
   char *token, *p;
   for (token = strtok_r(s, "/", &p); token != NULL;
      token = strtok_r(NULL, "/", &p))
   {
      struct inode *inode = NULL;
      if(! dir_lookup(curr, token, &inode)) {
         dir_close(curr);
         return NULL; // such directory not exist
      }

      struct dir *next = dir_open(inode);
      if(next == NULL) {
         dir_close(curr);
         return NULL;
      }
      dir_close(curr);
      curr = next;
   }

   // prevent from opening removed directories
   if (inode_is_removed (dir_get_inode(curr))) {
      dir_close(curr);
      return NULL;
   }

   return curr;
}

struct dir *get_dir_from_sym (struct inode *sym_inode)
{
	struct inode *inode = sector_inode_open (inode_get_inumber(sym_inode));
	struct dir *dir = dir_open(inode);
	return dir;
}