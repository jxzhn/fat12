# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <ctype.h>
# include <time.h>
# include "fat12.h"
# include "fat12_internal.h"

// to emulate the real way using BIOS
void loadSectors(const floppy* disk, WORD logic_sec_num, WORD count, BYTE* buf) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    memcpy(buf, disk->storage + logic_sec_num * header->BPB_BytesPerSec, header->BPB_BytesPerSec * count);
}

void writeSectors(floppy* disk, WORD logic_sec_num, WORD count, const BYTE* buf) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    memcpy(disk->storage + logic_sec_num * header->BPB_BytesPerSec, buf, header->BPB_BytesPerSec * count);
}

// write the number to specific position of FAT12 record
void writeFATAtPosition(BYTE* FAT, WORD pos, WORD num) {
    if (pos & 1) { // odd
        int offset = (pos - 1) / 2 * 3 + 1;
        WORD* at = (WORD*)(FAT + offset);
        WORD target = (0x000F & (*at)) | (num << 4);
        *at = target;
    } else { // even
        int offset = pos / 2 * 3;
        WORD* at = (WORD*)(FAT + offset);
        WORD target = (0xF000 & (*at)) | (0x0FFF & num);
        *at = target;
    }
}

WORD readFATAtPosition(const BYTE* FAT, WORD pos) {
    if (pos & 1) { // odd
        int offset = (pos - 1) / 2 * 3 + 1;
        WORD num = *(const WORD*)(FAT + offset);
        return (num >> 4);
    } else { // even
        int offset = pos / 2 * 3;
        WORD num = *(const WORD*)(FAT + offset);
        return (num & 0x0FFF);
    }
}

WORD getNextClusNumFromFAT(const floppy* disk, WORD clus_num) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    // FAT1 is located at the second sector (which is after MBR sector)
    const BYTE* FAT1 = disk->storage + header->BPB_BytesPerSec;
    return readFATAtPosition(FAT1, clus_num);
}

void getWrtTimeFromFileEnt(
    const file_entry* ent, 
    int* year, int* month, int* date,
    int* hour, int* minute, int* second)
{
    *hour = (ent->DIR_WrtTime & 0xF800) >> 11;
    *minute = (ent->DIR_WrtTime & 0x07E0) >> 5;
    *second = (ent->DIR_WrtTime & 0x001F) << 1;

    *year = ((ent->DIR_WrtDate & 0xFE00) >> 9) + 1980;
    *month = (ent->DIR_WrtDate & 0x01E0) >> 5;
    *date = (ent->DIR_WrtDate & 0x001F);
}

void setWrtTime(const struct tm* time, WORD* WrtTime, WORD* WrtDate) {
    WORD hour = time->tm_hour << 11;
    WORD minute = time->tm_min << 5;
    WORD second = time->tm_sec >> 1;
    *WrtTime = hour | minute | second;
    WORD year = (time->tm_year + 1900 - 1980) << 9;
    WORD month = (time->tm_mon + 1) << 5;
    WORD date = time->tm_mday;
    *WrtDate = year | month | date;
}

void printFileEnt(const file_entry* ent) {
    char buffer[12];
    
    if (ent->DIR_Attr & FILE_ATTR_VOLLAB) {
        memcpy(buffer, ent->DIR_Name, 11);
        buffer[11] = '\0';
        printf("VOLLAB:   %s\n", buffer);
        return;
    }
    // print attribute in formmat "drwahs"
    if (ent->DIR_Attr & FILE_ATTR_DIR) {
        buffer[0] = 'd';
        buffer[1] = buffer[2] = buffer[3] = '-';
    } else {
        buffer[0] = '-';
        buffer[1] = 'r';
        buffer[2] = (ent->DIR_Attr & FILE_ATTR_RO) ? '-' : 'w';
        buffer[3] = (ent->DIR_Attr & FILE_ATTR_ARCH) ? 'a' : '-';
    }
    buffer[4] = (ent->DIR_Attr & FILE_ATTR_HIDDEN) ? 'h' : '-';
    buffer[5] = (ent->DIR_Attr & FILE_ATTR_SYSTEM) ? 's' : '-';
    buffer[6] = '\0';
    printf("%s    ", buffer);
    // print name
    memcpy(buffer, ent->DIR_Name, 8);
    buffer[8] = '\0';
    printf("%s ", buffer);
    // print type
    memcpy(buffer, ent->DIR_Name + 8, 3);
    buffer[3] = '\0';
    printf("%s ", buffer);
    // print file length
    printf("%9d ", ent->DIR_FileSize);
    // print last changed time
    int year, month, date, hour, minute, second;
    getWrtTimeFromFileEnt(
        ent, &year, &month, &date, 
        &hour, &minute, &second);
    printf("%4d-%02d-%02d %02d:%02d:%02d\n", year, month, date, 
        hour, minute, second);
}

// ----------- a simple completement of C++ vector -----------

void fileVectorInit(file_vector* p) {
    p->storage=(file_entry*)malloc(sizeof(file_entry) * 2);
    p->max_size = 2;
    p->size = 0;
}

void fileVectorAppend(file_vector* p, const file_entry* ent) {
    if (p->size == p->max_size) {
        file_entry* temp = (file_entry*)malloc(sizeof(file_entry) * (p->max_size * 2));
        memcpy(temp, p->storage, sizeof(file_entry) * p->max_size);
        free(p->storage);
        p->storage = temp;
        p->max_size *= 2;
    }
    memcpy(p->storage + p->size, ent, sizeof(file_entry));
    ++p->size;
}

void fileVectorDestroy(file_vector* p) {
    free(p->storage);
}

// ----------- ----------------------------------- -----------

int fileEntCmp(const void* x, const void* y) { // use to sort entries
    const file_entry* a = (const file_entry*)x;
    const file_entry* b = (const file_entry*)y;
    // volumn label should be the foremost, assume only one of the two could be volumn label
    if (a->DIR_Attr & FILE_ATTR_VOLLAB) return -1;
    else if (b->DIR_Attr & FILE_ATTR_VOLLAB) return 1;
    // directory should be before file
    int a_is_dir = a->DIR_Attr & FILE_ATTR_DIR;
    int b_is_dir = b->DIR_Attr & FILE_ATTR_DIR;
    if (a_is_dir && !b_is_dir) return -1;
    else if (!a_is_dir && b_is_dir) return 1;
    char buffer1[12], buffer2[12];
    memcpy(buffer1, a->DIR_Name, 11);
    buffer1[11] = '\0';
    memcpy(buffer2, b->DIR_Name, 11);
    buffer2[11] = '\0';
    if (a_is_dir && b_is_dir) {
        // "." and ".." should before other directory
        if (!strcmp(buffer1, ".          ")) return -1;
        else if (!strcmp(buffer2, ".          ")) return 1;
        else if (!strcmp(buffer1, "..         ")) return -1;
        else if (!strcmp(buffer2, "..         ")) return 1;
    }
    return strcmp(buffer1, buffer2);
}

void formatNameToNormal(const BYTE* FAT_name, char* buffer) {
    int name_len;
    for (name_len = 7; name_len >= 0; --name_len) {
        if (FAT_name[name_len] != ' ') break;
    }
    ++name_len;
    memcpy(buffer, FAT_name, name_len);
    int ext_len;
    for (ext_len = 2; ext_len >= 0; --ext_len) {
        if (FAT_name[8 + ext_len] != ' ') break;
    }
    ++ext_len;
    if (ext_len != 0) {
        buffer[name_len] = '.';
        memcpy(buffer + (name_len + 1), FAT_name + 8, ext_len);
        buffer[name_len + ext_len + 1] = '\0';
    } else {
        buffer[name_len] = '\0';
    }
}

// return 1 in case success, else return 0
void formatNameToFATType(const char* name, BYTE* buffer) {
    char file_name[8] = "        ";
    char file_ext[3] = "   ";
    int len = strlen(name);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (name[i] == '.') break;
    }
    if (i != -1 && i != len - 1) {
        for (int k = 0; k < 8 && k < i; ++k) {
            file_name[k] = isalpha(name[k]) ? toupper(name[k]) : name[k];
        }
        for (int k = 0, j = i + 1; k < 3 && j < len; ++k, ++j) {
            file_ext[k] = isalpha(name[j]) ? toupper(name[j]) : name[j];
        }
    } else {
        for (int k = 0; k < 8 && k < len; ++k) {
            file_name[k] = isalpha(name[k]) ? toupper(name[k]) : name[k];
        }
    }
    memcpy(buffer, file_name, 8);
    memcpy(buffer + 8, file_ext, 3);
}

// ----------- a simple completement of tree -----------

void entTreeInit(ent_tree* p) {
    p->storage = (ent_tree_node*)malloc(sizeof(ent_tree_node) * 2);
    p->max_size = 2;
    p->size = 0;
}

void entTreeAppend(ent_tree* p, const file_entry* ent, void* sub_tree) {
    if (p->size == p->max_size) {
        ent_tree_node* temp = (ent_tree_node*)malloc(sizeof(ent_tree_node) * (p->max_size * 2));
        memcpy(temp, p->storage, sizeof(ent_tree_node) * p->max_size);
        free(p->storage);
        p->storage = temp;
        p->max_size *= 2;
    }
    memcpy(&(p->storage + p->size)->ent, ent, sizeof(file_entry));
    (p->storage + p->size)->sub_tree = sub_tree;
    ++p->size;
}

int entTreeNodeCmp(const void* x, const void* y) { // use to sort
    const ent_tree_node* a = (const ent_tree_node*)x;
    const ent_tree_node* b = (const ent_tree_node*)y;
    return fileEntCmp(&a->ent, &b->ent);
}

void entTreeDestroy(ent_tree* p) {
    for (size_t i = 0; i < p->size; ++i) {
        if (p->storage[i].sub_tree != NULL) entTreeDestroy((ent_tree*)p->storage[i].sub_tree);
    }
    free(p->storage);
    free(p);
}

// ----------- ----------------------------- -----------

// the pointer returned by this function should be destroyed by function `entTreeDestroy`
ent_tree* getEntTree(const floppy* disk, WORD dir_clus_num) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;
    // assume bytes_per_clus is a multiple of sizeof(file_entry)
    int entries_per_clus = bytes_per_clus / sizeof(file_entry);

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;

    BYTE* now_clus = (BYTE*)malloc(bytes_per_clus);
    ent_tree* tree = (ent_tree*)malloc(sizeof(ent_tree));
    entTreeInit(tree);
    char buffer[13];
    if (dir_clus_num == 0) {
        // now in root directory
        int root_head_sec = 1 + FAT_sectors;
        int max_root_entries = header->BPB_RootEntCnt;

        loadSectors(disk, root_head_sec, sec_per_clus, now_clus);
        int total = 0, i = 0;
        while (total < max_root_entries) {
            const file_entry* ent = &((const file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; // empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                formatNameToNormal(ent->DIR_Name, buffer);
                if (!(ent->DIR_Attr & FILE_ATTR_VOLLAB)) {
                    // skip volumn label, self and last level directory
                    ent_tree* sub_tree = NULL;
                    if (ent->DIR_Attr & FILE_ATTR_DIR) {
                        sub_tree = getEntTree(disk, ent->DIR_FstClus);
                    }
                    entTreeAppend(tree, ent, sub_tree);
                }
            }

            ++total;
            ++i;
            if (i >= entries_per_clus) {
                int offset = (total/entries_per_clus) * sec_per_clus;
                loadSectors(disk, root_head_sec + offset, sec_per_clus, now_clus);
            }
        }
    } else {
        // now in normal subdirectory (non-root)
        int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
        // assume bytes of root directory is a multiple of bytes per sector
        int root_sectors = root_bytes / header->BPB_BytesPerSec;
        int data_head_sec = 1 + FAT_sectors + root_sectors;

        int now_clus_num = dir_clus_num;
        int logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
        loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
        int i = 0;
        for (;;) {
            const file_entry* ent = &((const file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; //empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                formatNameToNormal(ent->DIR_Name, buffer);
                if (!(ent->DIR_Attr & FILE_ATTR_VOLLAB) && strcmp(buffer, ".") && strcmp(buffer, "..")) {
                    // skip volumn label, self and last level directory
                    ent_tree* sub_tree = NULL;
                    if (ent->DIR_Attr & FILE_ATTR_DIR) {
                        sub_tree = getEntTree(disk, ent->DIR_FstClus);
                    }
                    entTreeAppend(tree, ent, sub_tree);
                }
            }

            ++i;
            if (i >= entries_per_clus) {
                now_clus_num = getNextClusNumFromFAT(disk, now_clus_num);
                if (clusNumIsEOF(now_clus_num)) break;
                logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
                loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            }
        }
    }
    free(now_clus);
    if (tree->size == 0) {
        entTreeDestroy(tree);
        tree = NULL;
    }
    return tree;
}

void printEntTree(const ent_tree* p, const char* indent, int indent_len) {
    qsort(p->storage, p->size, sizeof(ent_tree_node), entTreeNodeCmp);
    char buffer[13];
    // append 4 char to next level, and a byte '\0'
    char* next_indent = (char*)malloc(indent_len + 5);
    sprintf(next_indent, "%s |  ", indent);
    for (size_t i = 0; i < p->size; ++i) {
        // the last one is a little special
        formatNameToNormal(p->storage[i].ent.DIR_Name, buffer);
        if (i == p->size - 1) {
            sprintf(next_indent, "%s    ", indent);
            printf("%s `-- %s\n", indent, buffer);
        }
        else printf("%s |-- %s\n", indent, buffer);
        
        if (p->storage[i].sub_tree != NULL) {
            printEntTree(p->storage[i].sub_tree, next_indent, indent_len + 4);
        }
    }
    free(next_indent);
}

void destroyEntClusInfo(ent_clus* p) {
    free(p->clus_buf);
    free(p);
}

// get file entry with cluster buffer and infomation, return NULL when not found
// the pointer returned (except NULL) should be destroyed by `destroyEntClusInfo`
ent_clus* getFileEntWithClusInfoByName(const floppy* disk, WORD dir_clus_num, const char* name) {
    char file_name[12];
    char ent_name[12];
    memset(ent_name, 0, 12);
    formatNameToFATType(name, (BYTE*)file_name);
    file_name[11] = '\0';

    const fat12_header* header = (const fat12_header*)disk->storage;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;
    // assume bytes_per_clus is a multiple of sizeof(file_entry)
    int entries_per_clus = bytes_per_clus / sizeof(file_entry);

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;

    BYTE* now_clus = (BYTE*)malloc(bytes_per_clus);
    ent_clus* result = (ent_clus*)malloc(sizeof(ent_clus));
    result->clus_buf = now_clus;
    result->sec_per_clus = sec_per_clus;
    if (dir_clus_num == 0) {
        // now in root directory
        int root_head_sec = 1 + FAT_sectors;
        int max_root_entries = header->BPB_RootEntCnt;

        loadSectors(disk, root_head_sec, sec_per_clus, now_clus);
        int total = 0, i = 0;
        while (total < max_root_entries) {
            file_entry* ent = &((file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; // empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                memcpy(ent_name, ent->DIR_Name, 11);
                if (!strcmp(ent_name, file_name)) { // the entry is found
                    int offset = (total/entries_per_clus) * sec_per_clus;
                    result->logic_sec_num = root_head_sec + offset;
                    result->ent = ent;
                    return result;
                }
            }

            ++total;
            ++i;
            if (i >= entries_per_clus) {
                int offset = (total/entries_per_clus) * sec_per_clus;
                loadSectors(disk, root_head_sec + offset, sec_per_clus, now_clus);
            }
        }
    } else {
        // now in normal subdirectory (non-root)
        int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;
        int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
        // assume bytes of root directory is a multiple of bytes per sector
        int root_sectors = root_bytes / header->BPB_BytesPerSec;
        int data_head_sec = 1 + FAT_sectors + root_sectors;

        int now_clus_num = dir_clus_num;
        int logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
        loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
        int i = 0;
        for (;;) {
            file_entry* ent = &((file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00) break; //empty
            else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
                memcpy(ent_name, ent->DIR_Name, 11);
                if (!strcmp(ent_name, file_name)) { // the entry is found
                    result->logic_sec_num = logic_sec_num;
                    result->ent = ent;
                    return result;
                }
            }

            ++i;
            if (i >= entries_per_clus) {
                now_clus_num = getNextClusNumFromFAT(disk, now_clus_num);
                if (clusNumIsEOF(now_clus_num)) break;
                logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
                loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            }
        }
    }
    free(now_clus);
    free(result);
    return NULL;
}

// get file entry by name in specified directory, return pointer to a copy of the file entry
// return NULL when not found
// the pointer (except NULL) returned should be detroyed by `free` or a memory leak problem occurred
file_entry* getFileEntByName(const floppy* disk, WORD dir_clus_num, const char* name) {
    ent_clus* info = getFileEntWithClusInfoByName(disk, dir_clus_num, name);
    if (!info) return NULL; // not found
    file_entry* result = (file_entry*)malloc(sizeof(file_entry));
    memcpy(result, info->ent, sizeof(file_entry));
    destroyEntClusInfo(info);
    return result;
}

// get file entry with cluster buffer and infomation, return NULL when not found
// the pointer returned (except NULL) should be destroyed by `destroyEntClusInfo`
ent_clus* getFileEntWithClusInfoByPath(const floppy* disk, WORD dir_clus_num, const char* path) {
    int start = 0, end = 0;
    int len = strlen(path);
    if (path[0] == '/') {
        // absolute path
        dir_clus_num = 0;
        ++start;
        ++end;
        if (start == len) { // path is only "/", root has no entry so we should build one
            file_entry* ent = (file_entry*)malloc(sizeof(file_entry));
            ent->DIR_Attr = FILE_ATTR_DIR;
            ent->DIR_FstClus = 0;
            ent_clus* result = (ent_clus*)malloc(sizeof(ent_clus));
            result->clus_buf = (BYTE*)ent;
            result->logic_sec_num = -1;
            result->sec_per_clus = 0;
            result->ent = ent;
            return result;
        }
    }
    char buffer[256];
    while (end < len) {
        if (path[start] == '/') {
            // probably "//" exists in path which is illegal
            return NULL;
        }
        if (path[end] == '/') {
            int this_len = end - start;
            if (this_len > 255) return NULL; // prevent buffer out-of-bounds access
            memcpy(buffer, path + start, this_len);
            buffer[this_len] = '\0';
            ent_clus* info = getFileEntWithClusInfoByName(disk, dir_clus_num, buffer);
            if (!info) return NULL; // not found
            else if (!(info->ent->DIR_Attr & FILE_ATTR_DIR)) {
                // a file path should not be ended with '/', so it's a illegal path
                destroyEntClusInfo(info);
                return NULL;
            }
            dir_clus_num = info->ent->DIR_FstClus;
            start = end + 1;
            // decide either return the info or free it
            if (start == len) return info;
            else destroyEntClusInfo(info);
        }
        ++end;
    }
    // if execute here, start must be less than len
    int this_len = len - start;
    if (this_len > 255) return NULL; // prevent buffer out-of-bounds access
    memcpy(buffer, path + start, this_len);
    buffer[this_len] = '\0';
    ent_clus* info = getFileEntWithClusInfoByName(disk, dir_clus_num, buffer);
    //if (!info) return NULL; // not found
    return info;
}

// get file entry by path, return pointer to a copy of the file entry, return NULL when not found
// the pointer (except NULL) returned should be detroyed by `free` or a memory leak problem occurred
file_entry* getFileEntByPath(const floppy* disk, WORD dir_clus_num, const char* path) {
    ent_clus* info = getFileEntWithClusInfoByPath(disk, dir_clus_num, path);
    if (!info) return NULL; // not found
    file_entry* ent = (file_entry*)malloc(sizeof(file_entry));
    memcpy(ent, info->ent, sizeof(file_entry));
    destroyEntClusInfo(info);
    return ent;
}

// simplify a absolute direcotry path stirng
void simplifyAbsolutePathString(char* path) {
    if (path[0] != '/') {
        printf("fatal: (simplifyAbsolutePathString)Invalid absolute string\n");
        exit(1);
    }
    int len = strlen(path);
    int now_begin = 1;
    int now_end = 1;
    int now_used = 0;
    while (now_end <= len) {
        if ( path[now_end] == '/' || (now_end == len && now_begin != len) ) {
            if (now_end - now_begin == 1 && path[now_begin] == '.') {
                // ignore
            } else if (now_end - now_begin == 2 && 
                    path[now_begin] == '.' && path[now_begin + 1] == '.') {
                // pop the directory
                --now_used;
                for (; path[now_used] != '/'; --now_used);
            } else {
                // must use memmove but not memcpy
                ++now_used;
                memmove(path + now_used, path + now_begin, now_end - now_begin);
                now_used += now_end - now_begin;
                path[now_used] = '/';
            }
            now_begin = now_end + 1;
        }
        ++now_end;
    }
    if (now_used == 0) { // consider "/"
        ++now_used;
    }
    path[now_used] = '\0';
}

// read file content to buffer, return number of cluters loaded
// return 0 is the file size doesn't match FAT record
int readFileContentByEnt(const floppy* disk, const file_entry* ent, BYTE* buf) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;
    int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
    // assume bytes of root directory is a multiple of bytes per sector
    int root_sectors = root_bytes / header->BPB_BytesPerSec;
    int data_head_sec = 1 + FAT_sectors + root_sectors;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;

    WORD cur_clus_num = ent->DIR_FstClus;
    BYTE* cur_clus = (BYTE*)malloc(bytes_per_clus);
    int counter = 0;
    while (1) {
        int logic_sec_num = data_head_sec + (cur_clus_num - 2) * sec_per_clus;
        loadSectors(disk, logic_sec_num, sec_per_clus, cur_clus);
        ++counter;

        cur_clus_num = getNextClusNumFromFAT(disk, cur_clus_num);
        if (clusNumIsEOF(cur_clus_num)) {
            int rest_size = ent->DIR_FileSize % bytes_per_clus;
            if (rest_size == 0) rest_size = bytes_per_clus;
            memcpy(buf + (counter-1) * bytes_per_clus, cur_clus, rest_size);
            break;
        }
        memcpy(buf + (counter-1) * bytes_per_clus, cur_clus, bytes_per_clus);
    }
    free(cur_clus);
    // test if file size matches FAT record
    if (counter != (ent->DIR_FileSize + bytes_per_clus-1)/bytes_per_clus) return 0;
    return counter;
}

// alloc `count` number of data clusters in FAT record
// if the `pre` parameter is not 0, FAT[pre] would be changed to the first allocated cluster
// no matter `pre` is 0 or not, return the number of the first allocated cluster
// if allocating failed, return 0
WORD allocFATClus(floppy* disk, unsigned int count, WORD pre_clus) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    int num_FATs = header->BPB_NumFATs;
    int secs_per_FAT = header->BPB_FATSz16;
    int bytes_per_FAT = secs_per_FAT * header->BPB_BytesPerSec;

    BYTE* FAT1 = (BYTE*)malloc(bytes_per_FAT); // assume FAT1 is not broken
    // FAT1 is started at the second sector (1 if count from 0) just after MBR sector
    loadSectors(disk, 1, secs_per_FAT, FAT1);

    WORD max_clusters = (header->BPB_FATSz16 * header->BPB_BytesPerSec) * 2 / 3; // divided by 1.5
    WORD head_clus = 0;
    int head_clus_is_set = 0;
    int allocated = 0; // number of allocated clusters
    for (WORD i = 2; i < max_clusters; ++i) {
        if (readFATAtPosition(FAT1, i) == NOT_USED_CLUSTER_NUM) {
            if (pre_clus) {
                writeFATAtPosition(FAT1, pre_clus, i);
            }
            if (!head_clus_is_set) { // set the head_clus to return
                head_clus = i;
                head_clus_is_set = 1;
            }
            writeFATAtPosition(FAT1, i, EOF_CLUSTER_NUM);
            pre_clus = i;
            ++allocated;
            if (allocated >= count) break;
        }
    }
    if (allocated < count) { // space of disk not enough
        head_clus = 0;
    } else {
        // write back, all FAT (usually FAT1 and FAT2) should be written
        for (int i = 0; i < num_FATs; ++i) {
            writeSectors(disk, 1 + secs_per_FAT * i, secs_per_FAT, FAT1);
        }

        // clean up the clusters
        int FAT_sectors = num_FATs * secs_per_FAT;
        int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
        // assume bytes of root directory is a multiple of bytes per sector
        int root_sectors = root_bytes / header->BPB_BytesPerSec;
        int data_head_sec = 1 + FAT_sectors + root_sectors;
        int sec_per_clus = header->BPB_SecPerClus;
        int bytes_per_clus = header->BPB_BytesPerSec * sec_per_clus;

        BYTE* buffer = (BYTE*)malloc(bytes_per_clus);
        memset(buffer, 0, bytes_per_clus); // set all clusters to all 0
        WORD cur_clus_num = head_clus;
        while (!clusNumIsEOF(cur_clus_num)) {
            int logic_sec_num = data_head_sec + (cur_clus_num - 2) * sec_per_clus;
            writeSectors(disk, logic_sec_num, sec_per_clus, buffer);
            cur_clus_num = readFATAtPosition(FAT1, cur_clus_num);
        }
        free(buffer);
    }
    free(FAT1);
    return head_clus;
}

void freeFATClus(floppy* disk, WORD head_clus_num) {
    const fat12_header* const header = (const fat12_header* const)disk->storage;
    int num_FATs = header->BPB_NumFATs;
    int secs_per_FAT = header->BPB_FATSz16;
    int bytes_per_FAT = secs_per_FAT * header->BPB_BytesPerSec;

    BYTE* FAT1 = (BYTE*)malloc(bytes_per_FAT); // assume FAT1 is not broken
    // FAT1 is started at the second sector (1 if count from 0) just after MBR sector
    loadSectors(disk, 1, secs_per_FAT, FAT1);

    WORD now_clus_num = head_clus_num;
    while (!clusNumIsEOF(now_clus_num)) {
        WORD next_clus_num = readFATAtPosition(FAT1, now_clus_num);
        writeFATAtPosition(FAT1, now_clus_num, NOT_USED_CLUSTER_NUM);
        now_clus_num = next_clus_num;
    }
    // write back, all FAT (usually FAT1 and FAT2) should be written
    for (int i = 0; i < num_FATs; ++i) {
        writeSectors(disk, 1 + secs_per_FAT * i, secs_per_FAT, FAT1);
    }
    free(FAT1);
}

// append the entry in specific directory. Return 1 when succeed, else return 0
// whoever use this function has the duty to ensure the entry is legal
int appendEntInDir(floppy* disk, WORD dir_clus_num, const file_entry* ent_to_append) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;
    // assume bytes_per_clus is a multiple of sizeof(file_entry)
    int entries_per_clus = bytes_per_clus / sizeof(file_entry);

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;

    BYTE* now_clus = (BYTE*)malloc(bytes_per_clus); // buffer for loading cluster
    if (dir_clus_num == 0) {
        // append in root directory
        int root_head_sec = 1 + FAT_sectors;
        int max_root_entries = header->BPB_RootEntCnt;

        loadSectors(disk, root_head_sec, sec_per_clus, now_clus);
        int total = 0, i = 0;
        while (total < max_root_entries) {
            file_entry* ent = &((file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00 || *(const BYTE*)ent == FILE_DEL_BYTE) {
                // this position is empty or deleted
                memcpy(ent, ent_to_append, sizeof(file_entry));
                int offset = (total/entries_per_clus) * sec_per_clus;
                writeSectors(disk, root_head_sec + offset, sec_per_clus, now_clus);
                free(now_clus);
                return 1;
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
        // append in normal subdirectory (non-root)
        int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
        // assume bytes of root directory is a multiple of bytes per sector
        int root_sectors = root_bytes / header->BPB_BytesPerSec;
        int data_head_sec = 1 + FAT_sectors + root_sectors;

        int now_clus_num = dir_clus_num;
        int logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
        loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
        int i = 0;
        while (1) {
            file_entry* ent = &((file_entry*)now_clus)[i];

            if (*(const BYTE*)ent == 0x00 || *(const BYTE*)ent == FILE_DEL_BYTE) {
                // this position is empty or deleted
                memcpy(ent, ent_to_append, sizeof(file_entry));
                writeSectors(disk, logic_sec_num, sec_per_clus, now_clus);
                free(now_clus);
                return 1;
            }

            ++i;
            if (i >= entries_per_clus) {
                i = 0;
                WORD next_clus_num = getNextClusNumFromFAT(disk, now_clus_num);
                if (clusNumIsEOF(next_clus_num)) {
                    // We need a new cluster to store the new entry
                    WORD alloc_clus_num = allocFATClus(disk, 1, now_clus_num);
                    logic_sec_num = data_head_sec + (alloc_clus_num - 2) * sec_per_clus;
                    memset(now_clus, 0, bytes_per_clus);
                    ((file_entry*)now_clus)[0] = *ent_to_append;
                    writeSectors(disk, logic_sec_num, sec_per_clus, now_clus);
                    free(now_clus);
                    return 1;
                }
                now_clus_num = next_clus_num;
                logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
                loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            }
        }
    }
    // if excutes here, probably there are too many entries in root
    free(now_clus);
    return 0;
}

// write file content in buffer to disk according to file entry, return number of clusters written
// assume the file entry has already been set with correct head cluster and file size
// return 0 if file size doesn't match FAT record, but content written would not be recover
int writeFileContentByEnt(floppy* disk, const file_entry* ent, const BYTE* buf) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;
    int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
    // assume bytes of root directory is a multiple of bytes per sector
    int root_sectors = root_bytes / header->BPB_BytesPerSec;
    int data_head_sec = 1 + FAT_sectors + root_sectors;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;

    WORD cur_clus_num = ent->DIR_FstClus;
    const BYTE* cur_clus = buf;
    int counter = 0;
    while(1) {
        int logic_sec_num = data_head_sec + (cur_clus_num - 2) * sec_per_clus;

        cur_clus_num = getNextClusNumFromFAT(disk, cur_clus_num);
        ++counter;
        if (clusNumIsEOF(cur_clus_num)) {
            // fill the rest cluster with 0 to ensure the length is enough
            BYTE* tmp = (BYTE*)malloc(bytes_per_clus);
            memset(tmp, 0, bytes_per_clus);
            int rest_size = ent->DIR_FileSize % bytes_per_clus;
            if (rest_size == 0) rest_size = bytes_per_clus;
            memcpy(tmp, cur_clus, rest_size);
            writeSectors(disk, logic_sec_num, 1, tmp);
            free(tmp);
            break;
        }
        writeSectors(disk, logic_sec_num, sec_per_clus, cur_clus);
        cur_clus = cur_clus + bytes_per_clus;
    }
    // test if file size matches FAT record
    if (counter != (ent->DIR_FileSize + bytes_per_clus-1)/bytes_per_clus) return 0;
    return counter;
}

// judge if dir A is parent of dir B
int isParent(const floppy* disk, WORD A_clus_num, WORD B_clus_num) {
    if (A_clus_num == 0) return 1; // root must be parent of any directory
    while (B_clus_num != 0) {
        if (A_clus_num == B_clus_num) return 1;
        file_entry* parent = getFileEntByName(disk, B_clus_num, "..");
        B_clus_num = parent->DIR_FstClus;
        free(parent);
    }
    return 0;
}

// remove all file (include directory, recursively) in directory
// this function is not applicable to root
void removeAllInDir(floppy* disk, WORD dir_clus_num) {
    const fat12_header* header = (const fat12_header*)disk->storage;

    int sec_per_clus = header->BPB_SecPerClus;
    int bytes_per_clus = sec_per_clus * header->BPB_BytesPerSec;
    // assume bytes_per_clus is a multiple of sizeof(file_entry)
    int entries_per_clus = bytes_per_clus / sizeof(file_entry);

    int FAT_sectors = header->BPB_NumFATs * header->BPB_FATSz16;

    int root_bytes = header->BPB_RootEntCnt * sizeof(file_entry);
    // assume bytes of root directory is a multiple of bytes per sector
    int root_sectors = root_bytes / header->BPB_BytesPerSec;
    int data_head_sec = 1 + FAT_sectors + root_sectors;

    BYTE* now_clus = (BYTE*)malloc(bytes_per_clus); // buffer for loading cluster
    int now_clus_num = dir_clus_num;
    int logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
    loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
    char ent_name[12]; // buffer for entry name
    ent_name[11] = '\0';
    int i = 0;
    while (1) {
        file_entry* ent = &((file_entry*)now_clus)[i];

        if (*(const BYTE*)ent == 0x00) {
            writeSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            break;
        } else if (*(const BYTE*)ent != FILE_DEL_BYTE) {
            memcpy(ent_name, ent->DIR_Name, 11);
            if (strcmp(ent_name, ".          ") && strcmp(ent_name, "..         ")) {
                // recursively delete
                if (ent->DIR_Attr & FILE_ATTR_DIR) removeAllInDir(disk, ent->DIR_FstClus);
                freeFATClus(disk, ent->DIR_FstClus);
            }
            *(BYTE*)ent = FILE_DEL_BYTE;
        }

        ++i;
        if (i >= entries_per_clus) {
            i = 0;
            writeSectors(disk, logic_sec_num, sec_per_clus, now_clus);
            now_clus_num = getNextClusNumFromFAT(disk, now_clus_num);
            if (clusNumIsEOF(now_clus_num)) break;
            logic_sec_num = data_head_sec + (now_clus_num - 2) * sec_per_clus;
            loadSectors(disk, logic_sec_num, sec_per_clus, now_clus);
        }
    }
    free(now_clus);
}

// return 1 when succeed else return 0
// for convenience this is a completement with low efficiency
int copyDirInternalRecursion(floppy* disk,
    const directory* dir,
    const char* src,
    int src_len,
    const char* des,
    int des_len,
    const ent_tree* tree) {
    char* src_buf = (char*)malloc(src_len + 14);
    char* des_buf = (char*)malloc(des_len + 14);
    memcpy(src_buf, src, src_len);
    src_buf[src_len] = '/';
    ++src_len;
    char* src_next = src_buf + src_len;
    memcpy(des_buf, des, des_len);
    des_buf[des_len] = '/';
    ++des_len;
    char* des_next = des_buf + des_len;
    for (size_t i = 0; i < tree->size; ++i) {
        formatNameToNormal(tree->storage[i].ent.DIR_Name, src_next);
        int len = strlen(src_next);
        memcpy(des_next, src_next, len + 1);
        if (tree->storage[i].sub_tree) { // directory
            if (!makeDirByPath(disk, dir, des_buf)) {
                free(src_buf);
                free(des_buf);
                return 0;
            }
            if (!copyDirInternalRecursion(disk, dir, 
                    src_buf, src_len + len, 
                    des_buf, des_len + len, 
                    (ent_tree*)tree->storage[i].sub_tree)
            ) {
                removeDirByPath(disk, dir, des_buf);
                free(src_buf);
                free(des_buf);
                return 0;
            }
        } else { // file
            if (!copyFileByPath(disk, dir, src_buf, des_buf)) {
                free(src_buf);
                free(des_buf);
                return 0;
            }
        }
    }
    free(src_buf);
    free(des_buf);
    return 1;
}