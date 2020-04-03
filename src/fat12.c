# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <time.h>
# include "fat12.h"
# include "fat12_internal.h"

// return 1 when success, else return 0
int readFloppyDisk(const char* file_name, floppy* disk) {
    FILE* fp = fopen(file_name, "rb");
    if (!fp) return 0;
    int read_size = fread(disk, sizeof(floppy), 1, fp);
    fclose(fp);
    return read_size == 1;
}

// return 1 when success, else return 0
int writeFloppyDisk(const char* file_name, const floppy* disk) {
    FILE* fp = fopen(file_name, "wb");
    if (!fp) return 0;
    int write_size = fwrite(disk, sizeof(floppy), 1, fp);
    fclose(fp);
    return write_size == 1;
}

// return 1 if the floppy disk is bootable, else return 0
int verifyBootId(const floppy* disk) {
    const BYTE* s = disk->storage;
    return s[510] == 0x55 && s[511] == 0xAA;
}

void printFat12Info(const floppy* disk) {
    // the start of floopy disk is exactly the header
    fat12_header* p = (fat12_header*)disk->storage;

    // calculate start address of boot program
    WORD jmp_addr = BOOT_START_ADDR + p->JmpCode[1] + 2;
    printf("Boot start address: 0x%04x\n", jmp_addr);

    char buffer[12];

    memcpy(buffer, p->BS_OEMName, 8);
    buffer[8] = '\0';
    printf("BS_OEMName:         %s\n", buffer);
    
    printf("BPB_BytesPerSec:    %u\n", p->BPB_BytesPerSec);
    printf("BPB_SecPerClus:     %u\n", p->BPB_SecPerClus);
    printf("BPB_RsvdSecCnt:     %u\n", p->BPB_RsvdSecCnt);
    printf("BPB_NumFATs:        %u\n", p->BPB_NumFATs);
    printf("BPB_RootEntCnt:     %u\n", p->BPB_RootEntCnt);
    printf("BPB_TotSec16:       %u\n", p->BPB_TotSec16);
    printf("BPB_Media:          0x%02x\n", p->BPB_Media);
    printf("BPB_FATSz16:        %u\n", p->BPB_FATSz16);
    printf("BPB_SecPerTrk:      %u\n", p->BPB_SecPerTrk);
    printf("BPB_NumHeads:       %u\n", p->BPB_NumHeads);
    printf("BPB_HiddSec:        %u\n", p->BPB_HiddSec);
    printf("BPB_TotSec32:       %u\n", p->BPB_TotSec32);
    printf("BS_DrvNum:          %u\n", p->BS_DrvNum);
    printf("BS_Reserved1:       %u\n", p->BS_Reserved1);
    printf("BS_BootSig:         0x%02x\n", p->BS_BootSig);
    printf("BS_VolID:           %u\n", p->BS_VolID);

    memcpy(buffer, p->BS_VolLab, 11);
    buffer[11] = '\0';
    printf("BS_VolLab:          %s\n", buffer);

    memcpy(buffer, p->BS_FileSysType, 8);
    buffer[8] = '\0';
    printf("BS_FileSysType:     %s\n", buffer);
}

void initDirWithRoot(directory* dir) {
    dir->clus_num = 0;
    dir->max_path_len = 256;
    dir->path_str = (char*)malloc(sizeof(char) * 256);
    memset(dir->path_str, 0, 256);
    dir->path_str[0] = '/';
}

void printAllInDir(const floppy* disk, const directory* dir) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;
    // assume bytes_per_clus is a multiple of sizeof(file_entry)
    int entries_per_clus = bytes_per_clus / sizeof(file_entry);

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;

    file_vector vector;
    fileVectorInit(&vector);

    BYTE* now_clus = (BYTE*)malloc(bytes_per_clus); // buffer for loading cluster
    if (dir->clus_num == 0) {
        // list root directory
        int root_head_sec = 1 + FAT_sectors;
        int max_root_entries = header->BPB_RootEntCnt;

        loadSectors(disk, root_head_sec, sec_per_clus, now_clus);
        int total = 0, i = 0;
        while (total < max_root_entries) {
            const file_entry* ent = &((const file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; // empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                fileVectorAppend(&vector, ent);
            }

            ++total;
            ++i;
            if (i >= entries_per_clus) {
                i = 0;
                int offset = (total/entries_per_clus) * sec_per_clus;
                loadSectors(disk, root_head_sec + offset, sec_per_clus, now_clus);
            }
        }
    } else {
        // list normal subdirectory (non-root)
        int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
        // assume bytes of root directory is a multiple of bytes per sector
        int root_sectors = root_bytes / header->BPB_BytesPerSec;
        int data_head_sec = 1 + FAT_sectors + root_sectors;

        int now_clus_num = dir->clus_num;
        int logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
        loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
        int i = 0;
        for (;;) {
            const file_entry* ent = &((const file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; //empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                fileVectorAppend(&vector, ent);
            }

            ++i;
            if (i >= entries_per_clus) {
                i = 0;
                now_clus_num = getNextClusNumFromFAT(disk, now_clus_num);
                if (clusNumIsEOF(now_clus_num)) break;
                logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
                loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            }
        }
    }
    free(now_clus);

    qsort(vector.storage, vector.size, sizeof(file_entry), fileEntCmp);
    int i = 0;
    if (vector.storage[0].DIR_Attr & FILE_ATTR_VOLLAB) {
        // print volumn label before the bar
        printFileEnt(&vector.storage[0]);
        ++i;
    }
    printf("Attribute Name    Type      Size   Last Changed Time\n");
    for (; i < vector.size; ++i) {
        printFileEnt(&vector.storage[i]);
    }
    fileVectorDestroy(&vector);
}

void printDirTree(const floppy* disk, const directory* dir) {
    ent_tree* tree = getEntTree(disk, dir->clus_num);
    printEntTree(tree, "", 0);
    entTreeDestroy(tree);
}

// return 1 when directory is changed successfully, else return 0
int changeDirectory(const floppy* disk, directory* dir, const char* path) {
    file_entry* ent = getFileEntByPath(disk, dir->clus_num, path);
    if (!ent) { // not found or path illegal
        return 0;
    } else if (!(ent->DIR_Attr & FILE_ATTR_DIR)) { // not a directory
        free(ent);
        return 0;
    }
    dir->clus_num = ent->DIR_FstClus;
    free(ent);

    // adjust dir->path_str
    int len = strlen(path);
    int is_absolute = (path[0] == '/');
    int origin_len = is_absolute ? 0 : strlen(dir->path_str);
    int now_max_len = dir->max_path_len;
    // dynamic extend string buffer size
    while (now_max_len < len + origin_len + 2) now_max_len *= 2; // +2 include '/' and '\0'
    if (now_max_len != dir->max_path_len) {
        char* temp = (char*)malloc(sizeof(char) * now_max_len);
        if (!is_absolute) {
            memcpy(temp, dir->path_str, origin_len);
        }
        dir->max_path_len = now_max_len;
        free(dir->path_str);
        dir->path_str = temp;
    }
    if (origin_len > 1) { // only "/" is end with '/'
        dir->path_str[origin_len] = '/';
        ++origin_len;
    }
    memcpy(dir->path_str + origin_len, path, len + 1);
    simplifyAbsolutePathString(dir->path_str);
    return 1;
}

// return 1 in case success, else return 0
int printFileContentByPath(const floppy* disk, const directory* dir, const char* path) {
    file_entry* ent = getFileEntByPath(disk, dir->clus_num, path);
    if (!ent) { // not found or path illegal
        return 0;
    } else if (ent->DIR_Attr & FILE_ATTR_DIR) { // not a file
        free(ent);
        return 0;
    }
    BYTE* buffer = (BYTE*)malloc(ent->DIR_FileSize);
    int loaded = readFileContentByEnt(disk, ent, buffer);
    if (loaded == 0) return 0; // something wrong with the file entry
    for (unsigned int i = 0; i < ent->DIR_FileSize; ++i) {
        putchar(buffer[i]);
    }
    putchar('\n');
    free(buffer);
    free(ent);
    return 1;
}

// copy file using path relative to directory, return 1 when succeed else return 0
int copyFileByPath(floppy* disk, const directory* dir, const char* src, const char* des) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    int bytes_per_clus = header->BPB_SecPerClus * header->BPB_BytesPerSec;

    file_entry* src_ent = getFileEntByPath(disk, dir->clus_num, src);
    if (!src_ent) return 0; // not found
    else if (src_ent->DIR_Attr & FILE_ATTR_DIR) { // not a file
        free(src_ent);
        return 0;
    }
    // Seperate destination directory (should exist already) and filename (should not exist)
    int len = strlen(des);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (des[i] == '/') break;
    }
    WORD des_dir = dir->clus_num;
    if (i >= 0) { // path includes a direcotry path before file name
        char* dir_path = (char*)malloc((i + 2) * sizeof(char));
        memcpy(dir_path, des, (i + 1));
        dir_path[i + 1] = '\0';
        file_entry* des_dir_ent = getFileEntByPath(disk, dir->clus_num, dir_path);
        free(dir_path);
        if (!des_dir_ent) {
            free(src_ent);
            return 0;
        }
        des_dir = des_dir_ent->DIR_FstClus;
        free(des_dir_ent);
    }
    char file_name[32];
    if (i == len - 1) {
        // given a directory path and no file name appointed, just use the same name as src
        formatNameToNormal(src_ent->DIR_Name, file_name);
    } else {
        int name_len = len - (i + 1);
        if (name_len > 31) name_len = 31; // prevent out-of-bounds access
        memcpy(file_name, des + i + 1, name_len);
        file_name[name_len] = '\0';
    }
    // Check if a file using the name exists in the directory
    file_entry* test = getFileEntByName(disk, des_dir, file_name);
    if (test) {
        if (!(test->DIR_Attr & FILE_ATTR_DIR)) { // destination file already exists
            free(test);
            free(src_ent);
            return 0;
        } else { // given a directory name without a '/'
            des_dir = test->DIR_FstClus;
            formatNameToNormal(src_ent->DIR_Name, file_name); // use the same name as src
            free(test);
        }
    }
    // Check "." and ".."
    if (!strcmp(file_name, ".") || !strcmp(file_name, "..")) {
        free(src_ent);
        return 0;
    }
    // set destination file entry content
    file_entry des_ent;
    memcpy(&des_ent, src_ent, sizeof(file_entry));
    formatNameToFATType(file_name, des_ent.DIR_Name); // set name

    time_t t = time(NULL);
    const struct tm* now_time = localtime(&t);
    setWrtTime(now_time, &des_ent.DIR_WrtTime, &des_ent.DIR_WrtDate); // set time

    int num_clus = (des_ent.DIR_FileSize + bytes_per_clus-1)/bytes_per_clus; // round up
    des_ent.DIR_FstClus = allocFATClus(disk, num_clus, 0); // set first cluster
    if (des_ent.DIR_FstClus == 0) { // no space
        free(src_ent);
        return 0;
    }
    // copy content to disk
    BYTE* buffer = (BYTE*)malloc(src_ent->DIR_FileSize);
    if (!readFileContentByEnt(disk, src_ent, buffer) || 
        !writeFileContentByEnt(disk, &des_ent, buffer)) {
        // read or write failed
        freeFATClus(disk, des_ent.DIR_FstClus);
        free(src_ent);
        return 0;
    }
    free(buffer);
    free(src_ent);
    if (!appendEntInDir(disk, des_dir, &des_ent)) { // failed to append
        freeFATClus(disk, des_ent.DIR_FstClus);
        return 0;
    }
    return 1;
}

// return 1 when succeed, else return 0
int removeFileByPath(floppy* disk, const directory* dir, const char* path) {
    ent_clus* info = getFileEntWithClusInfoByPath(disk, dir->clus_num, path);
    if (!info) return 0; // not found
    if (info->ent->DIR_Attr & FILE_ATTR_DIR) { // not a file
        destroyEntClusInfo(info);
        return 0;
    }
    freeFATClus(disk, info->ent->DIR_FstClus);
    *(BYTE*)info->ent = FILE_DEL_BYTE;
    writeSectors(disk, info->logic_sec_num, info->sec_per_clus, info->clus_buf);
    destroyEntClusInfo(info);
    return 1;
}

// move file or dir using path relative to directory, return 1 when succeed else return 0
int moveFileByPath(floppy* disk, const directory* dir, const char* src, const char* des) {
    ent_clus* src_info = getFileEntWithClusInfoByPath(disk, dir->clus_num, src);
    if (!src_info) return 0; // not found
    char src_name[12];
    memcpy(src_name, src_info->ent->DIR_Name, 11);
    src_name[11] = '\0';
    if (src_info->ent->DIR_FstClus == 0 || 
        !strcmp(src_name, ".          ") || !strcmp(src_name, "..         ")) {
        // src is root or reserved entry
        destroyEntClusInfo(src_info);
        return 0;
    }
    // Seperate destination directory (should exist already) and filename (should not exist)
    int len = strlen(des);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (des[i] == '/') break;
    }
    WORD des_dir = dir->clus_num;
    if (i >= 0) { // path includes a direcotry path before file name
        char* dir_path = (char*)malloc((i + 2) * sizeof(char));
        memcpy(dir_path, des, (i + 1));
        dir_path[i + 1] = '\0';
        file_entry* des_dir_ent = getFileEntByPath(disk, dir->clus_num, dir_path);
        free(dir_path);
        if (!des_dir_ent) {
            destroyEntClusInfo(src_info);
            return 0;
        }
        des_dir = des_dir_ent->DIR_FstClus;
        free(des_dir_ent);
    }
    char file_name[32];
    if (i == len - 1) {
        // given a directory path and no file name appointed, just use the same name as src
        formatNameToNormal(src_info->ent->DIR_Name, file_name);
    } else {
        int name_len = len - (i + 1);
        if (name_len > 31) name_len = 31; // prevent out-of-bounds access
        memcpy(file_name, des + i + 1, name_len);
        file_name[name_len] = '\0';
    }
    // Check if a file using the name exists in the directory
    file_entry* test = getFileEntByName(disk, des_dir, file_name);
    if (test) {
        if (!(test->DIR_Attr & FILE_ATTR_DIR)) { // destination file already exists
            free(test);
            destroyEntClusInfo(src_info);
            return 0;
        } else { // given a directory name without a '/'
            des_dir = test->DIR_FstClus;
            formatNameToNormal(src_info->ent->DIR_Name, file_name); // use the same name as src
            free(test);
        }
    }
    // Check "." and ".."
    if (!strcmp(file_name, ".") || !strcmp(file_name, "..")) {
        destroyEntClusInfo(src_info);
        return 0;
    }
    // Check parent relationship
    if (src_info->ent->DIR_Attr & FILE_ATTR_DIR) {
        if (isParent(disk, src_info->ent->DIR_FstClus, des_dir)) {
            destroyEntClusInfo(src_info);
            return 0;
        }
    }
    // set destination file entry content
    file_entry des_ent;
    memcpy(&des_ent, src_info->ent, sizeof(file_entry));
    formatNameToFATType(file_name, des_ent.DIR_Name); // set name

    time_t t = time(NULL);
    const struct tm* now_time = localtime(&t);
    setWrtTime(now_time, &des_ent.DIR_WrtTime, &des_ent.DIR_WrtDate); // set time

    // mark source file entry as deleted
    BYTE backup = *(BYTE*)(src_info->ent); // to used when need recover
    *(BYTE*)(src_info->ent) = FILE_DEL_BYTE;
    writeSectors(disk, src_info->logic_sec_num, src_info->sec_per_clus, src_info->clus_buf);
    // add destination file entry to disk, this should after delete source entry
    // because `clus_buf` of `src_info` has probability of coverring added entry
    if (!appendEntInDir(disk, des_dir, &des_ent)) { // failed to append
        *(BYTE*)(src_info->ent) = backup; // recover
        writeSectors(disk, src_info->logic_sec_num, src_info->sec_per_clus, src_info->clus_buf);
        destroyEntClusInfo(src_info);
        return 0;
    }
    destroyEntClusInfo(src_info);
    return 1;
}

// return 1 when succeed else return 0
int makeDirByPath(floppy* disk, const directory* dir, const char* path) {
    // Seperate destination directory (should exist already) and new dirname (should not exist)
    int len = strlen(path);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (path[i] == '/') break;
    }
    if (i == len - 1) return 0; // given a directory path and no new dirname appointed
    WORD des_dir = dir->clus_num;
    if (i >= 0) { // path includes a direcotry path before new dirname
        char* dir_path = (char*)malloc((i + 2) * sizeof(char));
        memcpy(dir_path, path, (i + 1));
        dir_path[i + 1] = '\0';
        file_entry* des_dir_ent = getFileEntByPath(disk, dir->clus_num, dir_path);
        free(dir_path);
        if (!des_dir_ent) return 0; // illegal path
        des_dir = des_dir_ent->DIR_FstClus;
        free(des_dir_ent);
    }
    const char* dirname = path + (i + 1);
    // Check if a file using the name exists in the directory
    file_entry* test = getFileEntByName(disk, des_dir, dirname);
    if (test) {
        free(test);
        return 0;
    }
    // append the new directory entry to destination directory
    file_entry newdir;
    formatNameToFATType(dirname, newdir.DIR_Name); // set name
    newdir.DIR_Attr = FILE_ATTR_DIR; // set attribute
    memset(newdir.Reserve, 0, 10); // set reserved
    time_t t = time(NULL);
    struct tm* now_time = localtime(&t);
    setWrtTime(now_time, &newdir.DIR_WrtTime, &newdir.DIR_WrtDate); // set time
    newdir.DIR_FstClus = allocFATClus(disk, 1, 0); // alloc cluster
    if (!newdir.DIR_FstClus) return 0; // probably space is run out
    newdir.DIR_FileSize = 0; // set size (for directory is 0)
    if (!appendEntInDir(disk, des_dir, &newdir)) {
        freeFATClus(disk, newdir.DIR_FstClus);
        return 0;
    }
    // create "." and ".." entries
    WORD newdir_clus_num = newdir.DIR_FstClus;
    memcpy(newdir.DIR_Name, ".          ", 11);
    appendEntInDir(disk, newdir_clus_num, &newdir); // This MUST be success
    newdir.DIR_Name[1] = '.';
    newdir.DIR_FstClus = des_dir; // parent directory
    appendEntInDir(disk, newdir_clus_num, &newdir); // This MUST be success
    return 1;
}

// remove a directory (and everything in it). Return 1 when succeed else return 0
int removeDirByPath(floppy* disk, const directory* dir, const char* path) {
    ent_clus* info = getFileEntWithClusInfoByPath(disk, dir->clus_num, path);
    if (!info) return 0; // not found
    char ent_name[12];
    ent_name[11] = '\0';
    memcpy(ent_name, info->ent->DIR_Name, 11);
    if (!(info->ent->DIR_Attr & FILE_ATTR_DIR) || info->ent->DIR_FstClus == 0
        || !strcmp(ent_name, ".          ") || !strcmp(ent_name, "..         ")) {
        // not a directory or directory is root or reserved entry
        destroyEntClusInfo(info);
        return 0;
    }
    removeAllInDir(disk, info->ent->DIR_FstClus);
    freeFATClus(disk, info->ent->DIR_FstClus);
    *(BYTE*)(info->ent) = FILE_DEL_BYTE;
    writeSectors(disk, info->logic_sec_num, info->sec_per_clus, info->clus_buf);
    destroyEntClusInfo(info);
    return 1;
}

// concat content of two files to one new file, return 1 when succeed else return 0
int concatFileByPath(floppy* disk, const directory* dir, 
    const char* src1,
    const char* src2,
    const char* des) 
{
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    int bytes_per_clus = header->BPB_SecPerClus * header->BPB_BytesPerSec;

    file_entry* src_ent1 = getFileEntByPath(disk, dir->clus_num, src1);
    if (!src_ent1) return 0; // not found
    if (src_ent1->DIR_Attr & FILE_ATTR_DIR) { // not a file
        free(src_ent1);
        return 0;
    }
    file_entry* src_ent2 = getFileEntByPath(disk, dir->clus_num, src2);
    if (!src_ent2) { // not found
        free(src_ent1);
        return 0;
    }
    if (src_ent2->DIR_Attr & FILE_ATTR_DIR) { // not a file
        free(src_ent1);
        free(src_ent2);
        return 0;
    }
    int file_size = src_ent1->DIR_FileSize + src_ent2->DIR_FileSize;
    BYTE* buffer = (BYTE*)malloc(file_size);
    if (!readFileContentByEnt(disk, src_ent1, buffer) ||
        !readFileContentByEnt(disk, src_ent2, buffer + src_ent1->DIR_FileSize)) {
        // failed to read
        free(src_ent1);
        free(src_ent2);
        free(buffer);
        return 0;
    }
    free(src_ent1);
    free(src_ent2);
    // Seperate destination directory (should exist already) and filename (should not exist)
    int len = strlen(des);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (des[i] == '/') break;
    }
    if (i == len - 1) { // given a directory path and no file name appointed
        free(buffer);
        return 0;
    }
    WORD des_dir = dir->clus_num;
    if (i >= 0) { // path includes a direcotry path before file name
        char* dir_path = (char*)malloc((i + 2) * sizeof(char));
        memcpy(dir_path, des, (i + 1));
        dir_path[i + 1] = '\0';
        file_entry* des_dir_ent = getFileEntByPath(disk, dir->clus_num, dir_path);
        free(dir_path);
        if (!des_dir_ent) {
            free(buffer);
            return 0;
        }
        des_dir = des_dir_ent->DIR_FstClus;
        free(des_dir_ent);
    }
    const char* file_name = des + (i + 1);
    // Check "." and ".."
    if (!strcmp(file_name, ".") || !strcmp(file_name, "..")) {
        free(buffer);
        return 0;
    }
    // Check if a file using the name exists in the directory
    file_entry* test = getFileEntByName(disk, des_dir, file_name);
    if (test) {
        free(test);
        free(buffer);
        return 0;
    }
    // set file entry infomation
    file_entry des_ent;
    formatNameToFATType(file_name, des_ent.DIR_Name); // set name
    des_ent.DIR_Attr = FILE_ATTR_ARCH; // set attribute
    memset(des_ent.Reserve, 0, 10); // clean reserved (no sense though)
    time_t t = time(NULL);
    struct tm* now_time = localtime(&t);
    setWrtTime(now_time, &des_ent.DIR_WrtTime, &des_ent.DIR_WrtDate);
    int num_clus = (file_size + bytes_per_clus-1)/bytes_per_clus; // round up
    des_ent.DIR_FstClus = allocFATClus(disk, num_clus, 0);
    if (!des_ent.DIR_FstClus) { // failed to allocate cluster
        free(buffer);
        return 0;
    }
    des_ent.DIR_FileSize = file_size;
    if (!writeFileContentByEnt(disk, &des_ent, buffer)) { // failed to write
        freeFATClus(disk, des_ent.DIR_FstClus);
        free(buffer);
        return 0;
    }
    free(buffer);
    if (!appendEntInDir(disk, des_dir, &des_ent)) { // failed to append entry
        freeFATClus(disk, des_ent.DIR_FstClus);
        return 0;
    }
    return 1;
}

// return 1 when succeed else return 0
// for convenience this is a completement with low efficiency
int copyDirByPath(floppy* disk, const directory* dir, const char* src, const char* des) {
    file_entry* src_ent = getFileEntByPath(disk, dir->clus_num, src);
    if (!src_ent) return 0;
    if (!(src_ent->DIR_Attr & FILE_ATTR_DIR)) {
        free(src_ent);
        return 0;
    }
    if (!makeDirByPath(disk, dir, des)) {
        free(src_ent);
        return 0;
    }
    file_entry* des_ent = getFileEntByPath(disk, dir->clus_num, des);
    if (isParent(disk, src_ent->DIR_FstClus, des_ent->DIR_FstClus)) {
        removeDirByPath(disk, dir, des);
        free(src_ent);
        free(des_ent);
    }
    free(des_ent);
    WORD srcdir_clus_num = src_ent->DIR_FstClus;
    free(src_ent);
    ent_tree* tree = getEntTree(disk, srcdir_clus_num);
    int src_len = strlen(src);
    int des_len = strlen(des);
    if (!copyDirInternalRecursion(disk, dir, src, src_len, des, des_len, tree)) {
        removeDirByPath(disk, dir, des);
        entTreeDestroy(tree);
        return 0;
    }
    entTreeDestroy(tree);
    return 1;
}

// free allocated memory
void destroyDir(directory* dir) {
    free(dir->path_str);
}