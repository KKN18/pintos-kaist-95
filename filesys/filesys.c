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
	inode_all_remove();
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
filesys_create (const char *path, off_t initial_size, bool is_dir) {
	
	if(LOG)
	{
		printf("filesys_create: %s, initial_size: %d\n", path, initial_size);	
	}
		
	disk_sector_t inode_sector = 0;
	
	// RYU
	bool success;
	char name[PATH_MAX_LEN + 1];
	struct dir *dir = parse_path(path, name);

	if(is_dir) 
	{
		success = (dir != NULL
					&& free_fat_allocate (1, &inode_sector)
					&& dir_create (inode_sector, 16)
					&& dir_add (dir, name, inode_sector, is_dir));
	}
	else 
	{
		success = (dir != NULL
			&& free_fat_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, is_dir)
			&& dir_add (dir, name, inode_sector, is_dir));
	}
	

	if (!success && inode_sector != 0)
		free_fat_release (inode_sector, 1);

	if (success && is_dir)
	{
		struct dir *new_dir = dir_open (inode_open (inode_sector));
		dir_add (new_dir, ".", inode_sector, true);
		dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (dir)), true);
		dir_close (new_dir);
	}

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
	// printf("original path: %s\n", path);
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

	bool sym_path = is_sym_path(path);
	if (sym_path)
	{
		struct thread *t = thread_current();
		struct list *sym_list = &t->sym_list;
		struct list_elem *e;

		for (e = list_begin(sym_list); e != list_end(sym_list); e = list_next(e))
		{
			struct sym_link *sym = list_entry(e, struct sym_link, sym_elem);
			if (!strcmp(sym->linkpath, path)) 
			{
				list_remove(e);
				return true;
			}
		}
		PANIC("NOT REACHED\n");
	}

	struct dir *dir = parse_path (path, name);
	struct inode *inode;
	dir_lookup (dir, name, &inode);

	if(inode_is_removed(inode))
		return false;

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
	dir_add (root, ".", ROOT_DIR_SECTOR, true);
	dir_add (root, "..", ROOT_DIR_SECTOR, true);  
	// printf("do_format dir close\n");
	dir_close (root);
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
	
	if (sym_path_exist(path))
	{
		// printf("Sympath exist\n");
		char *converted = convert_sym_path(path);
		strlcpy(path, converted, PATH_MAX_LEN + 2);
		free(converted);
	}

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
			dir_close (dir);
			return NULL;
		}
		if (!inode_is_dir (inode))
		{
			dir_close (dir);
			return NULL;
		}

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