/*
 * file:        homework.c
 * description: skeleton file for CS 5600/7600 file system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2016
 * Philip Gust, March 2019
 */

#define FUSE_USE_VERSION 27

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>

#include "fsx600.h"
#include "blkdev.h"


//extern int homework_part;       /* set by '-part n' command-line option */

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of BLOCK_SIZE byte blocks
 */
extern struct blkdev *disk;

/* by defining bitmaps as 'fd_set' pointers, you can use existing
 * macros to handle them. 
 *   FD_ISSET(##, inode_map);
 *   FD_CLR(##, block_map);
 *   FD_SET(##, block_map);
 */

/** pointer to inode bitmap to determine free inodes */
static fd_set *inode_map;
static int     inode_map_base;

/** pointer to inode blocks */
static struct fs_inode *inodes;
/** number of inodes from superblock */
static int   n_inodes;
/** number of first inode block */
static int   inode_base;

/** pointer to block bitmap to determine free blocks */
fd_set *block_map;
/** number of first data block */
static int     block_map_base;

/** number of available blocks from superblock */
static int   n_blocks;

/** number of root inode from superblock */
static int   root_inode;

/** number of metadata blocks */
static int    n_meta;

/** array of dirty metadata blocks to write */
static void **dirty;

/* Suggested functions to implement -- you are free to ignore these
 * and implement your own instead
 */
/**
 * Look up a single directory entry in a directory.
 *
 * Errors
 *   -EIO     - error reading block
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - intermediate component of path not a directory
 *
 * @param inum the inode number of the directory
 * @param name the name of the child file or directory
 * @return the inode of the child file or directory or an error value
 */
static int lookup(int inum, const char *name)
{
    return -EOPNOTSUPP;
}

/**
 * Parse path name into tokens at most nnames tokens after
 * normalizing paths by removing '.' and '..' elements.
 *
 * If names is NULL,path is not altered and function  returns
 * the path count. Otherwise, path is altered by strtok() and
 * function returns names in the names array, that point to
 * elements of path string.
 *
 * @param path the directory path
 * @param names the argument token array or NULL
 * @param nnames the maximum number of names, 0 = unlimited
 * @return the number of path name tokens
 */
static int parse(char *path, char *names[], int nnames)
{
    char *token = NULL;
    //char mpath[40] = "/users/documents/test.txt/..";
    char mpath[40];
    strcpy(mpath, path);
    char* delim = "/";
    names[nnames] = NULL;
    int num_tokens = 0;
    //char *mydelim = "/";

    /* get the first token */
    token = strtok(mpath, delim);
    //names[0] = token;

    int i = 0;
    /* walk through other tokens */
    while( token != NULL ) {
        if(!(strcmp(token, ".")==0 || strcmp(token, "..")==0 )){
            char *tokenCopy = malloc(strlen(token));
            strcpy(tokenCopy, token);
            names[i] = tokenCopy;
            //printf( " %s\n", names[i] );
            i++;
            num_tokens++;
        }
        token = strtok(NULL, delim);


    }
    //printf("No. of tokens: %d\n", num_tokens);
	return num_tokens;
}

/* Return inode number for specified file or directory.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @return inode of path node or error
 */
static int translate(const char *path)
{
	// note: make copy of path before passing to parse()
	// note: make copy of path before passing to parse()
	char *file_path = strdup(path);
	char *names[64] = {NULL};
	int token_number = parse(file_path, names, 0);
	int the_inode = root_inode;
	void *block;
	block = malloc(FS_BLOCK_SIZE);
	struct fs_dirent *fd;
	struct fs_inode temp;
	for (int i = 0; i < token_number; i++) {
		const char *file_name =(const char *) names[i];
		temp = inodes[the_inode];
		disk->ops->read(disk, temp.direct[0], 1, block);
		fd = block;
		int index = 0;
		for (index = 0; index < DIRENTS_PER_BLK; index++) {
			if (fd->valid && !strcmp(file_name, fd->name)) {
				if (names[i + 1] != NULL && !fd->isDir) {
					free(block);
					return -ENOTDIR;
				}
				the_inode = fd->inode;
				break;
			}
			fd++;
		}
		if (index == DIRENTS_PER_BLK) {
			free(block);
			return ENOENT;
		}
	}

    return the_inode;
}

/**
 *  Return inode number for path to specified file
 *  or directory, and a leaf name that may not yet
 *  exist.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param leaf pointer to space for FS_FILENAME_SIZE leaf name
 * @return inode of path node or error
 */
static int translate_1(const char *path, char *leaf)
{
	// note: make copy of path before passing to parse()
    return -EOPNOTSUPP;
}

/**
 * Mark a inode as dirty.
 *
 * @param in pointer to an inode
 */
static void mark_inode(struct fs_inode *in)
{
    int inum = in - inodes;
    int blk = inum / INODES_PER_BLK;
    dirty[inode_base + blk] = (void*)inodes + blk * FS_BLOCK_SIZE;
}

/**
 * Flush dirty metadata blocks to disk.
 */
static void flush_metadata(void)
{
    int i;
    for (i = 0; i < n_meta; i++) {
        if (dirty[i] != NULL) {
            disk->ops->write(disk, i, 1, dirty[i]);
            dirty[i] = NULL;
        }
    }
}

/**
 * Gets a free block number from the free list.
 *
 * @return free block number or 0 if none available
 */
static int get_free_blk(void)
{
	return 0;
}

/**
 * Return a block to the free list
 *
 * @param  blkno the block number
 */
static void return_blk(int blkno)
{
}

/**
 * Gets a free inode number from the free list.
 *
 * @return a free inode number or 0 if none available
 */
static int get_free_inode(void)
{
	return 0;
}

/**
 * Return a inode to the free list.
 *
 * @param  inum the inode number
 */
static void return_inode(int inum)
{
}

/**
 * Find entry number for existing directory entry.
 *
 * @param fs_dirent ptr to first dirent in directory
 * @param name the name of the directory entry
 * @return the entry number, or -1 if not found.
 */
static int find_dir_entry(struct fs_dirent *de, const char *name)
{
    return -1;
}

/**
 * Find inode for existing directory entry.
 *
 * @param fs_dirent ptr to first dirent in directory
 * @param name the name of the directory entry
 * @return the entry inode, or 0 if not found.
 */
static int find_in_dir(struct fs_dirent *de, const char *name)
{
	return 0;
}

/**
 * Find free directory entry number.
 *
 * @return index of directory free entry or -ENOSPC
 *   if no space for new entry in directory
 */
static int find_free_dir(struct fs_dirent *de)
{
    return -ENOSPC;
}

/**
 * Determines whether directory is empty.
 *
 * @param de ptr to first entry in directory
 * @return 1 if empty 0 if has entries
 */
static int is_empty_dir(struct fs_dirent *de)
{
	return 0;
}

/**
 * Returns the n-th block of the file, or allocates
 * it if it does not exist and alloc == 1.
 *
 * @param in the file inode
 * @param n the 0-based block index in file
 * @param alloc 1=allocate block if does not exist 0 = fail
 *   if does not exist
 * @return block number of the n-th block or 0 if available
 */
static int get_blk(struct fs_inode *in, int n, int alloc)
{
	return 0;
}

/* Fuse functions
 */

/**
 * init - this is called once by the FUSE framework at startup.
 *
 * This is a good place to read in the super-block and set up any
 * global variables you need. You don't need to worry about the
 * argument or the return value.
 *
 * @param conn fuse connection information - unused
 * @return unused - returns NULL
 */
static void* fs_init(struct fuse_conn_info *conn)
{
	// read the superblock
    struct fs_super sb;
    if (disk->ops->read(disk, 0, 1, &sb) < 0) {
        exit(1);
    }

    root_inode = sb.root_inode;

    /* The inode map and block map are written directly to the disk after the superblock */

    // read inode map
    inode_map_base = 1;
    inode_map = malloc(sb.inode_map_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, inode_map_base, sb.inode_map_sz, inode_map) < 0) {
        exit(1);
    }

    // read block map
    block_map_base = inode_map_base + sb.inode_map_sz;
    block_map = malloc(sb.block_map_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, block_map_base, sb.block_map_sz, block_map) < 0) {
        exit(1);
    }

    /* The inode data is written to the next set of blocks */
    inode_base = block_map_base + sb.block_map_sz;
    n_inodes = sb.inode_region_sz * INODES_PER_BLK;
    inodes = malloc(sb.inode_region_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, inode_base, sb.inode_region_sz, inodes) < 0) {
        exit(1);
    }

    // number of blocks on device
    n_blocks = sb.num_blocks;

    // number of metadata blocks
    n_meta = inode_base + sb.inode_region_sz;

    // allocate array of dirty metadata block pointers
    dirty = calloc(n_meta, sizeof(void*));  // ptrs to dirty metadata blks

    /* your code here */

    fuse_fill_dir_t filler;
    void *ptr = filler;
    off_t offset;
    struct fuse_file_info *fi;
    readdir("/dir1", ptr, filler, fi);
    printf(filler);

    return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path is not present.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/**
 * getattr - get file or directory attributes. For a description of
 * the fields in 'struct stat', see 'man lstat'.
 *
 * Note - fields not provided in CS5600fs are:
 *    st_nlink - always set to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param sb pointer to stat struct
 * @return 0 if successful, or -error number
 */

static void fs_set_attrs(struct fs_inode *inode, struct stat *sb, int inum) {
    //set attrs from inode to sb
	sb->st_ino = inum;
    sb->st_blocks = (inode->size - 1) / FS_BLOCK_SIZE + 1;
    sb->st_mode = inode->mode;
    sb->st_size = inode->size;
    sb->st_uid = inode->uid;
    sb->st_gid = inode->gid;
    //set time
    sb->st_ctime = inode->ctime;
    sb->st_mtime = inode->mtime;
    sb->st_atime = sb->st_mtime;
    sb->st_nlink = 1;
}

static int fs_getattr(const char *path, struct stat *sb)
{
    int inode_num = translate(path); // read the inode number from the given path

    struct fs_inode inode = inodes[inode_num];
    fs_set_attrs(&inode, &sb, inode_num);

    // if the entry is not there then return the inode value
    if (inode_num != -ENOENT || inode_num != -ENOTDIR) {
            return inode_num;
       }
    if (inode_num == -ENOENT) {
                return -ENOENT;
        }

    if (inode_num == -ENOTDIR) {
                return ENOTDIR;
           }


    return 0;
}


/**
 * readdir - get directory contents.
 *
 * For each entry in the directory, invoke the 'filler' function,
 * which is passed as a function pointer, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a struct stat, just like in getattr.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the directory path
 * @param ptr  filler buf pointer
 * @param filler filler function to call for each entry
 * @param offset the file offset -- unused
 * @param fi the fuse file information
 * @return 0 if successful, or -error number
 */




static int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct stat sb;
	int rtv = fs_getattr(path, &sb);
	//return if there is any error from getattr
	if(rtv == -ENOENT || rtv == -ENOTDIR) {
		return rtv;
	}

	struct fs_inode inode = inodes[sb.st_ino];
	//check if the inode is a directory
	if(!S_ISDIR(inode.mode)) {
		return -ENOTDIR;
	}


	//request memory of block size
	void *block = malloc(FS_BLOCK_SIZE);
	//read information of the inode's d-entries block from disk into block
	disk->ops->read(disk, inode.direct[0], 1, block);
	struct fs_dirent *fd = block;

	int i;
	for(i = 0; i < DIRENTS_PER_BLK; i++) {
		if(fd->valid) {
			//reset sb
			memset(&sb, 0, sizeof(sb));
			//get inode
			inode = inodes[fd->inode];
			//set attrs of inode to sb
			fs_set_attrs(&inode, &sb, fd->inode);
			//fill
			filler(ptr, fd->name, &sb, 0);
		}
		fd++;
	}

	if(block) {
		free(block);
	}

    return 0;
}



/**
 * open - open file directory.
 *
 * You can save information about the open directory in
 * fi->fh. If you allocate memory, free it in fs_releasedir.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param fi fuse file system information
 * @return 0 if successful, or -error number
 */
static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/**
 * Release resources when directory is closed.
 * If you allocate memory in fs_opendir, free it here.
 *
 * @param path the directory path
 * @param fi fuse file system information
 * @return 0 if successful, or -error number
 */
static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/**
 * mknod - create a new file with permissions (mode & 01777)
 * minor device numbers extracted from mode. Behavior undefined
 * when mode bits other than the low 9 bits are used.
 *
 * The access permissions of path are constrained by the
 * umask(2) of the parent process.
 *
 * Errors
 *   -ENOTDIR  - component of path not a directory
 *   -EEXIST   - file already exists
 *   -ENOSPC   - free inode not available
 *   -ENOSPC   - results in >32 entries in directory
 *
 * @param path the file path
 * @param mode the mode, indicating block or character-special file
 * @param dev the character or block I/O device specification
 * @return 0 if successful, or -error number
 */
static int fs_mknod(const char *path, mode_t mode, dev_t dev)
{
    return -EOPNOTSUPP;
}

/**
 *  mkdir - create a directory with the given mode. Behavior
 *  undefined when mode bits other than the low 9 bits are used.
 *
 * Errors
 *   -ENOTDIR  - component of path not a directory
 *   -EEXIST   - directory already exists
 *   -ENOSPC   - free inode not available
 *   -ENOSPC   - results in >32 entries in directory
 *
 * @param path path to file
 * @param mode the mode for the new directory
 * @return 0 if successful, or -error number
 */ 
static int fs_mkdir(const char *path, mode_t mode)
{
    return -EOPNOTSUPP;
}

/**
 * truncate - truncate file to exactly 'len' bytes.
 *
 * Errors:
 *   ENOENT  - file does not exist
 *   ENOTDIR - component of path not a directory
 *   EINVAL  - length invalid (only supports 0)
 *   EISDIR	 - path is a directory (only files)
 *
 * @param path the file path
 * @param len the length
 * @return 0 if successful, or -error number
 */
static int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0) {
    	return -EINVAL;		/* invalid argument */
    }
    return -EOPNOTSUPP;
}

/**
 * unlink - delete a file.
 *
 * Errors
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *   -EISDIR   - cannot unlink a directory
 *
 * @param path path to file
 * @return 0 if successful, or -error number
 */

char *get_last_of_split(char *path) {

    char *token = NULL;
    char *tokens[64] = {NULL};
    int i = 0;
    /* tokenize the string */
    token = strtok(path, "/");
    while (token) {
        tokens[i++] = token;
        token = strtok(NULL, "/");
    }

    return tokens[--i];
}

static int fs_unlink(const char *path)
{
    int i = 0;
    int inum = fs_truncate(path, 0);
    if (inum < 0)
        return inum;

    char dir_name[FS_FILENAME_SIZE];
    char *_path = strdupa(path);
    int parent_inum = get_parent_inum(_path, dir_name);
    _path = strdupa(path);
    char *last;

    last = get_last_of_split(_path);

    struct fs_inode parent_dir = inodes[parent_inum];
    struct fs_dirent *block = (struct fs_dirent *) malloc(FS_BLOCK_SIZE);
    disk->ops->read(disk, parent_dir.direct[0], 1, block);


    /* find the inode entry and clear it */
    for (i = 0; i < 32; i++) {

        if (block[i].valid == 0) {
            continue;
        }

        if (strcmp(block[i].name, last) == 0) {

            if (block[i].isDir) {
                return -EISDIR;
            }
            int file_node_num = block[i].inode;
            struct fs_inode fileinode = inodes[file_node_num];
            if (FD_ISSET(file_node_num, inode_map)) {
                FD_CLR(file_node_num, inode_map);
                fileinode.size = 0;
                fileinode.mtime = time(NULL);
                block[i].valid = 0;
                inodes[file_node_num] = fileinode;
                break;
            }
        }
    }


    if (i > 31) {
        return -ENOENT;
    }

    disk->ops->write(disk, parent_dir.direct[0], 1, block);
    write_all_inodes();
    //write_block_map();
    write_inode_map();
    free(block);

    return 0;

}

/**
 * rmdir - remove a directory.
 *
 * Errors
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *   -ENOTDIR  - path not a directory
 *   -ENOTEMPTY - directory not empty
 *
 * @param path the path of the directory
 * @return 0 if successful, or -error number
 */
static int fs_rmdir(const char *path)
{
    return -EOPNOTSUPP;
}

/**
 * rename - rename a file or directory.
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 *
 * Errors:
 *   -ENOENT   - source file or directory does not exist
 *   -ENOTDIR  - component of source or target path not a directory
 *   -EEXIST   - destination already exists
 *   -EINVAL   - source and destination not in the same directory
 *
 * @param src_path the source path
 * @param dst_path the destination path.
 * @return 0 if successful, or -error number
 */
static int fs_rename(const char *src_path, const char *dst_path)
{
    //get the old name from src_path
    char the_old_name[FS_FILENAME_SIZE];
    char *tmp_path = strdupa(src_path);
    //get_parent_inum
    int prev_pinum = translate_1(tmp_path, the_old_name);

    tmp_path = strdupa(src_path);
    //translate_path_to_inum
    int curr_inum = translate(tmp_path);

    char the_new_name[FS_FILENAME_SIZE];
    tmp_path = strdupa(dst_path);
    int new_pinum = translate_1(tmp_path, the_new_name);

    if (curr_inum == -ENOTDIR || curr_inum == -ENOENT) {
        return curr_inum;
    }

    if (prev_pinum != new_pinum) {
        return -EINVAL;
    }

    struct fs_inode parent_inode = inodes[prev_pinum];
    void *block = malloc(FS_BLOCK_SIZE);
    disk->ops->read(disk, parent_inode.direct[0], 1, block);
    struct fs_dirent *entry = block;

    /* to check if destination is not present */
    int i = 0;
    for (i = 0; i < DIRENTS_PER_BLK; i++) {
        if (!strcmp(entry->name, the_new_name)) {
            if (block) {
                free(block);
            }
            return -EEXIST;
        }
        entry++;
    }

    /* update name of matching inode */
    entry = block;
    for (i = 0; i < DIRENTS_PER_BLK; i++) {
        if (entry->inode == curr_inum) {
            strncpy(entry->name, the_new_name, strlen(the_new_name));
            struct fs_inode inode = inodes[curr_inum];
            inode.ctime = time(NULL);
            inodes[curr_inum] = inode;
            write_all_inodes();
            break;
        }
        entry++;
    }

    /* write back to disk */
    disk->ops->write(disk, parent_inode.direct[0], 1, block);

    /* free previously allocated memory */
    if (block) {
        free(block);
    }

    return 0;

}

/**
 * chmod - change file permissions
 *
 * Errors:
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *
 * @param path the file or directory path
 * @param mode the mode_t mode value -- see man 'chmod'
 *   for description
 * @return 0 if successful, or -error number
 */


//write all the inode to the disk
static void write_all_inodes() {
	struct fs_super sb;
    disk->ops->write(disk, (1 + sb.inode_map_sz + sb.block_map_sz), sb.inode_region_sz, inodes);
}

static int fs_chmod(const char *path, mode_t mode)
{
    struct stat sb;
    int rtv = fs_getattr(path, &sb);

    // check if there was any error when the path is being resolved.
    if (rtv == -ENOENT || rtv == -ENOTDIR) {
        return rtv;
    }

    struct fs_inode inode = inodes[sb.st_ino];

    // update the mode of the directory or the file.
    if (S_ISDIR(inode.mode)) {  //if it is a directory
        inode.mode = (S_IFDIR | mode);
    } else if (S_ISREG(inode.mode)) {  //if it is a regular file
        inode.mode = (S_IFREG | mode);
    }

    // update the ctime for the inode
    inode.ctime = time(NULL);

    // finally write to the disk
    inodes[sb.st_ino] = inode;
    write_all_inodes();
    return 0;
}

/**
 * utime - change access and modification times.
 *
 * Errors:
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *
 * @param path the file or directory path.
 * @param ut utimbuf - see man 'utime' for description.
 * @return 0 if successful, or -error number
 */
static int fs_utime(const char *path, struct utimbuf *ut)
{
    struct stat sb;
    int rtv = fs_getattr(path, &sb);

    // check for error
    if (rtv == -ENOENT || rtv == -ENOTDIR) {
        return rtv;
    }

    struct fs_inode inode = inodes[sb.st_ino];

    // update modification time for the directory or file type.
    inode.mtime = ut->modtime;

    // write to disk
    inodes[sb.st_ino] = inode;
    write_all_inodes();
    return 0;
}

/**
 * read - read data from an open file.
 *
 * Should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return bytes from offset to EOF
 *   - on error, return <0
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *   -EIO     - error reading block
 *
 * @param path the path to the file
 * @param buf the read buffer
 * @param len the number of bytes to read
 * @param offset to start reading at
 * @param fi fuse file info
 * @return number of bytes actually read if successful, or -error number
 */
static int fs_read(const char *path, char *buf, size_t len, off_t offset,
		    struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/**
 *  write - write data to a file
 *
 * It should return exactly the number of bytes requested, except on
 * error.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *   -EINVAL  - if 'offset' is greater than current file length.
 *  			(POSIX semantics support the creation of files with
 *  			"holes" in them, but we don't)
 *
 * @param path the file path
 * @param buf the buffer to write
 * @param len the number of bytes to write
 * @param offset the offset to starting writing at
 * @param fi the Fuse file info for writing
 * @return number of bytes actually written if successful, or -error number
 *
 */
static int fs_write(const char *path, const char *buf, size_t len,
		     off_t offset, struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/**
 * Open a filesystem file or directory path.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *
 * @param path the path
 * @param fuse file info data
 * @return 0 if successful, or -error number
 */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/**
 * Release resources created by pending open call.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *
 * @param path the file name
 * @param fi the fuse file info
 * @return 0 if successful, or -error number
 */
static int fs_release(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/**
 * statfs - get file system statistics.
 * See 'man 2 statfs' for description of 'struct statvfs'.
 *
 * Errors
 *   none -  Needs to work
 *
 * @param path the path to the file
 * @param st the statvfs struct
 * @return 0 for successful
 */
static int fs_statfs(const char *path, struct statvfs *st)
{
    /* Return the following fields (set others to zero):
     *   f_bsize:	fundamental file system block size
     *   f_blocks	total blocks in file system
     *   f_bfree	free blocks in file system
     *   f_bavail	free blocks available to non-superuser
     *   f_files	total file nodes in file system
     *   f_ffiles	total free file nodes in file system
     *   f_favail	total free file nodes available to non-superuser
     *   f_namelen	maximum length of file name
     */
	memset(st, 0, sizeof(statvfs));

	// compute number of free blocks
	int n_blocks_free = 0;
	for (int i = 0; i < n_blocks; i++) {
		if (FD_ISSET(i, block_map) == 0) n_blocks_free++;
	}

	// compute number of free inodes
	int n_inodes_free = 0;
	for (int i = 0; i < n_inodes; i++) {
		if (FD_ISSET(i, inode_map) == 0) n_inodes_free++;
	}

	st->f_bsize = FS_BLOCK_SIZE;
    st->f_blocks = n_blocks;
    st->f_bfree = n_blocks_free;
    st->f_bavail = st->f_bfree;
    st->f_files = n_inodes;
    st->f_ffree = n_inodes_free;
    st->f_favail = st->f_ffree;
    st->f_namemax = FS_FILENAME_SIZE-1;

    return 0;
}

/**
 * Operations vector. Please don't rename it, as the
 * skeleton code in misc.c assumes it is named 'fs_ops'.
 */
struct fuse_operations fs_ops = {
    .init = fs_init,
    .getattr = fs_getattr,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .releasedir = fs_releasedir,
    .mknod = fs_mknod,
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .release = fs_release,
    .statfs = fs_statfs,
};

