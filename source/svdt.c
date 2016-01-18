// svdt.c: functions for svdt
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>

#include "utils.h"
#include "smdh.h"
#include "svdt.h"
#include "filesystem.h"
#include "text.h"

void freeDir(lsDir* dir)
{
    if (!dir) return;
    // still totally cribbing from 3ds_hb_menu
    lsLine* line = dir->firstLine;
    lsLine* temp = NULL;
    while(line)
    {
        temp = line->nextLine;
        line->nextLine = NULL;
        free(line);
        line = temp;
    }
    dir->dirEntryCount = 0;
    dir->firstLine = NULL;
}

// hey look yet another 3ds_hb_menu derivative
void gotoParentDirectory(lsDir* dir)
{
    char* cwd = dir->thisDir;
    char *p = cwd + strlen(cwd)-2;
    while(p > cwd && *p != '/') *p-- = 0;
}

void gotoSubDirectory(lsDir* dir, char* basename)
{
    if (!dir || !basename) return;
    char* cwd = dir->thisDir;
    cwd = strcat(cwd,basename);
    if (cwd[strlen(cwd+1)] != '/')
    {
        if(canHasConsole){
            //printf("\nappending slash");
        }
        cwd = strcat(cwd,"/");
    }
    if (canHasConsole)
    {
        textcolour(NEONGREEN);
        printf("\nsubdir path: %s\n",cwd);
    }
}

char* lsDirBasename(lsDir* dir)
{
    char* ret = NULL;
    // our convention is awkward enough to have to find the second-to-last slash in most cases
    char* baseidx = dir->thisDir;
    char* baseidx2 = strrchr(dir->thisDir,'/');
    while (baseidx!=baseidx2)
    {
        ret = baseidx+1;
        baseidx = strchr(baseidx+1,'/');
    }
    return ret;
}

int lsLine_cmp(lsLine* line1, lsLine* line2)
{
    if (!line1)
        return 1;
    if (!line2)
        return -1;
    int cmp = (line2->isDirectory) - (line1->isDirectory);
    if (cmp) {
        return cmp;
    } else {
        cmp = strcasecmp(line1->thisLine,line2->thisLine);
        if (cmp) return cmp;
        return -strcmp(line1->thisLine,line2->thisLine);
    }
}

void scanDir(lsDir* dir, FS_Archive* archive)
{
    if (!dir) return;

    dir->firstLine = NULL;
    Handle dirHandle;
    Result res = FSUSER_OpenDirectory(&dirHandle, *archive, fsMakePath(PATH_ASCII, dir->thisDir));
    dir->dirEntryCount = 0;
    dir->lsOffset = 0;
    // cribbing from 3ds_hb_menu again
    u32 entriesRead;
    lsLine* lastLine = NULL;
    static char pathname[MAX_PATH_LENGTH];
    do
    {
        static FS_DirectoryEntry entry;
        memset(&entry,0,sizeof(FS_DirectoryEntry));
        entriesRead=0;
        res = FSDIR_Read(dirHandle, &entriesRead, 1, &entry);
        if (res)
        {
            if (canHasConsole)
            {
                textcolour(RED);
                printf("! error reading directory\n");
                printf("result code %08x\n",(unsigned int)res);
            }
            return;
        }
        unicodeToChar(pathname,entry.name,MAX_PATH_LENGTH);
        if(entriesRead)
        {
            lsLine* tempLine = (lsLine*)malloc(sizeof(lsLine));
            strncpy(tempLine->thisLine,pathname,MAX_PATH_LENGTH);
            tempLine->isDirectory = entry.attributes & FS_ATTRIBUTE_DIRECTORY;
            tempLine->fileSize = entry.fileSize;
            tempLine->nextLine = NULL;
            if (!alphabetSort)
            {
                if(dir->firstLine)
                {
                    lastLine->nextLine = tempLine;
                } else {
                    dir->firstLine = tempLine;
                }
                lastLine = tempLine;
            } else {
                if(dir->firstLine == NULL) {
                    dir->firstLine = tempLine;
                } else {
                    lastLine = dir->firstLine;
                    if(lsLine_cmp(tempLine,lastLine)<0) {
                        tempLine->nextLine = lastLine;
                        dir->firstLine = tempLine;
                    } else {
                        while(lsLine_cmp(lastLine->nextLine,tempLine)<0)
                        {
                            lastLine = lastLine->nextLine;
                        }
                        tempLine->nextLine = lastLine->nextLine;
                        lastLine->nextLine = tempLine;
                    }
                }
            }
            dir->dirEntryCount++;
        }
    }while (entriesRead);
    FSDIR_Close(dirHandle); // oh god how did I forget this line
}

// functions and structs for getting target title

Result getTitleList(u8 mediatype, int* usable_count)
{
    u32 i;
    // cribbing from 3ds_hb_menu for the ???th time
    u32 num;
    Result ret = AM_GetTitleCount(mediatype, &num);
    if(ret)
        return ret;
    u64* tmp = (u64*)malloc(sizeof(u64) * num);
    if(!tmp)
        return -1;
    ret = AM_GetTitleIdList(mediatype, num, tmp);
    int running_count = 0;
    lsTitle* currentTitle;
    currentTitle = NULL;
    // only keep system + normal + demo titles
    for (i=0;i<num;i++)
    {
        u64 tid = tmp[i];
        u32 tid_high = tid >> 32;
        if (tid_high == 0x00040010 || tid_high == 0x00040000 || tid_high == 0x00040002)
        {
            lsTitle* tempTitle = (lsTitle*)malloc(sizeof(lsTitle));
            tempTitle->thisTitle = tmp[i];
            tempTitle->nextTitle = NULL;
            if(!firstTitle)
            {
                firstTitle = tempTitle;
            }
            else
                currentTitle->nextTitle = tempTitle;
            currentTitle = tempTitle;
            running_count++;
        }
    }
    *usable_count = running_count;
    return ret;
}

void clearTitleList()
{
    // still totally cribbing from 3ds_hb_menu
    lsTitle* title = firstTitle;
    lsTitle* temp = NULL;
    while(title)
    {
        temp = title->nextTitle;
        title->nextTitle = NULL;
        free(title);
        title = temp;
    }
    firstTitle = NULL;
}

Result getTitleTitle(u64 tid, u8 mediatype, char* titleTitle)
{
    filesystemReadySaveRead();
    Handle fileHandle;
    smdh_s* icon = malloc(sizeof(smdh_s));
    u32 tid_high = tid >> 32;
    u32 tid_low = tid & 0xffffffff;
    u32 archivePath[] = {tid_low, tid_high, mediatype, 0x00000000};
    static const u32 filePath[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};    
    FS_Archive saveArchive = {0x2345678a, (FS_Path){PATH_BINARY, 0x10, (u8*)archivePath}, 0};
    FS_Path savePath = {PATH_BINARY, 0x14, (u8*)filePath};
    Result ret = FSUSER_OpenFileDirectly(&fileHandle, saveArchive, savePath, FS_OPEN_READ, 0);
    if(R_FAILED(ret)) return ret;
    
    u32 bytesRead;
    ret = FSFILE_Read(fileHandle, &bytesRead, 0x0, icon, sizeof(smdh_s));
    char buffer[0x40];
    unicodeToChar(buffer,icon->applicationTitles[1].shortDescription,0x40);
    strncpy(titleTitle,buffer,0x40);
    FSFILE_Close(fileHandle);
    
    // excise special characters from title
    // (necessary because we just use this string for directory names)
    // (thanks Blazingflare on GBAtemp)
    char* forbiddenChar;
    while ((forbiddenChar = strpbrk(titleTitle,"<>:\"\\/|?*.")))
    {
        titleTitle[forbiddenChar-titleTitle] = ' ';
    }
    if (titleTitle[strlen(titleTitle)-1] == ' ') titleTitle[strlen(titleTitle)-1] = '\0';

    filesystemDoneSaveRead();
    return ret;
}

Result nthTitleInList(int n, u8 mediatype, char* titleTitle, u64* tid)
{
    int i;
    lsTitle* currentTitle = firstTitle;
    for (i=0;i<n;i++)
    {
        if(!currentTitle)
            return -1;
        currentTitle = currentTitle->nextTitle;
    }
    Result res = getTitleTitle(currentTitle->thisTitle,mediatype,titleTitle);
    if(res)
    {
        sprintf(titleTitle,"[tid:%08x%08x]",(unsigned int)(currentTitle->thisTitle>>32),(unsigned int)(currentTitle->thisTitle & 0xffffffff));
    }
    *tid = currentTitle->thisTitle;
    return res;
}

