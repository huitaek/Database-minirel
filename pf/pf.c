#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "minirel.h"
#include "bf.h"
#include "pf.h"

typedef struct PFhdr_str {
	int 	numpages;	/* number of pages in the file */
} PFhdr_str;

typedef struct PFftab_ele {
	bool_t		valid;		/* set to TRUE when a file is open 	*/
	ino_t		inode;		/* inode number of the file 		*/
	char		*fname;		/* file name				*/
	int 		unixfd;		/* Unix file descriptor			*/
	PFhdr_str	hdr;		/* file header				*/
	short		hdrchanged;	/* TRUE if file header has changed 	*/
} PFftab_ele;

PFftab_ele *PFftable; 
size_t PFftab_length;

void PF_Init(void)
{
    int i;
	/* Invoke the BF_Init() */
	BF_Init();

	/* Create the file table */
	PFftable = malloc(sizeof(PFftab_ele)*PF_FTAB_SIZE);

	/*Initialize the file table */
	for(i = 0; i < PF_FTAB_SIZE; i++){
		PFftable[i].valid = FALSE;
		PFftable[i].inode = 0;
		PFftable[i].fname = NULL;
		PFftable[i].unixfd = -1;
		PFftable[i].hdr.numpages = 0;
		PFftable[i].hdrchanged = FALSE;
	}
}

int PF_CreateFile(const char *filename)
{
	int unixfd;
	int inode;
	struct stat fileStat;
	BFreq bq;
	PFhdr_str header;

	/* File exist check */
	if(access(filename, F_OK) != -1){
		return PFE_FD; /* Already exists */
	}

	/* Create the file */
	if((unixfd = open(filename, O_RDWR|O_CREATE, FILE_CREATE_MASK)) < 0){
		return PFE_FILEOPEN;
	}

	/* Fine the inode of the file */
	if((inode = fstat(unixfd, &fileStat)) < 0){
		return PFE_UNIX;
	}

	/* Initialize the header and write to the file */
	header.numpages = 1;
	if(write(unixfd, header, sizeof(PFhdr_str)) != sizeof(PFhdr_str)){
		return PFE_UNIX;
	}

	/* Close the file */
	if(close(unixfd) < 0){
		return PFE_UNIX;
	}

	return PFE_OK;
}

int PF_DestroyFile(const char *filename)
{
    int i;
	/* File exist check */
	if(access(filename, F_OK) == -1){
		return PFE_UNIX;
	}

	/* Find the file by filename */
	for(i = 0; i < PF_FTAB_SIZE; i++){
		if(strcmp(PFftable[i].fname, filename)){ /* Found the file */
			/* File open check */
			if(PFftable[i].valid)
				return PFE_FILEOPEN;

			/* Unlink the file */
			unlink(filename);
			break;
		}
	}

	return PFE_OK;
}

int PF_OpenFile(const char *filename)
{
	int unixfd;
	int PFfdsc;
	int error;
	PFhdr_str header;
	struct stat fileStat;
    int i;

	/* Open the file */
	if((unixfd = open(filename, O_RDONLY)) < 0){
		return PFE_FILEOPEN;
	}

	PFfdsc = -1;

	/* Iterate the table and find the empty entry */
	for(i = 0; i < PF_FTAB_SIZE; i++){
		if(!PFftable[i].valid){ /* Found the target entry */
			PFfdsc = i; /* Index of the file table entry */
			if(read(unixfd, &header, sizeof(PFhdr_str)) >= 0) /* Read the header */
				break;
			return PFE_UNIX;
		}
	}

	/* Check whether the table is full */
	if(PFfdsc == -1)
		return PFE_FTABFULL;

	/* Find the inode of the file */
	if((error = fstat(unixfd, &fileStat)) < 0){
		return PFE_UNIX;
	}

	/* Fill the file table entries accordingly */
	PFftable[PFfdsc].valid = TRUE;
	PFftable[PFfdsc].inode = fileStat.st_ino;
	PFftable[PFfdsc].fname = filename;
	PFftable[PFfdsc].unixfd = unixfd;
	PFftable[PFfdsc].hdr.numpages = header.numpages;
	PFftable[PFfdsc].hdrchanged = FALSE;

	/* Return the PF file descriptor */
	return PFfdsc;
}

int PF_CloseFile(int fd)
{
	int unixfd;
	int error;
	PFhdr_str header;
	char **pagebuf;

	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
		return PFE_FD;

	/* Check whether the file is open */
	if(!PFftable[fd].valid)
		return PFE_FILENOTOPEN;

	/* Find the unix file descriptor in the PF file table */
	unixfd = PFftable[fd].unixfd;

	/* 
	 * Release all the buffer pages belonging to the file to the FREE list 
	 * Dirty pages will be written to the disk
	 */
	if((error = BF_FlushBuf(fd)) != BFE_OK)
		return error;

	/* Check whether the header is changed */
	if(PFftable[fd].hdrchanged){
		/* Re-write the header in the file */
		header = PFftable[fd].hdr;
		if((error = pwrite(unixfd, header, sizeof(PFhdr_str), 0)) < 0)
			return PFE_UNIX;
	}

	/* Close the file */
	if((error = close(unixfd)) < 0){
		return PFE_UNIX;
	}

	/* Invalid the corresponding entry */
	PFftable[fd].valid = FALSE;

	return PFE_OK;
}

int PF_AllocPage (int fd, int *pagenum, char **pagebuf)
{
    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err; 

    /* Invalid PF file descriptor */
	if (fd < 0 || fd > PF_FTAB_SIZE-1) 
        return PFE_FD;
	
	/* Invaild page number or page buffer */
	if (pagenum == NULL || pagebuf == NULL) 
        return PFE_INVALIDPAGE;

	/* Current PF file descriptor */
	current_pfftab = PFftable + fd;

	/* Wrap the BF layer request */
    bq.fd = fd;
	bq.pagenum = current_pfftab->hdr.numpages;
	bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    
	/* Check whether the file is not open */
	if (current_pfftab->valid == FALSE)
			return PFE_FILENOTOPEN;
    
	/* Append the page to the file */
	err = BF_AllocBuf(bq, &pfpage);
	if (err != BFE_OK)
			return err;

	/* Set the dirty page */
    err = BF_TouchBuf(bq);
    if (err != BFE_OK)
			return err;

	/* Set the return argument */
	*pagenum = bq.pagenum;
	*(pagebuf) = pfpage->pagebuf;
	current_pfftab->hdrchanged = TRUE;
	current_pfftab->hdr.numpages++;

	return PFE_OK;
}

int  PF_GetFirstPage (int fd, int *pagenum, char **pagebuf)
{    
	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
		return PFE_FD;
		
	*pagenum = -1;

	return PF_GetNextPage(fd, pagenum, pagebuf);
}
int  PF_GetNextPage	(int fd, int *pagenum, char **pagebuf)
{
    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
			return PFE_FD;

    current_pfftab = PFftable + fd;

	/* Wrap the BF layer request */
    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = FALSE;
    bq.pagenum = (*pagenum) + 1;

	/* Check whether the file is not open */
    if (current_pfftab->valid == FALSE)
        return PFE_FILENOTOPEN;

	/* Invalid page number */
    if (current_pfftab->hdr.numpages <= bq.pagenum || *pagenum < -1)
        return PFE_EOF;

	/* Get the page buffer with the page number */
    err = BF_GetBuf(bq, &pfpage);
    if (err != BFE_OK)
        return err;

	/* Set the return argument */
    *pagebuf = pfpage->pagebuf;
    *pagenum++;

    return PFE_OK;
}
int  PF_GetThisPage	(int fd, int pagenum, char **pagebuf)
{    
    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
			return PFE_FD;

    current_pfftab = PFftable + fd;

	/* Wrap the BF layer request */
    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = FALSE;
    bq.pagenum = pagenum;

	/* Check whether the file is open */
    if (current_pfftab->valid == FALSE)
        return PFE_FILENOTOPEN;

	/* Invalid page number */
    if (current_pfftab->hdr.numpages <= bq.pagenum || pagenum < 0)
        return PFE_INVALIDPAGE;

	/* Get the page buffer with the page number */
    err = BF_GetBuf(bq, &pfpage);
    if (err != BFE_OK)
        return err;

	/* Set the return argument */
    *pagebuf = pfpage->pagebuf;

    return PFE_OK;
}
int  PF_DirtyPage	(int fd, int pagenum)
{    
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
		return PFE_FD;

    current_pfftab = PFftable + fd;

	/* Check whether the file is open */
    if (current_pfftab->valid == FALSE)
        return PFE_FILENOTOPEN;
	
	/* Invalid page number */
    if (pagenum >= current_pfftab->hdr.numpages || pagenum < 0)
        return PFE_INVALIDPAGE;

    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    bq.pagenum = pagenum;

	/* Set the page as dirty */
    err = BF_TouchBuf(bq);
    if (err != BFE_OK)
        return err;

    return PFE_OK;
}
int  PF_UnpinPage	(int fd, int pagenum, int dirty)
{
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

	/* Invalid PF file descriptor */
	if(fd < 0 || fd > PF_FTAB_SIZE-1)
			return PFE_FD;

    current_pfftab = PFftable + fd;
 
	/* Check whether the file is open */
 	if (current_pfftab->valid == FALSE)
        return PFE_FILENOTOPEN;

	/* Invalid page number */
    if (pagenum >= current_pfftab->hdr.numpages || pagenum < 0)
        return PFE_INVALIDPAGE;

    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    bq.pagenum = pagenum;
    
    if (dirty){
        err = BF_TouchBuf(bq);
        if (err != BFE_OK)
            return err;
    }
    
    err = BF_UnpinBuf(bq);
    if (err != BFE_OK)
        return err;

    return PFE_OK;
}

void PF_PrintError(const char *errString)
{
	printf("PF Error : %s", errString);
	exit(1);
}
