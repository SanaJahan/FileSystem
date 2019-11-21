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
#include <fuse/fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>
#include <fuse/fuse_common.h>


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
    int retVal = 0;     // return value is inode of child file or directory

    // move pointer to inode with inode number inum
    struct fs_inode *my_inode = inodes + inum;

    int num_blocks = 1;     // directory size is always 1 block
    int starting_block = my_inode->direct[0];

    // allocate ptr to point to the block which is the directory on disk
    void* ptr = malloc(num_blocks * FS_BLOCK_SIZE);
    if(disk->ops->read(disk, starting_block, num_blocks, ptr) < 0){
        return EIO;
        exit(2);
    }
    struct fs_dirent* dp = ptr;     // set dp to ptr (pointer type fs_dirent*)
    // iterate through all entries in the directory to find a match
    for(int i=0; i < 32 * num_blocks; i++){
        dp = dp + 1;
        if(dp->valid && strcmp(dp->name, name)==0){
            retVal = dp->inode;
            free(ptr);
            return retVal;
        }
    }
    free(ptr);
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
    printf("No. of tokens: %d\n", num_tokens);
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
	free(block);
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
	char *tokens[64] = {NULL};
	char *_path = strdup(path);
	int n_tokens = parse(_path, tokens, 64);
	memset(leaf, 0, FS_FILENAME_SIZE);
	if (n_tokens > 0){
		strncpy(leaf, tokens[n_tokens - 1], strlen(tokens[n_tokens - 1]));
	} else {
		return -ENOENT; // if no tokens available
	}
	int index = 0;
	struct fs_inode temp;
	struct fs_dirent *fd;
	char *token = tokens[index];
	int inum = 1;
	void *block = malloc(FS_BLOCK_SIZE);
	while(tokens[index + 1] != NULL) {
		temp = inodes[inum];
		if (disk->ops->read(disk, temp.direct[0], 1, block)) {
			exit(2);
		}
		fd = block;
		int i = 0;
		for (i = 0; i < DIRENTS_PER_BLK; i++) {
			if (fd->valid && !strcmp(token, fd->name)) {
				if (tokens[i + 1] != NULL && !(fd->isDir)) {
					free(block);
					return -ENOTDIR;
				}
				inum = fd->inode;
				break;
			}
			fd++;
		}
		if (i == DIRENTS_PER_BLK) {
			free(block);
			return -ENOENT;
		}
		token = tokens[++index];
	}

	if (block) {
		free(block);
	}
    return inum;
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
	for (int i = n_meta; i < n_blocks; i++) {
		if (!FD_ISSET(i, block_map)) {
			FD_SET(i, block_map);
			return i;
		}
	}
	return -ENOSPC;

}

/**
 * Return a block to the free list
 *
 * @param  blkno the block number
 */
static void return_blk(int blkno)
{
	if (blkno > n_meta) {
		FD_CLR(blkno, block_map);
	}

}

/**
 * Gets a free inode number from the free list.
 *
 * @return a free inode number or 0 if none available
 */
static int get_free_inode(void)
{
	for (int i = 0; i < n_inodes; i++) {
		if (!FD_ISSET(i, inode_map)) {
			FD_SET(i, inode_map);
			return i;
		}
	}

return -ENOSPC;

}

/**
 * Return a inode to the free list.
 *
 * @param  inum the inode number
 */
static void return_inode(int inum)
{
	if (inum >= 0 && inum < n_inodes) {
		FD_CLR(inum, inode_map);
	}
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
    for (int entryNum = 0; entryNum < PTRS_PER_BLK; entryNum++){
        if (strcmp(de->name, name) == 0){
            return entryNum;
        }
        de++;
    }
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
    for (int i = 0; i < PTRS_PER_BLK; i++){
        if (strcmp(de->name, name)==0){
            return de->inode;
        }
        de++;
    }
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
    for (int entryNum = 0; entryNum < PTRS_PER_BLK; entryNum++){
        if ((de->valid)==0){
            return entryNum;
        }
        de++;
    }
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
    for (int i = 0; i < PTRS_PER_BLK; i++){
        if (de->valid==1){
            return 0;
        }
        de++;
    }
    return 1;
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
 * fs_init reads the super block
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
    // read the bitmap into memory and hold it there
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
    inodes = malloc(sb.inode_region_sz * FS_BLOCK_SIZE);// how many blocks allocated to inodes X no. of inodes
    if (disk->ops->read(disk, inode_base, sb.inode_region_sz, inodes) < 0) {
        exit(1);
    }

    // number of blocks on device
    n_blocks = sb.num_blocks;

    // number of metadata blocks
    n_meta = inode_base + sb.inode_region_sz;

    // allocate array of dirty metadata block pointers
    dirty = calloc(n_meta, sizeof(void*));  // ptrs to dirty metadata blks

    //dirty bits : that data eventually should be wriiten out to disks.
    //whihc means if we made a change to the blocks and changed the inode from allocated to alocated and
    // didnt write ot the disks yet, we mark it dirty

    /* your code here */
    int result = fs_rmdir("dir1");
    printf("rmdir %d", result);

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

static void fs_set_stat(struct stat *sb, struct fs_inode _inode, int inum) {

	// stat is used to pull the information for the details of the inode
	// it is mainly used in the ls command
    memset(sb,0,sizeof(*sb));
    sb->st_dev = 0;                    /* ID of device containing file */
    sb->st_ino = inum;                /* inode number */
    sb->st_mode = _inode.mode;     /* protection */
    sb->st_nlink = 1;                  /* number of hard links */
    sb->st_uid = _inode.uid;       /* user ID of owner */
    sb->st_gid = _inode.gid;       /* group ID of owner */
    sb->st_rdev = 0;                   /* device ID (if special file) */
    sb->st_size = _inode.size;     /* total size, in bytes */
    sb->st_blksize = FS_BLOCK_SIZE;    /* block size for file system I/O */
    sb->st_blocks = (_inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;  /* number of 512B blocks allocated */
    sb->st_atime = _inode.mtime;   /* time of last access */
    sb->st_mtime = _inode.mtime;   /* time of last modification */
    sb->st_ctime = _inode.ctime;   /* time of last status change */

}

static int fs_getattr(const char *path, struct stat *sb)
{
    int inode_num = translate(path); // read the inode number from the given path

    // if the entry is not there then return the inode value
    if (inode_num != -ENOENT || inode_num != -ENOTDIR) {
    	struct fs_inode _inode = inodes[inode_num];
    	fs_set_stat(sb, _inode, inode_num);
        return 0;
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

	/* invalid argument when len is not zero */
    if (len != 0) {
    	return -EINVAL;
    }

    int index;
    int inum = translate(path);
    char *dir_name = strrchr(path, '/') + 1;


    if (inum < 0) {
        return -ENOENT;
    } else if (S_ISDIR(inodes[inum].mode)) {
        return -EISDIR;
    }


    struct fs_inode *inode = inodes[inum];

    // clear the block of the inode
    for (int i = 0; i < N_DIRECT; i++) {
        if (inode->direct[i])
            FD_CLR(inode->direct[i], block_map);
        inode->direct[i] = 0;
    }

    // clear indirect blocks if it exists
    if (inode->indir_1) {
        truncate_indir_1(inode->indir_1);
    }

    // clear indir 2 blocks if it exists
    if (inode->indir_2) {
    	truncate_indir_2(inode->indir_2);
    }

    // set size
    inode->size    = 0;
    inode->indir_1 = 0;
    inode->indir_2 = 0;

    //write back to the device
    write_all_inodes();

    if (disk->ops->write(disk, inode_map_base, block_map_base - inode_map_base, inode_map) < 0)
        exit(1);
    if (disk->ops->write(disk,block_map_base,inode_base - block_map_base,block_map) < 0)
        exit(1);

    return 0;
}

static void truncate_indir_1(int blk_num) {

	int num_per_blk = BLOCK_SIZE / sizeof(uint32_t);

    // read from blocks
    uint32_t buffer[num_per_blk];

    memset(buffer, 0, BLOCK_SIZE);

   // read directory
    if (disk->ops->read(disk, blk_num, 1, buffer) < 0)
        exit(1);

    // clear the blocks
    for (int i = 0; i < num_per_blk; i++) {
        if (buffer[i])
            FD_CLR(buffer[i], block_map);
    }

    FD_CLR(blk_num, block_map);
}

/* clear the indir2 blocks of file
 */
static void truncate_indir_2(int blk_num) {
	int num_per_blk = BLOCK_SIZE / sizeof(uint32_t);

    // read from blocks
    uint32_t buffer[num_per_blk];

    memset(buffer, 0, BLOCK_SIZE);

    // read directory
    if (disk->ops->read(disk, blk_num, 1, buffer) < 0)
        exit(1);

    // clear the blocks
    for (int i = 0; i < num_per_blk; i++) {
        if (buffer[i])
            truncate_indir_1(buffer[i]);
    }

    FD_CLR(blk_num, block_map);
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
static int fs_unlink(const char *path)
{
    return -EOPNOTSUPP;
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

    // in case of trying to remove root dir
    if (strcmp(path, "/") == 0)
        return -EINVAL;


    char buffer[1024];

    // get the inode of the file or directory
    int inode = translate(path);


    char *dir_name = strrchr(path, '/') + 1;


    if (inode < 0) {
        return -ENOENT;
    } else if (!S_ISDIR(inodes[inode].mode)) {
        return -ENOTDIR;
    } else if (!is_empty_dir(inode)) {
        return -ENOTEMPTY;
    }

    struct fs_dirent *dirEntry =  (struct fs_dirent*) malloc(FS_BLOCK_SIZE);

    // read the disk if its empty

    if (disk->ops->read(disk, inodes[inode].direct[0], 1, dirEntry) < 0) {
        exit(1);
    }

    //remove the directory
    for (int i = 0; i <= 31; i++) {
    	// check if the entry is valid
        if (dirEntry[i].valid && (strcmp(dirEntry[i].name, dir_name) == 0)) {
        	dirEntry[i].valid = 0;
            // mark the inode as free
            FD_CLR(dirEntry[i].inode, inode_map);
            break;
        }
    }

    // change the modification time to null
    inodes[inode].mtime = time(NULL);

    // free the inode for the directory
    FD_CLR(inodes[inode].direct[0], block_map);

    // write directory
    if (disk->ops->write(disk, inodes[inode].direct[0], 1, dirEntry) < 0) {
        exit(1);
    }

    write_all_inodes();

    free(dirEntry);
    return 0;
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
 *   -EINVAL   - source and destination not in thinite same directory
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


//write all the inode to the disk
static void write_all_inodes() {
	struct fs_super sb;
    disk->ops->write(disk, (1 + sb.inode_map_sz + sb.block_map_sz), sb.inode_region_sz, inodes);
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
		return 0;
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
	(void) path;
	close(fi->fh);
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
    .rmdir = fs_rmdir,//sana
    .rename = fs_rename,
    .chmod = fs_chmod,
    .utime = fs_utime,
    .truncate = fs_truncate,//sana
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .release = fs_release,
    .statfs = fs_statfs,
};

