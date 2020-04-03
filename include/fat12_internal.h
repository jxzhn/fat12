# ifndef FAT12_INTERNAL_H_
# define FAT12_INTERNAL_H_

# include "fat12.h"

# define EOF_CLUSTER_NUM 0xFFF
# define NOT_USED_CLUSTER_NUM 0x000

typedef struct fat12_header {
    BYTE    JmpCode[3];
    BYTE    BS_OEMName[8];
    WORD    BPB_BytesPerSec;
    BYTE    BPB_SecPerClus;
    WORD    BPB_RsvdSecCnt;
    BYTE    BPB_NumFATs;
    WORD    BPB_RootEntCnt;
    WORD    BPB_TotSec16;
    BYTE    BPB_Media;
    WORD    BPB_FATSz16;
    WORD    BPB_SecPerTrk;
    WORD    BPB_NumHeads;
    DWORD   BPB_HiddSec;
    DWORD   BPB_TotSec32;
    BYTE    BS_DrvNum;
    BYTE    BS_Reserved1;
    BYTE    BS_BootSig;
    DWORD   BS_VolID;
    BYTE    BS_VolLab[11];
    BYTE    BS_FileSysType[8];
}__attribute__((packed)) fat12_header;

typedef struct file_entry {
    BYTE    DIR_Name[11];
    BYTE    DIR_Attr;
    BYTE    Reserve[10];
    WORD    DIR_WrtTime;
    WORD    DIR_WrtDate;
    WORD    DIR_FstClus;
    DWORD   DIR_FileSize;
}__attribute__((packed)) file_entry;

# define FILE_DEL_BYTE 0xE5
# define FILE_ATTR_RO 0x01
# define FILE_ATTR_HIDDEN 0x02
# define FILE_ATTR_SYSTEM 0x04
# define FILE_ATTR_VOLLAB 0x08
# define FILE_ATTR_DIR 0x10
# define FILE_ATTR_ARCH 0x20

// to emulate the real way using BIOS
void loadSectors(const floppy* disk, WORD logic_sec_num, WORD count, BYTE* buf);

void writeSectors(floppy* disk, WORD logic_sec_num, WORD count, const BYTE* buf);

// read the number at specific position of FAT12 record
WORD readFATAtPosition(const BYTE* FAT, WORD pos);

// write the number to specific position of FAT12 record
void writeFATAtPosition(BYTE* FAT, WORD pos, WORD num);

WORD getNextClusNumFromFAT(const floppy* disk, WORD clus_num);

# define clusNumIsBadClus(clus_num) \
    (0x0FF0 <= clus_num && clus_num <= 0x0FF7)

# define clusNumIsEOF(clus_num) \
    (0x0FF8 <= clus_num && clus_num <= 0x0FFF)

void getWrtTimeFromFileEnt(
    const file_entry* ent, 
    int* year, int* month, int* date,
    int* hour, int* minute, int* second);

void setWrtTime(const struct tm* time, WORD* WrtTime, WORD* WrtDate);

void printFileEnt(const file_entry* ent);

// ----------- a simple completement of C++ vector -----------

typedef struct file_vector {
    file_entry* storage;
    int max_size;
    int size;
} file_vector;

void fileVectorInit(file_vector* p);

void fileVectorAppend(file_vector* p, const file_entry* ent);

void fileVectorDestroy(file_vector* p);

// ----------- ----------------------------------- -----------

// use to sort entries
int fileEntCmp(const void* x, const void* y);

void formatNameToNormal(const BYTE* FAT_name, char* buffer);

// return 1 in case success, else return 0
void formatNameToFATType(const char* name, BYTE* buffer);

// ----------- a simple completement of tree -----------

typedef struct ent_tree_node {
    file_entry ent;
    void* sub_tree;
} ent_tree_node;

typedef struct ent_tree {
    ent_tree_node* storage;
    size_t max_size;
    size_t size;
} ent_tree;

void entTreeInit(ent_tree* p);

void entTreeAppend(ent_tree* p, const file_entry* ent, void* sub_tree);

// use to sort
int entTreeNodeCmp(const void* x, const void* y);

void entTreeDestroy(ent_tree* p);

// ----------- ----------------------------- -----------

// the pointer returned by this function should be destroyed by function `entTreeDestroy`
ent_tree* getEntTree(const floppy* disk, WORD dir_clus_num);

void printEntTree(const ent_tree* p, const char* indent, int indent_len);

// this is used for return search result in `getFileEntWithClusInfo`
typedef struct ent_clus {
    BYTE* clus_buf;
    WORD logic_sec_num; // head logic sector number of cluster
    int sec_per_clus;
    file_entry* ent; // this pointer points to a specific position of `clus_buf`
} ent_clus;

void destroyEntClusInfo(ent_clus* p);

// get file entry with cluster buffer and infomation, return NULL when not found
// the pointer returned (except NULL) should be destroyed by `destroyEntClusInfo`
ent_clus* getFileEntWithClusInfoByName(const floppy* disk, WORD dir_clus_num, const char* name);

// get file entry by name in specified directory, return pointer to a copy of the file entry
// return NULL when not found
// the pointer (except NULL) returned should be detroyed by `free` or a memory leak problem occurred
file_entry* getFileEntByName(const floppy* disk, WORD dir_clus_num, const char* name);

// get file entry with cluster buffer and infomation, return NULL when not found
// the pointer returned (except NULL) should be destroyed by `destroyEntClusInfo`
ent_clus* getFileEntWithClusInfoByPath(const floppy* disk, WORD dir_clus_num, const char* path);

// get file entry by path, return pointer to a copy of the file entry, return NULL when not found
// the pointer (except NULL) returned should be detroyed by `free` or a memory leak problem occurred
file_entry* getFileEntByPath(const floppy* disk, WORD dir_clus_num, const char* path);

// simplify a absolute direcotry path stirng
void simplifyAbsolutePathString(char* path);

// read file content to buffer, return number of cluters loaded
// return 0 is the file size doesn't match FAT record
int readFileContentByEnt(const floppy* disk, const file_entry* ent, BYTE* buf);

// alloc `count` number of data clusters in FAT record
// if the `pre` parameter is not 0, FAT[pre] would be changed to the first allocated cluster
// no matter `pre` is 0 or not, return the number of the first allocated cluster
// if allocating failed, return 0
WORD allocFATClus(floppy* disk, unsigned int count, WORD pre_clus);

void freeFATClus(floppy* disk, WORD head_clus_num);

// append the entry in specific directory. Return 1 when succeed, else return 0
// whoever use this function has the duty to ensure the entry is legal
int appendEntInDir(floppy* disk, WORD dir_clus_num, const file_entry* ent_to_append);

// write file content in buffer to disk according to file entry, return number of clusters written
// assume the file entry has already been set with correct head cluster and file size
// return 0 if file size doesn't match FAT record, but content written would not be recover
int writeFileContentByEnt(floppy* disk, const file_entry* ent, const BYTE* buf);

// judge if dir A is parent of dir B
int isParent(const floppy* disk, WORD A_clus_num, WORD B_clus_num);

// remove all file (include directory, recursively) in directory
// this function is not applicable to root
void removeAllInDir(floppy* disk, WORD dir_clus_num);

// return 1 when succeed else return 0
// for convenience this is a completement with low efficiency
int copyDirInternalRecursion(floppy* disk,
    const directory* dir,
    const char* src,
    int src_len,
    const char* des,
    int des_len,
    const ent_tree* tree);

# endif