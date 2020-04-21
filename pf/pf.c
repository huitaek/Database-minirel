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

PFftab_ele *PFftable; 
size_t PFftab_length;

void PF_Init(void)
{
	/* Invoke the BF_Init() */
	BF_Init();

	/* Create the file table */
	PFftable = malloc(sizeof(PFftab_ele)*PF_FTAB_SIZE);

	/*Initialize the file table */
	for(int i = 0; i < PF_FTAB_SIZE; i++){
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

	/* Close the file */
	if(close(unixfd) < 0){
		return PFE_UNIX;
	}

	return PFE_OK;
}

int PF_DestroyFile(const char *filename)
{
	/* File exist check */
	if(access(filename, F_OK) == -1){
		return PFE_UNIX;
	}

	/* Find the file by filename */
	for(int i = 0; i < PF_FTAB_SIZE; i++){
		if(strcmp(PFftable[i].fname, filename)){ /* Found the file */
			/* File open check */
			if(PFftable[i].valid)
				return PFE_FILEOPEN;

			/* Unlink the file */
			unlink(filename);
			break;
		}
	}
}

int PF_OpenFile(const char *filename)
{
	int unixfd;
	int header;
	int PFfdsc;
	int inode;
	struct stat fileStat;

	/* Open the file */
	if((unixfd = open(filename, O_RDONLY)) < 0){
		return PFE_FILEOPEN;
	}

	/* Read the header in the file */
	for(int i = 0; i < PF_FTAB_SIZE; i++){
		if(strcmp(PFftable[i].fname, filename)){ /* Found the file */
			header = PFftable[i].hdr.numpages; /* File header */
			PFfdsc = i; /* PF file descriptor */
			break;
		}
	}

	/* Find the inode of the file */
	if((inode = fstat(unixfd, &fileStat)) < 0){
		return PFE_UNIX;
	}

	/* Fill the file table entries accordingly */
	PFftable[PFfdsc].valid = TRUE;
	PFftable[PFfdsc].inode = fileStat.st_ino;
	PFftable[PFfdsc].fname = filename;
	PFftable[PFfdsc].unixfd = unixfd;
	PFftable[PFfdsc].hdr = header;
	PFftable[PFfdsc].hdrchanged = FALSE;
}

int PF_CloseFile(int fd)
{
	int unixfd;
	int CurrNum, NextNum;
	int error;
	BFreq bq;
	char **pagebuf;

	/* Find the unix file descriptor in the PF file table */
	unixfd = PFftable[fd].unixfd;

	/* Release all the buffer pages belonging to the file to the FREE list */
	if(BF_FlushBuf(fd) != BFE_OK){
		/* Unpin all the buffer pages */
		if(PF_GetFirstPage(fd, CurrNum, pagebuf) != PFE_OK){
			return PFE_INVALIDPAGE;
		}
		/* Create the buffer request */
		bq.fd = fd;
		bq.unixfd = unixfd;
		bq.pagenum = CurrNum;
		bq.dirty = FALSE;
		BF_UnpinBuf(bq); /* Unpin the buffer page */
	}

	/* Unpin all the buffer pages */
	/* FIXME */

	/* Close the file */
	if((error = close(unixfd)) < 0){
		return PFE_UNIX;
	}

	/* Invalid the corresponding entry */
	PFftable[fd].valid = FALSE;

	return PFE_OK;
}

int PF_AllocPage (int fd, int *pagenum, char **pagebuf){

    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err; 

    if (fd < 0 || fd >= PFtab_length) 
        return PFE_FD;
    if (pagenum == NULL || pagebuf == NULL)
        return PFE_INVALIDPAGE;

    bq.fd = fd;
    current_pfftab = PFftab + fd;
    bq.pagenum = current_pfftab->hdr.numpages;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    
    err = BF_AllocBuf(bq, &pfpage);
    if (err != BFE_OK)
        BF_ErrorHandler(err);

    err = BF_TouchBuf(bq);
    if (err != BFE_OK)
        BF_ErrorHandler(err);

    *pagenum = bq.pagenum;
    *(pagebuf) = pfpage->pagebuf;
    current_pfftab->hdrchanged = TRUE;
    current_pfftab->hdr.numpages++;

    return PFE_OK;
}
int  PF_GetFirstPage (int fd, int *pagenum, char **pagebuf){
    *pageNum = -1;

    return PF_GetNextPage(fd, pagenum, pagebuf);
}
int  PF_GetNextPage	(int fd, int *pagenum, char **pagebuf){

    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

    current_pfftab = PFftab + fd;
    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = FALSE;
    bq.pagenum = (*pagenum) + 1;

    if (current_pfftab->hdr.numpages <= bq.pagenum)
        return PFE_EOF;

    err = BF_GetBuf(bq, &fpage);
    if (err < BFE_OK)
        BF_ErrorHandler(err);

    *pagebuf = fpage->pagebuf;
    *pagenum++;

    return PFE_OK;
}
int  PF_GetThisPage	(int fd, int pagenum, char **pagebuf){
    
    PFpage* pfpage;
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

    current_pfftab = PFftab + fd;
    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = FALSE;
    bq.pagenum = (*pagenum);

    if (current_pfftab->hdr.numpages <= bq.pagenum)
        return PFE_EOF;

    err = BF_GetBuf(bq, &fpage);
    if (err < BFE_OK)
        BF_ErrorHandler(err);

    *pagebuf = fpage->pagebuf;

    return PFE_OK;
}
int  PF_DirtyPage	(int fd, int pagenum){
    
    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

    if (fd >= PFftab_length || fd < 0)
        return PFE_FD;

    current_pfftab = PFftab + fd;

    if (pagenum >= current_pfftab->hdr.numpages || pagenum < 0)
        return PFE_INVALIDPAGE;

    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    bq.pagenum = pagenum;

    err = BF_TouchBuf(bq);
    if (err != BFE_OK)
        BF_ErrorHandler(err);

    return PFE_OK;
}
int  PF_UnpinPage	(int fd, int pagenum, int dirty){

    BFreq bq;
    PFftab_ele *current_pfftab;
    int err;

    if (fd >= PFftab_length || fd < 0)
        return PFE_FD;

    current_pfftab = PFftab + fd;

    if (pagenum >= current_pfftab->hdr.numpages || pagenum < 0)
        return PFE_INVALIDPAGE;

    bq.fd = fd;
    bq.unixfd = current_pfftab->unixfd;
    bq.dirty = TRUE;
    bq.pagenum = pagenum;
    
    if (dirty){
        err = BF_TouchBuf(bq);
        if (err != BFE_OK)
            BF_ErrorHandler(err);
    }
    
    err = BF_UnpinBuf(bq);
    if (err != BFE_OK)
        BF_ErrorHandler(err);

    return PFE_OK;
}

void PF_PrintError(const char *errString)
{
	printf("PF Error : %s", errString);
	exit(1);
}