/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.

	gcc -Wall `pkg-config fuse --cflags --libs` cs1550.c -o cs1550
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(size_t) - sizeof(long))

struct cs1550_directory_entry
{
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	int nFiles;			//How many files are in this directory. 
					//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} files[MAX_FILES_IN_DIR];		//There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
	//Two choices for interpreting size: 
	//	1) how many bytes are being used in this block
	//	2) how many bytes are left in the file
	//Either way, size tells us if we need to chase the pointer to the next
	//disk block. Use it however you want.
	size_t size;	

	//The next disk block, if needed. This is the next pointer in the linked 
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 Update the directory in the .directories file after adding a file
*/
static int updateDirectory(cs1550_directory_entry *directory, int index) 
{
	if (directory == NULL) 
		return -1;
	else 
	{
		FILE *f = fopen(".directories", "rb+");
		fseek(f, sizeof(cs1550_directory_entry) * index, SEEK_SET);
		fwrite(directory, sizeof(cs1550_directory_entry),1,f);
		fclose(f);
		return 0;
	}
}

//fills the given directory struct or returns 0 if there is a problem
static int getDirectory(cs1550_directory_entry *found_dir, int index)
{
	int retValue = 0;
	int seek, read;
	FILE *f = fopen(".directories", "rb");
	if(f == NULL) 
		return retValue;
	
	seek = fseek(f, sizeof(cs1550_directory_entry) * index, SEEK_SET);
	if(seek == -1) 
		return retValue;
		
	read = fread(found_dir, sizeof(cs1550_directory_entry), 1, f);
	if(read == 1)
		retValue = 1;
		
	fclose(f);
	return retValue;
}

// Searches through the .directories file and returns the index of that directory entry
static int findDirectory(char *dirName) 
{
	int retValue = -1; //in case we don't find it
	cs1550_directory_entry temp;
	int index = 0;
	while ((getDirectory(&temp, index) != 0) && (retValue == -1)) 
	{
		if (strcmp(dirName, temp.dname) == 0) 
			retValue = index;
			
		index++;
	}
	
	return retValue;
}

/*
Returns the index of the file in the .disk with "filename"
or will return -1 if it does not exist.
*/
static int findFile(cs1550_directory_entry *directory, char * filename, char *extension)
{
	int i;
	for (i = 0; i < directory->nFiles; i++) 
	{
		if ((strcmp(filename, directory->files[i].fname) == 0) && (strlen(filename) == strlen(directory->files[i].fname))) // If the filename is the same
		{
			if ((extension[0] == '\0') && (directory->files[i].fext[0] == '\0')) // If the extensions are NULL
				return i;
			else if ((extension != NULL) && (strcmp(extension, directory->files[i].fext) == 0)) // If the extension isn't NULL and it matches the one in the file
				return i;
		}
	}
	
	return -1;
}

/*
 * Allocates a new block for creating a file
 * returning it's position in .disk on success
 */
static int allocateBlock() 
{
	int blockIndex = -1;
	int newBlockIndex;
	FILE *f = fopen(".disk", "rb+");
	if(f == NULL) // If .disk does not exist
		return -1;
	
	int seekReturn = fseek(f, -sizeof(cs1550_disk_block), SEEK_END);
	if(seekReturn == -1) 
		return -1;
	
	int readReturn = fread(&blockIndex, sizeof(int), 1, f);
	if(readReturn == -1) 
		return -1;
	
	seekReturn = fseek(f, -sizeof(cs1550_disk_block), SEEK_END);
	if(seekReturn == -1) 
		return -1;
		
	newBlockIndex = blockIndex + 1;
	int writeReturn = fwrite(&newBlockIndex, 1, sizeof(int), f);
	if(writeReturn == -1) 
		return -1;
	
	fclose(f);
	return blockIndex;
}

/*
 *read a block from disk and load it to memory
 */
static int readBlock(cs1550_disk_block *returnBlock, int position) 
{
	int returnValue = -1;
	FILE *f = fopen(".disk", "rb");
	if(f == NULL)
		return returnValue;
	
	int seekReturn = fseek(f, sizeof(cs1550_disk_block) * position, SEEK_SET);
	if(seekReturn == -1)
		return returnValue;
	
	int readReturn = fread(returnBlock, sizeof(cs1550_disk_block), 1, f);
	if(readReturn == 1)
		returnValue = 0;
	
	fclose(f);
	return returnValue;
}

/*
 *write a block to disk from memory
 */
static int writeBlock(cs1550_disk_block *returnBlock, int position) 
{
	int returnValue = -1;
	FILE *f = fopen(".disk", "rb+");
	if(f == NULL)
		return returnValue;
	
	int seekReturn = fseek(f, sizeof(cs1550_disk_block) * position, SEEK_SET);
	if(seekReturn == -1)
		return returnValue;
	
	int writeReturn = fwrite(returnBlock, sizeof(cs1550_disk_block), 1, f);
	if(writeReturn == 1)
		returnValue = 0;
	
	fclose(f);
	return returnValue;
}


/*
 * Transfers data from a buffer to the data section of a block
 * 
 * Returns:
 * int representing the amount of data left to transfer
 */
static int buffer_to_block(cs1550_disk_block *block, const char *buffer, int position, int dataLeft) 
{
	while(dataLeft > 0) 
	{
		if(position > MAX_DATA_IN_BLOCK)//this block is not enough
			return dataLeft; // Return the amount of data left to write
		else 
		{
			block->data[position] = *buffer;
			buffer++; //increment buffer pointer
			dataLeft--;
			if(block->size <= position) 
				block->size += 1;
			
			position++;
		}
	}
	return dataLeft;
}


/* 
 * Transfers data from the data section of a block to a buffer
 * 
 * Returns:
 * integer representing the amount of data left to transfer
 * -1 if too much data was asked for
 */
static int block_to_buffer(cs1550_disk_block *block, char *buffer, int position, int dataLeft) 
{
	while(dataLeft > 0) 
	{
		if(position > MAX_DATA_IN_BLOCK)// this block is below the position for reading
			return dataLeft;
		else 
		{
			*buffer = block->data[position];
			buffer++; // increment the buffer pointer
			dataLeft--;
			position++;
		}
	}
	return dataLeft;
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	cs1550_directory_entry currentDirectory;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int retValue = -ENOENT;
	int directoryIndex, i;
	
	memset(stbuf, 0, sizeof(struct stat));
	
	if (strcmp(path, "/") == 0) //attributes of root directory
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		retValue = 0; // no error
	}
	else
	{ 
		memset(directory, 0, MAX_FILENAME + 1);
		memset(filename, 0, MAX_FILENAME + 1);
		memset(extension, 0, MAX_EXTENSION + 1);
		
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		directoryIndex = findDirectory(directory);
		
		if(directoryIndex != -1) //if it is a valid directory
		{
			getDirectory(&currentDirectory,directoryIndex);
			
			if(directory != NULL && (filename[0] == '\0')) //not a file
			{
					stbuf->st_mode = S_IFDIR | 0755;
					stbuf->st_nlink = 2;
					retValue = 0; //no error
			}
			else
			{
				for (i = 0; i < currentDirectory.nFiles; i++) 
				{
					if(strcmp(currentDirectory.files[i].fname,filename) == 0) //filename matches
					{
						if (strcmp(currentDirectory.files[i].fext,extension) == 0) //extension matches
						{
							stbuf->st_mode = S_IFREG | 0666; 
							stbuf->st_nlink = 1;
							stbuf->st_size = currentDirectory.files[i].fsize;
							retValue = 0; //no error
							break;
						}
					}
				}
			}
		}
		else
			retValue = -ENOENT; // directory does not exist
	}
	return retValue;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
	int retValue = 0;
	int directoryIndex;
	int i = 0;
	char buffer[50];
	cs1550_directory_entry currentDirectory;
	
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	
	if(strcmp(path, "/") != 0) //not the root
	{
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		directoryIndex = findDirectory(directory);
		if((directory != NULL) && (directoryIndex != -1)) 
		{
			getDirectory(&currentDirectory, directoryIndex);
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			
			for(i=0; i<currentDirectory.nFiles; i++) 
			{
				if(strlen(currentDirectory.files[i].fext) > 0) 
					sprintf(buffer, "%s.%s", currentDirectory.files[i].fname, currentDirectory.files[i].fext);
				else 
					sprintf(buffer, "%s", currentDirectory.files[i].fname);
				
				filler(buf, buffer, NULL, 0);
			}
			
			retValue = 0;
		}
		else 
			retValue = -ENOENT;
	}
	else
	{
		i = 0;
		filler(buf, ".", NULL,0);
		filler(buf, "..", NULL, 0);
		while(getDirectory(&currentDirectory, i) != 0) 
		{
			filler(buf, currentDirectory.dname, NULL, 0);
			i++;
		}
		retValue = 0;
	}
	
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) mode;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int retValue = 0;
	cs1550_directory_entry tempDirectory;
	
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if(directory == NULL || directory[0] == '\0') // If no directory was specified
		retValue = -EPERM;
	else 
	{
		if(findDirectory(directory) == -1) // If the directory does not already exist
		{
			if(strlen(directory) <= MAX_FILENAME) // If the filename is not too long
			{
				memset(&tempDirectory, 0, sizeof(struct cs1550_directory_entry));
				strcpy(tempDirectory.dname, directory);
				tempDirectory.nFiles = 0;
				
				FILE *f = fopen(".directories", "ab");
				fwrite(&tempDirectory, sizeof(cs1550_directory_entry),1,f);
				fclose(f);
			}
			else
				retValue = -ENAMETOOLONG;   // The filename is too long
		}
		else 
			retValue = -EEXIST; // directory already exists
	}
	
	return retValue;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int ret = 0;
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if(directory != NULL) //cannot create files in the root
	{
		int dirIndex = findDirectory(directory);

		if(strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION)
			ret = -ENAMETOOLONG;
		else
		{
			cs1550_directory_entry dirEntry;
			getDirectory(&dirEntry, dirIndex);
			if(findFile(&dirEntry, filename, extension) == -1) //if the file doesn't exist
			{
				strcpy(dirEntry.files[dirEntry.nFiles].fname ,filename);
				if(strlen(extension) > 0)
					strcpy(dirEntry.files[dirEntry.nFiles].fext ,extension);
				else
					strcpy(dirEntry.files[dirEntry.nFiles].fext, "\0");
				
				dirEntry.files[dirEntry.nFiles].nStartBlock = allocateBlock();
				dirEntry.files[dirEntry.nFiles].fsize = 0;
				dirEntry.nFiles = dirEntry.nFiles+1;
				updateDirectory(&dirEntry, dirIndex);
				ret = 0;

			}
			else
				ret = -EEXIST;
		}
	}
	else
		ret = -EPERM;
        
    return ret;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi)
{
	(void) fi;
	int returnValue = 0;
	int tempOffset = offset; // Make a copy
	cs1550_directory_entry tempDir;
	cs1550_disk_block tempBlock;
	
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	if(offset > size || size <= 0) 
		return -1;
		
	if(directory != NULL) 
	{
		if(filename == NULL)
			return -EISDIR;
			
		if(strlen(filename) < MAX_FILENAME) 
		{
			if((extension == NULL) || (extension[0] == '\0') || ((extension != NULL && extension[0] != '\0') && (strlen(extension) <= MAX_EXTENSION))) 
			{ 
				// If the extension is either NULL, or if it is not NULL but less than the max length
				int dirIndex = findDirectory(directory);
				if(dirIndex == -1)
					returnValue = -1;
				
				int result = getDirectory(&tempDir, dirIndex);
				if(result == 0) 
					returnValue = -1; // Problem getting the current directory
				
				int fileIndex = findFile(&tempDir, filename, extension);
				if(fileIndex != -1) // If the file exists
				{		
					if(tempDir.files[fileIndex].fsize == 0) //empty file - just return
						return 0;
					
					int blockNum = tempDir.files[fileIndex].nStartBlock;
					
					while(offset < size)
					{	
						readBlock(&tempBlock, blockNum);
						if (tempOffset > MAX_DATA_IN_BLOCK) 
						{
							blockNum = tempBlock.nNextBlock;
							tempOffset -= MAX_DATA_IN_BLOCK;
							continue;
						}
						else 
						{
							int bufferReturn = block_to_buffer(&tempBlock, buf, tempOffset, size - offset);
							tempOffset = 0;
							if (bufferReturn == 0) 
								break;
							else 
							{
								blockNum = tempBlock.nNextBlock;
								offset += MAX_DATA_IN_BLOCK;
								buf += MAX_DATA_IN_BLOCK;
							}
						}
					}
					
					returnValue = size;                            
				}
				else 
					returnValue = -1; // File already exists
			}
			else 
				returnValue = -1; // The extension is too long
		}
		else 
			returnValue = -1; // The filename is too long
	}
	else 
		returnValue = -1;

	return returnValue;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{		
	(void) fi;
	int tempOffset = offset; // Make a copy
	int returnValue = 0;
	
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	cs1550_directory_entry tempDir;
	cs1550_disk_block tempBlock;
	memset(&tempBlock, 0, sizeof(cs1550_disk_block)); // Clear the temporary block
	
	if((offset > size) || (size <= 0)) // If trying to write beyond the end of the file or write nothing
		return -1;
		
	if(directory != NULL) 
	{
		if((filename != NULL) && (filename[0] != '\0') && (strlen(filename) < MAX_FILENAME)) 
		{
			if((extension == NULL) || (extension[0] == '\0') || ((extension != NULL && extension[0] != '\0') && (strlen(extension) <= MAX_EXTENSION))) 
			{
				// If the extension is either NULL, or if it is not NULL but less than the max length
				int dirIndex = findDirectory(directory);
				if(dirIndex == -1)
					returnValue = -1; // Problem finding the current directory
				
				if(getDirectory(&tempDir, dirIndex) == 0)
					returnValue = -1; // Problem getting the current directory
				
				int fileIndex = findFile(&tempDir, filename, extension);
				if(fileIndex != -1) // If the file exists
				{
					int blockNum = tempDir.files[fileIndex].nStartBlock;
					tempDir.files[fileIndex].fsize = size;
					updateDirectory(&tempDir, dirIndex); // Write the updated directory to the .directories file
					
					while( tempOffset >= MAX_DATA_IN_BLOCK)
					{
						blockNum = tempBlock.nNextBlock;
						tempOffset -= MAX_DATA_IN_BLOCK;
						readBlock(&tempBlock, blockNum);
					}
					
					while( offset < size)
					{
						if (tempOffset > MAX_DATA_IN_BLOCK) 
						{
							blockNum = tempBlock.nNextBlock;
							tempOffset -= MAX_DATA_IN_BLOCK;
							continue;
						}
						else 
						{
							int bufferReturn = buffer_to_block(&tempBlock, buf, tempOffset, size - offset);
							if (bufferReturn != 0 && (tempBlock.nNextBlock <= 0)) //another block is needed
								tempBlock.nNextBlock = allocateBlock();
																
							writeBlock(&tempBlock, blockNum);
							tempOffset = 0;
							
							if (bufferReturn == 0)
								break;
							else 
							{
								blockNum = tempBlock.nNextBlock;
								offset += MAX_DATA_IN_BLOCK;
								readBlock(&tempBlock, blockNum);
								buf += MAX_DATA_IN_BLOCK;
							}
						}
					}
					
					returnValue = size;                            
				}
				else 
					returnValue = -1; //file exists
			}
			else 
				returnValue = -1; //extension is too long
		}
		else
			returnValue = -EISDIR; //not a file
	}
	else 
		returnValue = -1;
	
	return returnValue;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
