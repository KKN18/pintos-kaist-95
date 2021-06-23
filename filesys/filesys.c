#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
/* Our Implementation */
#include "threads/thread.h"
#include "filesys/fat.h"
#include "filesys/page_cache.h"

#define LOG 0

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

bool is_sym_path (char *path);
/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	if(LOG)
	{
		printf("filesys_init\n");	
	}
		
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();
	page_cache_init();
	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif

	thread_current()->working_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	if(LOG)
	{
		printf("filesys_done\n");	
	}
		
	/* Original FS */
#ifdef EFILESYS
	inode_all_remove();
	fat_close ();
	page_cache_close();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) {
	
	if(LOG)
	{
		printf("filesys_create: %s, initial_size: %d\n", name, initial_size);	
	}
	disk_sector_t inode_sector = 0;
	
	char parsed_path[PATH_MAX_LEN + 1];
	struct dir *dir = get_dir_and_filename(name, parsed_path);	// Parse name and get file name to parsed_path
	if (dir == NULL)
		return false;
	
	struct inode *inode;

	// If file is already created by sym link, just return true
	// We chose the design that if symlink is created before the pointing file is created, 
	// then also create the file to support symlink
	if (dir_lookup(dir, parsed_path, &inode) && inode_is_sym(inode))	
		return true;


	if (is_dir)
	{
		if (!free_fat_allocate (1, &inode_sector))
		{
			dir_close(dir);
			return false;
		}
		if (!dir_create (inode_sector, 16))
		{
			free_fat_release(inode_sector, 1);
			dir_close(dir);
			return false;
		}
		if (!dir_add (dir, parsed_path, inode_sector, true))
		{
			free_fat_release(inode_sector, 1);
			dir_close(dir);
			return false;
		}

		// If we are creating directory, then add itself in first entry
		// and add current directory as parent directory in second entry
		struct dir *new_dir = dir_open (inode_open (inode_sector));
		dir_add (new_dir, ".", inode_sector, true);
		dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (dir)), true);
		dir_close (new_dir);
		dir_close (dir);

	}
	else
	{
		if (!free_fat_allocate (1, &inode_sector))
		{
			dir_close(dir);
			return false;
		}
		if (!inode_create (inode_sector, initial_size, false))
		{
			free_fat_release(inode_sector, 1);
			dir_close(dir);
			return false;
		}
		if (!dir_add (dir, parsed_path, inode_sector, false))
		{
			free_fat_release(inode_sector, 1);
			dir_close(dir);
			return false;
		}
	}

	return true;
	
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file * filesys_open (const char *name) {
	if(LOG)
	{
		printf("filesys_open\n");	
	}
	// printf("original path: %s\n", path);
	char parsed_path[PATH_MAX_LEN + 1];
	struct dir *dir = get_dir_and_filename (name, parsed_path);
	if (dir == NULL)
		return NULL;

	struct inode *inode = NULL;
	if (!dir_lookup (dir, parsed_path, &inode))
	{
		dir_close(dir);
		return NULL;
	}
	dir_close(dir);

	struct file *ret = file_open (inode);
	if (ret == NULL)
		return NULL;
	return ret;
}



/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool filesys_remove (const char *name) {
	if(LOG)
	{
		printf("filesys_remove: %s\n", name);	
	}
	char parsed_path[PATH_MAX_LEN + 1];
	struct dir *dir = get_dir_and_filename (name, parsed_path);
	if (dir == NULL)
		return false;
	struct inode *inode;
	if (!dir_lookup (dir, parsed_path, &inode))
	{
		dir_close(dir);
		return false;
	}
	bool success;
	if (inode_is_dir (inode))
	{
		struct dir *inode_dir = dir_open (inode);
		if (inode_dir == NULL)
		{
			dir_close(dir);
			return false;
		}
		char buffer[PATH_MAX_LEN + 1];
		if (dir_readdir (inode_dir, buffer)) // inode directory should be empty
		{
			dir_close(dir);
			dir_close(inode_dir);
			return false;
		}
		success = dir != NULL && dir_remove (dir, parsed_path);
		dir_close(dir);
		dir_close(inode_dir);
	}
	else
	{
		success = dir != NULL && dir_remove (dir, parsed_path);
		dir_close(dir);
	}
	return success;

}

/* Formats the file system. */
static void
do_format (void) {
	if(LOG)
	{
		printf("do_format\n");	
	}
		
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif
	
	struct dir *dir = dir_open_root ();	
	// Let both two entries (., ..) point self because it is root directory
	dir_add (dir, ".", ROOT_DIR_SECTOR, true);
	dir_add (dir, "..", ROOT_DIR_SECTOR, true); 

	dir_close (dir);
	printf ("done.\n");
}

bool is_sym_path (char *path)
{
	struct thread *t = thread_current();
	struct list *sym_list = &t->sym_list;
	struct list_elem *e;

	for (e = list_begin(sym_list); e != list_end(sym_list); e = list_next(e))
	{
		struct sym_link *sym = list_entry(e, struct sym_link, sym_elem);
		if (!strcmp(sym->linkpath, path)) return true;
	}
	return false;
}

bool sym_path_exist (char *path)
{
	struct thread *t = thread_current();
	struct list *sym_list = &t->sym_list;
	struct list_elem *e;

	for (e = list_begin(sym_list); e != list_end(sym_list); e = list_next(e))
	{
		struct sym_link *sym = list_entry(e, struct sym_link, sym_elem);
		if (strstr(path, sym->linkpath) != NULL) return true;
	}
	return false;
}

char *convert_sym_path (char *path)
{
	struct thread *t = thread_current();
	struct list *sym_list = &t->sym_list;
	struct list_elem *e;

	char converted[PATH_MAX_LEN];
	strlcpy(converted, path, PATH_MAX_LEN + 1);
	for (e = list_begin(sym_list); e != list_end(sym_list); e = list_next(e))
	{
		struct sym_link *sym = list_entry(e, struct sym_link, sym_elem);
		char *ptr = strstr(converted, sym->linkpath);
		if (ptr == NULL) continue;
		int link_len = strlen(sym->linkpath);
		char temp[PATH_MAX_LEN];
		strlcpy(temp, ptr + link_len, link_len + 1);
		strlcpy(ptr, sym->path, strlen(sym->path) + 1);
		strlcpy(ptr + strlen(sym->path), temp, strlen(temp) + 1);
		return convert_sym_path(converted);
	}
	char *result = (char *)malloc(sizeof(char) * (PATH_MAX_LEN));
	strlcpy(result, path, strlen(path) + 1);
	return result;
}

bool valid_path (struct dir *dir, char *name, struct inode **inode)
{
	if (!dir_lookup (dir, name, inode))
	{
		return false;
	}
	if (!inode_is_dir (*inode))
	{
		return false;
	}
	return true;
}

struct dir * get_dir_and_filename (const char *bf_path, char *af_path)
{
	if(LOG)
	{
		printf("get_dir_and_filename\n");	
	}
	if (bf_path == NULL || strlen(bf_path) == 0)
		return NULL;

	struct dir *dir;
	if (bf_path[0] == '/')
		dir = dir_open_root();
	else if (thread_current()->working_dir == NULL)
		dir = dir_open_root();
	else
		dir = dir_reopen(thread_current()->working_dir);


	// To handle such long filename, initially use more buffer than PATH_MAX_LEN
	// We will return error if the path is fully copied later
	char path[PATH_MAX_LEN + 3];	
	strlcpy (path, bf_path, PATH_MAX_LEN + 2);

	if (!inode_is_dir (dir_get_inode (dir)))  // To prevent error cases such as dir-rm-cwd
		return NULL;

	char *ptr;
	char *remainder;
	if (!(ptr = strtok_r (path, "/", &remainder)))
	{
		strlcpy(af_path, ".", PATH_MAX_LEN); // ptr is pointing to nothing
		return dir;
	}
	while (ptr != NULL && strlen(remainder)!= 0)
	{
		struct inode *inode = NULL;
		if (!valid_path (dir, ptr, &inode)) // check whether the inode is in dir and it is directory
		{
			dir_close(dir);
			return NULL;
		}
		if (inode == NULL)
			return NULL;
		dir_close(dir);
		dir = dir_open(inode);

		ptr = strtok_r(NULL, "/", &remainder);
	}
	if (strlen(ptr) == PATH_MAX_LEN + 1)  // Error if the filename is too long
		return NULL;
	strlcpy (af_path, ptr, PATH_MAX_LEN);
	return dir;
}


struct dir *parse_sympath (const char *sympath, char *parsed_path)
{
	struct dir *dir = NULL;
	char path[PATH_MAX_LEN + 3];
	strlcpy(path, sympath, PATH_MAX_LEN + 2);

	if (path[0] == '/') dir = dir_open_root();
	else return NULL;
	
	char *remainder;
	char *ptr = strtok_r (path, "/", &remainder);
	char *next_ptr = strtok_r (NULL, "/", &remainder);


	struct inode *inode = NULL;
	if (!dir_lookup (dir, ptr, &inode))
	{
		dir_close(dir);
		return NULL;
	}
	dir_close(dir);
	dir = dir_open(inode);

	strlcpy(parsed_path, next_ptr, PATH_MAX_LEN);
	return dir;
}

bool filesys_symlink (const char *target, const char *linkpath) {
	char name[PATH_MAX_LEN + 1];

	struct dir *dir = get_dir_and_filename (target, name);
	ASSERT(dir != NULL);

	struct inode *temp_inode = NULL;
	if (!dir_lookup (dir, name, &temp_inode))
	{
		if (!filesys_create(target, 0, false))
			return false;
		if (!dir_lookup (dir, name, &temp_inode))
			return false;
		set_sym_inode(temp_inode);
	}
	disk_sector_t sector = inode_get_inumber(temp_inode);
	inode_close(temp_inode);
	char symlink[PATH_MAX_LEN + 1];
	strlcpy(symlink, linkpath, PATH_MAX_LEN+1);

	char parsed_path[PATH_MAX_LEN + 1];
	struct dir *sym_dir = parse_sympath(linkpath, parsed_path);
	if (sym_dir == NULL) 
	{
		strlcpy(parsed_path, linkpath, PATH_MAX_LEN + 1);
		sym_dir = dir;
	}
	else 
	{
		dir_close(dir);
	}

	if (!sym_inode_create(sector, parsed_path, sym_dir))
		return false;

	
	return true;
	
}