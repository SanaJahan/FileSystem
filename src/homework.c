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
#include <math.h>

#include "fsx600.h"
#include "blkdev.h"


//extern int homework_part;       /* set by '-part n' command-line option */


#define MAX_ENTRIES_DIR (FS_BLOCK_SIZE / sizeof(struct fs_dirent)) // the max number of dir entry per block
#define MAX_LENGTH_OF_DIR_NAME 26 // max length of a directory's name
#define INDIRECT_BOUND (N_DIRECT + ADDR_PER_BLOCK) //the max blocks that direct and indir1 can contain
#define SIZE_DOUBLE_INDIRECT (INDIRECT_BOUND + (ADDR_PER_BLOCK * ADDR_PER_BLOCK)) // the block number a inode can contain
#define ADDR_PER_BLOCK (FS_BLOCK_SIZE / sizeof(uint32_t)) // the max number of address a block can contain

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

/** block map size*/
static int block_map_sz;

/** inode map size*/
static int inode_map_sz;

/** inode region size*/
static int inode_reg_sz;

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
	struct fs_inode *theInode = (inodes + inum);
	void *start = malloc(FS_BLOCK_SIZE);
	if (disk->ops->read(disk, theInode->direct[0], 1, start) < 0) {
		return -EIO;
	}

	struct fs_dirent* dir = start;
	for (int i = 0 ; i < DIRENTS_PER_BLK; i++) {
		struct fs_dirent* directory_entry = dir + i;
		const char *TheName = directory_entry->name;
		if (directory_entry->valid &&strcmp(name, TheName) == 0) {
			free(start);
			return directory_entry->inode;
		}
	}
	free(start);
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
            i++;
            num_tokens++;
        }
        token = strtok(NULL, delim);


    }
	return num_tokens;
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
			return -ENOENT;
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
	/* parse the path into tokens*/
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
	/* keep search the directory till the second last token*/
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
 * Flush all the inode map to disk
 */
static void flush_inode_map(void) {
	disk->ops->write(disk, 1, inode_map_sz, inode_map);
}

/**
 * Flush all the block map to disk
 */
static void flush_block_map(void) {
	disk->ops->write(disk, 1 + inode_map_sz, block_map_sz, block_map);
}

/*
 * Write all inodes to disk
 */
static void write_all_inodes() {
    disk->ops->write(disk, (1 + inode_map_sz + block_map_sz), inode_reg_sz, inodes);
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
	return 0;
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

	return 0;
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
        if (!de->valid){
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


/** Helper function for getBlk which allocates the nth block if it does not exist */

static int allocate_nth_block(struct fs_inode *in, int n){

    int ret = 0;       // initialize ret

    // if n < 6, allocate a free block at nth index
    if(n < N_DIRECT){
        int freeBlk = get_free_blk();
        if(freeBlk==0){
            return -ENOSPC;
        }
        in->direct[n] = freeBlk;
        write_all_inodes();
        ret = freeBlk;
    }

    // n is > 6 or < 262, allocate a block in the indir1 blocks
    else if(n >= N_DIRECT && n < N_DIRECT + PTRS_PER_BLK){

        // if this is the very first block in the indir1 blocks, first allocate block for indir1. Then allocate data block inside indir1
        uint32_t indir_block_1 = in->indir_1;

        if (n == N_DIRECT) {            // when this is the very first indir block
            int freeBlkForIndir1 = get_free_blk();
            if(freeBlkForIndir1==0){
                return 0;
            }
            indir_block_1 = freeBlkForIndir1;
            in->indir_1 = indir_block_1;         // allocate block for indir1
            write_all_inodes();
        }

        int* ptr_to_indir1 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_1, 1, ptr_to_indir1) < 0) {
            exit(2);
        }

        // find free block from block map
        int freeBlkNum = get_free_blk();            // allocate block for data in indir1
        if (freeBlkNum == 0) {
            return 0;
        }
        // add free block to the nth block position
        ptr_to_indir1[n-N_DIRECT] = freeBlkNum;
        disk->ops->write(disk, indir_block_1, 1, ptr_to_indir1);
        ret = freeBlkNum; // returns nth block from indirect blocks
        free(ptr_to_indir1);

    }

    // if n >= 262, allocate a free block to the indir2 blocks
    else if (n >= (N_DIRECT + PTRS_PER_BLK)) {

        uint32_t indir_block_2 = in->indir_2;

        // if nth block is the very first block in the indir2 blocks, first allocate a block for indir2
        if( n == N_DIRECT + PTRS_PER_BLK) {
            int freeBlkForIndir2 = get_free_blk();      // allocate block for indir2
            if (freeBlkForIndir2 == 0) {
                return 0;
            }
            indir_block_2 = freeBlkForIndir2;
            in->indir_2 = freeBlkForIndir2;   // update in inode
            write_all_inodes();
        }

        // if this is not the very first indir2 block, perform computation to find correct location to add free block
        int adjusted_n = n - (N_DIRECT + PTRS_PER_BLK);

        int* ptr_to_indir2 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_2, 1, ptr_to_indir2) < 0) {
            exit(2);
        }

        // find index at which we will allocate a block inside indir2 block
        double index_from_indir2 = ceil(adjusted_n / PTRS_PER_BLK);
        // convert to int = 3 for dereferencing
        int index_from_indir2_int = (int)(index_from_indir2);

        int freeBlkInsideIndir2 = 0;
        if ( adjusted_n % PTRS_PER_BLK == 0) {
            // get free block to allocate inside indir2
            freeBlkInsideIndir2 = get_free_blk();       // first level block alloc
            if (freeBlkInsideIndir2 == 0) {
            	free(ptr_to_indir2);
                return 0;
            } else {
                ptr_to_indir2[index_from_indir2_int] = freeBlkInsideIndir2;
                disk->ops->write(disk,indir_block_2 , 1, ptr_to_indir2);
            }
        } else {
        	freeBlkInsideIndir2 = ptr_to_indir2[index_from_indir2_int];
        }

        int* myptr = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, freeBlkInsideIndir2 , 1, myptr) < 0) {
            exit(2);
        }
        // find the final target location where our free data block will be allocated
        int target_idx = adjusted_n % PTRS_PER_BLK;

        int finalindir2FreeBlk = get_free_blk();       // second level data block allocated
        if(finalindir2FreeBlk == 0) {
            return -ENOSPC;
        }
        myptr[target_idx] = finalindir2FreeBlk;
        disk->ops->write(disk, freeBlkInsideIndir2, 1,myptr);
        free(myptr);
        free(ptr_to_indir2);
        ret = finalindir2FreeBlk;
    }
    flush_block_map();
    return ret;
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
    // Let's assume n=1000 for inode 7 (size = 276177 bytes)
    float file_size_bytes = (float)in->size;
    // convert bytes to block size
    float file_size_blocks = ceil(file_size_bytes / FS_BLOCK_SIZE);

    /** adding code to check if nth block exists */
    // if nth block does not exist & alloc = 1, then allocate a free block to nth index
    if (file_size_blocks - 1 < n && alloc == 1) {
        int ret = allocate_nth_block(in, n);
        return ret;
    }
    // if nth block does not exist & alloc = 0, return error


    int retVal = -1;    // initialize retVal to -1 which is invalid block no
    // if n is between 0 to 5, access the direct blocks = 6 blocks
    if (n < N_DIRECT) {
        retVal = in->direct[n];
    }
    // if n is between 6 to 255: access indirect1 block
    else if (n >= N_DIRECT || n < N_DIRECT + PTRS_PER_BLK) {
        int indir_block_1 = in->indir_1;
 //       printf("Ptrs per blk: %d, indirBlk: %d\n", PTRS_PER_BLK, indir_block_1);
        int* ptr_to_indir1 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_1, 1, ptr_to_indir1) < 0) {
            exit(2);
        }
        retVal = ptr_to_indir1[n-N_DIRECT]; // returns nth block from indirect blocks
    }
    // else if n = 256 or greater : access the indir2 block
    else if (n >= (N_DIRECT + PTRS_PER_BLK)) {
        // if n = 1000, adjusted_n = 1000 - 262 = 738
        int adjusted_n = n - (N_DIRECT + PTRS_PER_BLK);
        // find indirect2 block num & read the block
        int indir_block_2 = in->indir_2;
        int* ptr_to_indir2 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_2, 1, ptr_to_indir2) < 0) {
            exit(2);
        }
        // find the block index in indirect2 block that we will read, get ceil
        // ceil(738 / 256) = 3.0
        double index_from_indir2 = ceil(adjusted_n / PTRS_PER_BLK);
        // convert to int = 3 for dereferencing
        int index_from_indir2_int = (int)(index_from_indir2);
        // get block number that we will finally read from (second level block)
        // i.e. read from the third block in indirect2
        int block_num_in_indirect = ptr_to_indir2[index_from_indir2_int];
        int* myptr = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, block_num_in_indirect , 1, myptr) < 0) {
            exit(2);
        }
        // target index = 738 % 256 = 226. Hence, we read the block at index 226 from the 3rd block that we had located
        int target_idx = adjusted_n % PTRS_PER_BLK;

        retVal = myptr[target_idx];

    }
    return retVal;
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
    //inode map size
    inode_map_sz = sb.inode_map_sz;
    block_map_sz = sb.block_map_sz;
    inode_reg_sz = sb.inode_region_sz;

    return NULL;
}



/**
 * fs_set_attrs - set attrs from inode to sb
 *
 * @param pointer to inode
 * @param sb pointer to stat struct
 *
 */
static void fs_set_attrs(struct fs_inode *inode, struct stat *sb, int inum) {
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
static int fs_getattr(const char *path, struct stat *sb)
{
    int inode_num = translate(path); // read the inode number from the given path

    if (inode_num == -ENOENT || inode_num == -ENOTDIR) {
    	return inode_num;
    }

    struct fs_inode the_inode = inodes[inode_num];
    fs_set_attrs(&the_inode, sb, inode_num);

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


	//request memory of block sizes
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
	char file_name[FS_FILENAME_SIZE];
	char *_path = strdup(path);
	int child_inode = translate(_path);
	_path = strdup(path);
	int parent_inode = translate_1(_path, file_name);


	/*
	 * If parent path contains a intermediate component which is not a directory
	 * or a component of the path is not present
	 */
	if(parent_inode == -ENOTDIR || parent_inode == -ENOENT) {
		return parent_inode;
	}
	struct fs_inode p_inode = inodes[parent_inode];
	if (!S_ISDIR(p_inode.mode)) {
		return ENOTDIR;
	}

	/*
	 * If child file alreayd exist
	 */
	if (child_inode > 0) {
		printf("In mknode function...child_file exist, child inode is: %d (checking existence)\n", child_inode);
		return EEXIST;
	}

	int free_inode = get_free_inode();
	if (free_inode == 0) {
		return -ENOSPC;
	}
	/*
	 * create inode in the parent directory
	 */
	void *parent_directory_block = malloc(FS_BLOCK_SIZE);
	if (disk->ops->read(disk, p_inode.direct[0], 1, parent_directory_block) < 0) {
		exit(2);
	}
	struct fs_dirent* entry = parent_directory_block;
	struct fs_dirent* entryStart = entry;
	struct fs_inode new_inode = inodes[free_inode];
	int free_entry_idx = find_free_dir(entryStart);

	if (free_entry_idx == -ENOSPC) {
		if (parent_directory_block) {
			free(parent_directory_block);
		}
		if (FD_ISSET(free_inode, inode_map)) {
			FD_CLR(free_inode, inode_map);
			flush_inode_map();
		}
		return -ENOSPC;
	}

	//create new directory entry and inode of the entry
	entry = entry + free_entry_idx;
	memset(entry, 0, sizeof(struct fs_dirent));
	strncpy(entry->name, file_name, strlen(file_name));
	entry->valid = 1;
	entry->isDir = S_ISDIR(mode);
	entry->inode = free_inode;

	/*set the inode attributes*/
	struct fuse_context *context = fuse_get_context();
	time_t current_time = time(NULL);
	new_inode.ctime = current_time;
	new_inode.mtime = current_time;
	new_inode.gid = context->gid;
	new_inode.uid = context->uid;
	new_inode.mode = mode;


	for (int i = 0; i < 6; i++) {
		new_inode.direct[i] = 0;
	}
	new_inode.indir_1 = 0;
	new_inode.indir_2 = 0;
	/*allocate a block to the directory if the inode is a directory*/
	if (S_ISDIR(mode)) {
		int free_block = get_free_blk();
		if (free_block == 0) {
			return_inode(free_inode);
			flush_inode_map();
			free(parent_directory_block);
			return -ENOSPC;
		}
		new_inode.direct[0] = free_block;
		flush_block_map();

		void *block_dir = malloc(FS_BLOCK_SIZE);
		memset(block_dir, 0, FS_BLOCK_SIZE);
		disk->ops->write(disk, free_block, 1, block_dir);
		free(block_dir);
	}

	/*flush to disk*/
	flush_inode_map();
	inodes[free_inode] = new_inode;
	write_all_inodes();
	disk->ops->write(disk,p_inode.direct[0], 1, parent_directory_block);

	free(parent_directory_block);
    return 0;
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
    mode = mode | S_IFDIR; // make it directory mode
    int ret = fs_mknod(path, mode, 0);
    return ret;
}

/*
 * Truncate the block in indir1 in inode struct
 * @param blk_num, the blk num of indir1
 */
static void truncate_indir_1(int blk_num) {

	int num_per_blk = FS_BLOCK_SIZE / sizeof(uint32_t);

    //read from block
    uint32_t buffer[num_per_blk];

    memset(buffer, 0, FS_BLOCK_SIZE);

    //read directory
    if (disk->ops->read(disk, blk_num, 1, buffer) < 0)
        exit(1);
    //clear the blocks
    for (int i = 0; i < num_per_blk; i++) {
        if (buffer[i])
        	return_blk(buffer[i]);
    }
    FD_CLR(blk_num, block_map);
}

/*
 * Truncate the block in indir2 in inode struct
 * @param blk_num, the blk num of indir2
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

    return_blk(blk_num);
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
	/* invalid argument when len is not zero */
    if (len != 0) {
    	return -EINVAL;
    }

    int inum = translate(path);


    if (inum < 0) {
        return inum;  //two possible errors
    } else if (S_ISDIR(inodes[inum].mode)) {
        return -EISDIR;
    }

    struct fs_inode *inode = &inodes[inum];

    // clear the block of the inode
    for (int i = 0; i < N_DIRECT; i++) {
        if (inode->direct[i])
        	return_blk(inode->direct[i]);
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
    flush_inode_map();
    flush_block_map();

    return 0;
}


/**
 * get_leaf - get the leaf name
 * @param the path of the file
 */

static char *get_leaf(char *path) {

    char *token = NULL;
    char *tokens[64] = {NULL};
    int i = 0;
    //tokenize the string
    token = strtok(path, "/");
    while (token) {
        tokens[i++] = token;
        token = strtok(NULL, "/");
    }

    return tokens[--i];
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
    int i = 0;
    int inum = fs_truncate(path, 0);
    if (inum < 0)
        return inum;

    char dir_name[FS_FILENAME_SIZE];
    char *Apath =(char*) path;
    int parent_inum = translate_1(Apath, dir_name);
    Apath =(char *) path;
    char *last = dir_name;

    last = get_leaf(Apath);


    struct fs_inode parent_dir = inodes[parent_inum];
    struct fs_dirent *block = (struct fs_dirent *) malloc(FS_BLOCK_SIZE);
    disk->ops->read(disk, parent_dir.direct[0], 1, block);


    // find the inode entry and clear it
    for (i = 0; i < 32; i++) {

        if (block[i].valid == 0) {
            continue;
        }

        if (strcmp(block[i].name, last) == 0) {
        	//delete it
            if (block[i].isDir) {
                return -EISDIR;
            }
            int file_inode_num = block[i].inode;
            struct fs_inode fileinode = inodes[file_inode_num];
            if (FD_ISSET(file_inode_num, inode_map)) {
                //clear the inode
            	FD_CLR(file_inode_num, inode_map);
                fileinode.size = 0;
                fileinode.mtime = time(NULL);
                block[i].valid = 0;
                inodes[file_inode_num] = fileinode;
                break;
            }
        }
    }


    if (i > 31) {
        return -ENOENT;
    }

    //write everything back to disk(inode map and inodes)
    disk->ops->write(disk, parent_dir.direct[0], 1, block);
    write_all_inodes();
    flush_inode_map();
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
    // in case of trying to remove root dir
        if (strcmp(path, "/") == 0)
        return -EINVAL;


	char* _path = strdup(path);

    // get the inode of the directory
    int inode = translate(_path);

    struct fs_dirent *dirEntry =  (struct fs_dirent*) malloc(FS_BLOCK_SIZE);

    if (disk->ops->read(disk, inodes[inode].direct[0], 1, dirEntry) < 0) {
            exit(2);
     }

    if (inode < 0) {
        return -ENOENT;
    } else if (!S_ISDIR(inodes[inode].mode)) {
        return -ENOTDIR;
    } else if (!is_empty_dir(dirEntry)) {
        return -ENOTEMPTY;
    }

	// clear the parent's block
	memset(dirEntry, 0, FS_BLOCK_SIZE);

	if (disk->ops->write(disk, inodes[inode].direct[0], 1, dirEntry) < 0){
		exit(1);
	}

	return_blk(inodes[inode].direct[0]);

	inodes[inode].direct[0] = 0;
	if (disk->ops->write(disk, block_map_base, 1, block_map) < 0){
		exit(1);
	}

	char* leaf = malloc(sizeof(char*));
	int parent_inum = translate_1(_path, leaf);
	if(parent_inum < 0){
		return parent_inum;
	}
	//get the parent inode
	struct fs_inode* parent_node = &inodes[parent_inum];
	if(!S_ISDIR(parent_node->mode)){
		return -ENOTDIR;
	}
	//get the dirents of parent inode

	struct fs_dirent* dirents = calloc(1, FS_BLOCK_SIZE);

	if (disk->ops->read(disk, parent_node->direct[0], 1, dirents) < 0){
		exit(2);
	}

	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if(dirents[i].valid && strcmp(dirents[i].name, leaf) == 0){
			dirents[i].valid = 0;
			return_blk(inode);
			break;
		}
	}

	if (disk->ops->write(disk, parent_node->direct[0], 1, dirents) < 0){
        exit(1);
	}

	if(disk->ops->write(disk, inode_map_base, block_map_base - inode_map_base, inode_map) < 0) {
        exit(1);
	}

	free(dirents);
	free(dirEntry);
	free(leaf);
	free(_path);
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
 *   -EINVAL   - source and destination not in the same directory
 *
 * @param src_path the source path
 * @param dst_path the destination path.
 * @return 0 if successful, or -error number
 */
static int fs_rename(const char *src_path, const char *dst_path)
{
    //get the old name from src_path
    char old_name[FS_FILENAME_SIZE];
    char *tmp_path = strdup(src_path);
    int prev_pinum = translate_1(tmp_path, old_name);
    //rewind
    tmp_path = strdup(src_path);
    int curr_inum = translate(tmp_path);

    //get the new name
    char new_name[FS_FILENAME_SIZE];
    tmp_path = strdup(dst_path);

    int new_pinum = translate_1(tmp_path, new_name);

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

    //check if dst is not present
    int i = 0;
    for (i = 0; i < DIRENTS_PER_BLK; i++) {
        if (!strcmp(entry->name, new_name)) {
            if (block) {
                free(block);
            }
            return -EEXIST;
        }
        entry++;
    }

    //update name of matching inode
    entry = block;
    for (i = 0; i < DIRENTS_PER_BLK; i++) {
        if (entry->inode == curr_inum) {
            strncpy(entry->name, new_name, strlen(new_name));
            struct fs_inode inode = inodes[curr_inum];
            inode.ctime = time(NULL);
            inodes[curr_inum] = inode;
            write_all_inodes();
            break;
        }
        entry++;
    }

    //write back to disk
    disk->ops->write(disk, parent_inode.direct[0], 1, block);

    //free allocated memory
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

    // get inode number & find instantiate inode
    int inum = translate(path);
    // if path not valid, return error
    if(inum == ENOENT){
        return -ENOENT;
    }
    // get inode
    struct fs_inode* my_inode = inodes + inum;
    int my_inode_size = my_inode->size;

    // if offset & len requested exceeds file size, reset len
    if(offset + len > my_inode_size){
        //return EIO;
        len = my_inode_size - offset;
    }

    // find the first byte to begin reading
    int firstByteToRead = offset;
    // find the last byte to be read in file
    int lastByteToRead = firstByteToRead + len;

    // if offset exceeds the file size, read the number of bytes that are available to read from start to end of file (needed in case of repeated calls to read file in small increments)
    if(offset > my_inode_size){
        len = lastByteToRead - firstByteToRead;
    }

    // find starting block to read - gets the nth block in the file
    float startBlkIndex = (floor(firstByteToRead / FS_BLOCK_SIZE));
    // find last block to read
    float lastBlkIndex = (floor(lastByteToRead / FS_BLOCK_SIZE));


    // find total no. of blocks we will read
    int numBlocksToRead = (int)(lastBlkIndex - startBlkIndex + 1);


    void* myPtr = malloc(FS_BLOCK_SIZE);
    int numBytes = 0;
    int bytesToCopy = 0;

    for(int i=0; i<numBlocksToRead; i++){

        int entryBlockNum = get_blk(my_inode, startBlkIndex, 0);

        if (disk->ops->read(disk, entryBlockNum, 1, myPtr) < 0) {
            exit(2);
        }
        if(i==0){
            // copies bytes from myPtr to buf, excludes no. of bytes in offset
            offset = offset % FS_BLOCK_SIZE;
            bytesToCopy = FS_BLOCK_SIZE-offset;
            memcpy(buf, myPtr + offset, bytesToCopy);

        }
        else if(i==numBlocksToRead-1){
            bytesToCopy = len - numBytes;
            memcpy(buf, myPtr, bytesToCopy);
        }
        else{
            bytesToCopy = FS_BLOCK_SIZE;
            memcpy(buf, myPtr, bytesToCopy);
        }
        numBytes = numBytes + bytesToCopy;
        buf =  buf + bytesToCopy;
        startBlkIndex++;
    }
    free(myPtr);
    return numBytes;
//
//    int num_copied = 0;
//
//    struct stat sb;
//    int ret = fs_getattr(path, &sb);
//
//    /* error-checking for path resolution */
//    if (ret == -ENOENT || ret == -ENOTDIR) {
//        return ret;
//    }
//
//    /* if offset >= file len */
//    int n_blocks = offset / FS_BLOCK_SIZE;
//    if (n_blocks >= SIZE_DOUBLE_INDIRECT) {
//        return 0;
//    }
//
//    struct fs_inode inode = inodes[sb.st_ino];
//
//    /* if given path is a directory instead of file */
//    if (S_ISDIR(inode.mode)) {
//        return -EISDIR;
//    }
//
//    void *block = malloc(FS_BLOCK_SIZE);
//    void *indirect_1 = malloc(FS_BLOCK_SIZE);
//    void *indirect_2 = malloc(FS_BLOCK_SIZE);
//
//    int i = 0;
//    int cnt_offset = 0;
//
//    // handling in different modular blocks as professor mentioned
//    if (n_blocks < 6) {
//        goto handle_direct_blocks;
//    } else if (n_blocks >= 6 && n_blocks < 262) {
//        goto handle_indirect_blocks;
//    } else if (n_blocks >= 262 && n_blocks < SIZE_DOUBLE_INDIRECT) {
//        goto handle_double_indirect_blocks;
//    }
//
//// this handles the direct block writing.
//    handle_direct_blocks:
//    i = n_blocks;
//    cnt_offset = n_blocks * FS_BLOCK_SIZE;
//    for (; i < 6; i++) {
//        int j = 0;
//        disk->ops->read(disk, inode.direct[i], 1, block);
//        char *string = block;
//        for (j = 0; j < FS_BLOCK_SIZE; j++) {
//            if (cnt_offset < offset)
//                cnt_offset++;
//            else {
//                if (len > 0 && (offset + num_copied) < inode.size) {
//                    // add data to the buffer from str
//                    *buf = *(string + j);
//                    num_copied++;
//                    len--;
//                } else {
//                    goto cleanup;
//                }
//                buf++;
//            }
//        }
//    }
//
//// this handles indirect level 1 blocks
//    handle_indirect_blocks:
//    i = 0;
//    disk->ops->read(disk, inode.indir_1, 1, indirect_1);
//    int *blocks = indirect_1;
//    if (n_blocks >= 6) {
//        i = n_blocks - 6;
//        blocks += i;
//    }
//    cnt_offset = (i + 6) * FS_BLOCK_SIZE;
//    for (; i < ADDR_PER_BLOCK; i++) {
//        int j = 0;
//        disk->ops->read(disk, *blocks, 1, block);
//        char *string = block;
//        for (j = 0; j < FS_BLOCK_SIZE; j++) {
//            if (cnt_offset < offset)
//                cnt_offset++;
//            else {
//                if (len > 0 && (offset + num_copied) < inode.size) {
//                    *buf = *(string + j);
//                    num_copied++;
//                    len--;
//                } else {
//                    goto cleanup;
//                }
//                buf++;
//            }
//        }
//        blocks++;
//    }
//
//// this handles indirect level 2 blocks
//    handle_double_indirect_blocks:
//    i = 0;
//    int j = 0;
//    disk->ops->read(disk, inode.indir_2, 1, indirect_2);
//    int *indirect_level1_blocks = indirect_2;
//    if (n_blocks >= 262) {
//        i = (n_blocks - 262) / ADDR_PER_BLOCK;
//        indirect_level1_blocks += i;
//        j = (n_blocks - 262) % ADDR_PER_BLOCK;
//
//    }
//    cnt_offset = (262 + (i * ADDR_PER_BLOCK)) * FS_BLOCK_SIZE;
//    for (; i < ADDR_PER_BLOCK; i++) {
//        disk->ops->read(disk, *indirect_level1_blocks, 1, indirect_1);
//        int *indirect_level2_blocks = indirect_1;
//        indirect_level2_blocks += j;
//        cnt_offset += j * FS_BLOCK_SIZE;
//        for (; j < ADDR_PER_BLOCK; j++) {
//            int k = 0;
//            disk->ops->read(disk, *indirect_level2_blocks, 1, block);
//            char *string = block;
//            for (k = 0; k < FS_BLOCK_SIZE; k++) {
//                if (cnt_offset < offset)
//                    cnt_offset++;
//                else {
//                    if (len > 0 && (offset + num_copied) < inode.size) {
//                        *buf = *(string + k);
//                        num_copied++;
//                        len--;
//                    } else {
//                        goto cleanup;
//                    }
//                    buf++;
//                }
//            }
//            indirect_level2_blocks++;
//        }
//        j = 0;
//        indirect_level1_blocks++;
//    }
//
//// this is the cleanup. free's the blocks and additional variables.
//    cleanup:
//    *buf = '\0';
//    if (block) {
//        free(block);
//    }
//
//    if (indirect_1) {
//        free(indirect_1);
//    }
//
//    if (indirect_2) {
//        free(indirect_2);
//    }
//
//    return num_copied;
}


//    // get inode number & find instantiate inode
//    int inum = translate(path);
//    // if path not valid, return error
//    if(inum == ENOENT){
//        return ENOENT;
//    }
//    // get inode
//    struct fs_inode* my_inode = inodes + inum;
//    int my_inode_size = my_inode->size;
//    if (offset > my_inode_size - 1) {
//    	return 0;
//    }
//   // printf("In fs_read File size: %d, offset: %lld\n", my_inode_size, offset);
//    if(offset + len > my_inode_size){
//        return EIO;
//    }
//
//    // find the first byte to begin reading
//    int firstByteToRead = offset;
//    // find the last byte to be read in file
//    int lastByteToRead = firstByteToRead + len;
//    printf("First byte: %d, last byte: %d\n", firstByteToRead, lastByteToRead);
//    // find starting block to read - gets the nth block in the file
//    float startBlkIndex = (floor(firstByteToRead / FS_BLOCK_SIZE));
//    // find last block to read
//    float lastBlkIndex = (floor(lastByteToRead / FS_BLOCK_SIZE));
//    printf("First block to read: %d, Last block: %d\n", (int)startBlkIndex, (int)lastBlkIndex);
//
//
//    // find total no. of blocks we will read
//    int numBlocksToRead = (int)(lastBlkIndex - startBlkIndex + 1);
//    printf("Total blocks to read: %d\n", numBlocksToRead);
//
//
//    void* myPtr = malloc(FS_BLOCK_SIZE);
//    int numBytes = 0;
//    int bytesToCopy = 0;
//
//    for(int i=0; i < numBlocksToRead; i++){
//
//        //off_t adjustedOffset = FS_BLOCK_SIZE % offset;
//        int entryBlockNum = get_blk(my_inode, startBlkIndex, 0);
//        printf("block to read: %d\n", entryBlockNum);
//        if (disk->ops->read(disk, entryBlockNum, 1, myPtr) < 0) {
//            exit(2);
//        }
//        if(i==0){
//            offset = offset % FS_BLOCK_SIZE;            // rounding up offset
//            // copies bytes from myPtr to buf, excludes no. of bytes in offset
//            bytesToCopy = FS_BLOCK_SIZE-offset;
//            memcpy(buf, myPtr + offset, bytesToCopy);
//            //adjustedOffset = 0;
//        }
//        else if(i==numBlocksToRead-1){
//            bytesToCopy = len - numBytes;
//            memcpy(buf, myPtr, bytesToCopy);
//        }
//        else{
//            bytesToCopy = FS_BLOCK_SIZE;
//            memcpy(buf, myPtr, bytesToCopy);
//        }
//        numBytes = numBytes + bytesToCopy;
//        buf =  buf + bytesToCopy;
//        startBlkIndex++;
//    }
//    free(myPtr);
//    return numBytes;
//}

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
	// get inode number & find instantiate inode
	int inum = translate(path);
	// if path not valid, return error
	if(inum == -ENOENT){
		return -ENOENT;
	}
	// get inode
	struct fs_inode* my_inode = inodes + inum;
	int my_inode_size = my_inode->size;
	if(offset > my_inode_size){
		return -EINVAL;
	}

	// find the first byte to begin reading
	int firstByteToRead = offset;
	// find the last byte to be read in file
	int lastByteToRead = firstByteToRead + len;
	// find starting block to read - gets the nth block in the file
	float startBlkIndex = (floor(firstByteToRead / FS_BLOCK_SIZE));
	// find last block to read
	float lastBlkIndex = (floor(lastByteToRead / FS_BLOCK_SIZE));

    // find total no. of blocks we will read
    int numBlocksToWrite = (int)(lastBlkIndex - startBlkIndex + 1);

    //void* myPtr = malloc(FS_BLOCK_SIZE);
    int bytesToWrite = 0;
    int length = len;

    for (int i = 0; i < numBlocksToWrite; i++) {
    	void* myPtr = malloc(FS_BLOCK_SIZE);
    	//off_t adjustedOffset = FS_BLOCK_SIZE % offset;
		int entryBlockNum = get_blk(my_inode, startBlkIndex, 1);
		if (entryBlockNum == 0) {
			free(myPtr);
			return len - length;
		}

		if (disk->ops->read(disk, entryBlockNum, 1, myPtr) < 0) {
			exit(2);
		}

		if (i == 0) {
			bytesToWrite = FS_BLOCK_SIZE - offset % FS_BLOCK_SIZE;
			if (length < bytesToWrite) {
				bytesToWrite = length;
				length = 0;
			} else {
				length -= bytesToWrite;
			}
			char *start = myPtr + offset % FS_BLOCK_SIZE;
			memcpy(start, buf, bytesToWrite);
		} else if (i == numBlocksToWrite-1) {
            bytesToWrite = len - length;
            length -= bytesToWrite;
            char *start = myPtr;
            memcpy(start, buf, bytesToWrite);
        } else {
        	char *start = myPtr;
        	bytesToWrite = FS_BLOCK_SIZE;
        	length -= bytesToWrite;
        	memcpy(start, buf, bytesToWrite);
        }
		buf += bytesToWrite;
		my_inode->size += bytesToWrite;
		write_all_inodes();
		disk->ops->write(disk, entryBlockNum, 1, myPtr);
		free(myPtr);
		if (length == 0) break;
		startBlkIndex++;
    }

    struct utimbuf ut;
    ut.modtime = time(NULL);
    fs_utime(path, &ut);
    return len - length;
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

