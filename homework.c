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

#if 1

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
    return -EOPNOTSUPP;
}

static void* fs_init(struct fuse_conn_info *conn);

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
    for (int i = n_meta; i < n_blocks; i++) {
        if (!FD_ISSET(i, block_map)) {
            FD_SET(i, block_map);   // find free block and set it
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
    for(int entryNum=0; entryNum<32; entryNum++){
        if(strcmp(de->name, name)==0){
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
    for(int i=0; i<32; i++){
        if(strcmp(de->name, name)==0){
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
    for(int entryNum=0; entryNum<32; entryNum++){
        if((de->valid)==0){
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
    for(int i=0; i<32; i++){
        if(de->valid==1){
            return 0;
        }
        de++;
    }
    return 1;
}


/** Helper function for getBlk which allocates the nth block if it does not exist */

static void allocate_nth_block(struct fs_inode *in, int n){
    
    // if n < 6, allocate a free block at nth index
    if(n < N_DIRECT){
        int freeBlk = get_free_blk();
        in->direct[n] = freeBlk;
        FD_SET(freeBlk, block_map);
    }
    
    // n is > 6 or < 256, allocate a block in the indir1 blocks
    else if(n >= N_DIRECT || n < N_DIRECT + PTRS_PER_BLK){
        
        uint32_t indir_block_1 = in->indir_1;
        // if this is the very first block in the indir1 blocks, first allocate block for indir1. Then allocate data block inside indir1
        if(n== N_DIRECT){
            int freeBlkForIndir1 = get_free_blk();
            indir_block_1 = freeBlkForIndir1;
        }
        
        printf("Ptrs per blk: %d, indirBlk: %d\n", PTRS_PER_BLK, indir_block_1);
        int* ptr_to_indir1 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_1, 1, ptr_to_indir1) < 0) {
            exit(2);
        }
        
        // find free block from block map
        int freeBlkNum = get_free_blk();
        // add free block to the nth block position
        ptr_to_indir1[n-N_DIRECT] = freeBlkNum;
        // set block num to allocated in blockmap
        FD_SET(freeBlkNum, block_map); // returns nth block from indirect blocks
        
    }
    // if n >= 262, allocate a free block to the indir2 blocks
    else if(n >= (N_DIRECT + PTRS_PER_BLK)){
        uint32_t indir_block_2 = in->indir_1;
        // if nth block is the very first block in the indir blocks, first allocate a block for indir2
        if(n==N_DIRECT + PTRS_PER_BLK){
            int freeBlkForIndir2 = get_free_blk();                      // free block for in->indir2
            indir_block_2 = freeBlkForIndir2;
        }
        // if this is not the very first indir2 block, perform computation to find correct location to add free block
        int adjusted_n = n - (N_DIRECT + PTRS_PER_BLK);
        
        int* ptr_to_indir2 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_2, 1, ptr_to_indir2) < 0) {
            exit(2);
        }
        // find the block index in indirect2 block that we will read, get ceil
        // ceil(738 / 256) = 3.0
        double index_from_indir2 = (ceil)(adjusted_n / PTRS_PER_BLK);
        // convert to int = 3 for dereferencing
        int index_from_indir2_int = (int)(index_from_indir2);
        
        // get free block to allocate inside indir2
        int freeBlkInsideIndir2 = get_free_blk();                       // first free block
        
        // set second level block in indir2 inside which we will allocate our data block
        ptr_to_indir2[index_from_indir2_int] = freeBlkInsideIndir2;
        FD_SET(freeBlkInsideIndir2, block_map);
        
        int* myptr = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, freeBlkInsideIndir2 , 1, myptr) < 0) {
            exit(2);
        }
        // find the final target location where our free data block will be allocated
        int target_idx = adjusted_n % PTRS_PER_BLK;
        
        int finalindir2FreeBlk = get_free_blk();                        // second free block
        myptr[target_idx] = finalindir2FreeBlk;
        FD_SET(finalindir2FreeBlk, block_map);
        
    }
    
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
    float file_size_blocks = (ceil)(file_size_bytes / FS_BLOCK_SIZE);
    
    /** adding code to check if nth block exists */
    // if nth block does not exist & alloc = 1, then allocate a free block to nth index
    if(file_size_blocks < n-1 && alloc==1){
        allocate_nth_block(in, n);
        return 0;
    }
    // if nth block does not exist & alloc = 0, return error
    else{
        return ENOMEM;
    }
    
    int retVal = -1;    // initialize retVal to -1 which is invalid block no
    // if n is between 0 to 5, access the direct blocks = 6 blocks
    if(n < N_DIRECT){
        retVal = in->direct[n];
    }
    // if n is between 6 to 255: access indirect1 block
    else if(n >= N_DIRECT || n < N_DIRECT + PTRS_PER_BLK){
        int indir_block_1 = in->indir_1;
        printf("Ptrs per blk: %d, indirBlk: %d\n", PTRS_PER_BLK, indir_block_1);
        int* ptr_to_indir1 = malloc(FS_BLOCK_SIZE);
        if (disk->ops->read(disk, indir_block_1, 1, ptr_to_indir1) < 0) {
            exit(2);
        }
        retVal = ptr_to_indir1[n-N_DIRECT]; // returns nth block from indirect blocks
    }
    // else if n = 256 or greater : access the indir2 block
    else if(n >= (N_DIRECT + PTRS_PER_BLK)){
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
        double index_from_indir2 = (ceil)(adjusted_n / PTRS_PER_BLK);
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
    // Testing lookup - delete later
    int a = lookup(3, "file.0");
    printf("Retval for file inode: %d\n", a);
    
    // testing get free block method - delete later
    int d = get_free_blk();
    printf("Free block: %d\n", d);

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
static int fs_getattr(const char *path, struct stat *sb)
{
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
        return ENOENT;
    }
    // get inode
    struct fs_inode* my_inode = inodes + inum;
    int my_inode_size = my_inode->size;
    printf("File size: %d, offset: %d\n", my_inode_size, offset);
    if(offset + len > my_inode_size){
        return EIO;
    }
    
    // find the first byte to begin reading
    int firstByteToRead = offset;
    // find the last byte to be read in file
    int lastByteToRead = firstByteToRead + len;
    printf("First byte: %d, last byte: %d\n", firstByteToRead, lastByteToRead);
    // find starting block to read - gets the nth block in the file
    float startBlkIndex = (floor(firstByteToRead / FS_BLOCK_SIZE));
    // find last block to read
    float lastBlkIndex = (floor(lastByteToRead / FS_BLOCK_SIZE));
    printf("First block to read: %d, Last block: %d\n", (int)startBlkIndex, (int)lastBlkIndex);
    
    
    // find total no. of blocks we will read
    int numBlocksToRead = (int)(lastBlkIndex - startBlkIndex + 1);
    printf("Total blocks to read: %d\n", numBlocksToRead);
    
    
    void* myPtr = malloc(FS_BLOCK_SIZE);
    int numBytes = 0;
    int bytesToCopy = 0;
    
    for(int i=0; i<numBlocksToRead; i++){
        
        //off_t adjustedOffset = FS_BLOCK_SIZE % offset;
        int entryBlockNum = get_blk(my_inode, startBlkIndex, 1);
        printf("block to read: %d\n", entryBlockNum);
        if (disk->ops->read(disk, entryBlockNum, 1, myPtr) < 0) {
            exit(2);
        }
        if(i==0){
            // copies bytes from myPtr to buf, excludes no. of bytes in offset
            bytesToCopy = FS_BLOCK_SIZE-offset;
            memcpy(buf, myPtr + offset, bytesToCopy);
            //adjustedOffset = 0;
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

#endif
