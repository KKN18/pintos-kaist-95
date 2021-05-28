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

#define LOG 0

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

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

	/* RYU */
	// printf("working dir\n");
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
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size) {
	
	if(LOG)
	{
		printf("filesys_create: %s, initial_size: %d\n", path, initial_size);	
	}
		
	disk_sector_t inode_sector = 0;
	
	// RYU
	char name[PATH_MAX_LEN + 1];
	struct dir *dir = parse_path(path, name);

	bool success = (dir != NULL
			&& free_fat_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (dir, name, inode_sector));

	if (!success && inode_sector != 0)
		free_fat_release (inode_sector, 1);
	// printf("filesys create dir close\n");
	dir_close (dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
// RYU
struct file * filesys_open (const char *path) {
	if(LOG)
	{
		printf("filesys_open\n");	
	}
	char name[PATH_MAX_LEN + 1];
	struct dir *dir = parse_path (path, name);
	if (dir == NULL)
		return NULL;
	struct inode *inode = NULL;
	if (!dir_lookup (dir, name, &inode))
		return NULL;
	// printf("filesys_open dir close\n");
	dir_close (dir);
	return file_open (inode);
}


/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
// RYU
bool
filesys_remove (const char *path) {
	if(LOG)
	{
		printf("filesys_remove: %s\n", path);	
	}
	
	char name[PATH_MAX_LEN + 1];
	struct dir *dir = parse_path (path, name);

	struct inode *inode;
	dir_lookup (dir, name, &inode);

	struct dir *cur_dir = NULL;
	char temp[PATH_MAX_LEN + 1];

	bool success = false;
	if (!inode_is_dir (inode) ||
		((cur_dir = dir_open (inode)) && !dir_readdir (cur_dir, temp)))
		success = dir != NULL && dir_remove (dir, name);
	// printf("filesys remove dir close\n");
	dir_close (dir);
	
	if (cur_dir)
		// printf("filesys remove cur_dir dir close\n");
		dir_close (cur_dir);
	return success;
}

struct dir {
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

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
	
	// RYU
	struct dir *root = dir_open_root ();
	dir_add (root, ".", ROOT_DIR_SECTOR);
	dir_add (root, "..", ROOT_DIR_SECTOR);  
	// printf("do_format dir close\n");
	dir_close (root);
	printf ("done.\n");
}

/* Our Implementation */
// RYU
struct dir *
parse_path (const char *path_o, char *file_name)
{
	if(LOG)
	{
		printf("parse_path\n");	
	}
		
	struct dir *dir = NULL;

	// 기본 예외 처리
	if (!path_o || !file_name)
		return NULL;
	if (strlen (path_o) == 0)
		return NULL;

	char path[PATH_MAX_LEN + 3];
	strlcpy (path, path_o, PATH_MAX_LEN + 2);

	if (path[0] == '/')
		dir = dir_open_root ();
	else
		dir = dir_reopen (thread_current ()->working_dir);

	// 아이노드가 어떤 이유로 제거되었거나 디렉터리가 아닌 경우
	if (!inode_is_dir (dir_get_inode (dir)))
		return NULL;

	char *token, *next_token, *save_ptr;
	token = strtok_r (path, "/", &save_ptr);
	next_token = strtok_r (NULL, "/", &save_ptr);

	if (token == NULL)
	{
		strlcpy (file_name, ".", PATH_MAX_LEN);
		return dir;
	}

	while (token && next_token)
	{
		struct inode *inode = NULL;
		if (!dir_lookup (dir, token, &inode))
		{
			PANIC("parse dir_lookup fail\n");
			dir_close (dir);
			return NULL;
		}
		if (!inode_is_dir (inode))
		{
			PANIC("parse dir_lookup fail\n");
			dir_close (dir);
			return NULL;
		}
		// printf("parse while dir close\n");
		dir_close (dir);
		dir = dir_open (inode);

		token = next_token;
		next_token = strtok_r (NULL, "/", &save_ptr);
	}
	if (strlen(token) == PATH_MAX_LEN + 1)
		return NULL;
	strlcpy (file_name, token, PATH_MAX_LEN);
	return dir;
}

// RYU
bool
filesys_create_dir (const char *path)
{
	if(LOG)
	{
		printf("filesys_create_dir: %s\n", path);	
	}
		
	disk_sector_t inode_sector = 0;
	char name[PATH_MAX_LEN + 1];
	struct dir *dir = parse_path (path, name);

	bool success = (dir != NULL
					&& free_fat_allocate (1, &inode_sector)
					&& dir_create (inode_sector, 16)
					&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_fat_release (inode_sector, 1);

	
	if (success)
	{
		struct dir *new_dir = dir_open (inode_open (inode_sector));
		dir_add (new_dir, ".", inode_sector);
		dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (dir)));
		// printf("filesys_create_dir new_dir close\n");
		dir_close (new_dir);
	}
	// printf("filesys_create_dir dir close\n");
	dir_close (dir);
	return success;
}