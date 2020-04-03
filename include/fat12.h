# ifndef FAT12_H_
# define FAT12_H_

# define BYTE    unsigned char
# define WORD    unsigned short
# define DWORD   unsigned int

# define BOOT_START_ADDR 0x7c00

// 1.44MB = 2880 x 512B
# define FLOPPY_SIZE 1474560

typedef struct floppy {
    BYTE    storage[FLOPPY_SIZE];
} floppy;

typedef struct directory {
    // head cluster number of the directory. Use 0 to represent root.
    WORD    clus_num;
    char*   path_str;
    size_t  max_path_len;
} directory;

// return 1 when success, else return 0
int readFloppyDisk(const char* file_name, floppy* disk);

// return 1 when success, else return 0
int writeFloppyDisk(const char* file_name, const floppy* disk);

// return 1 if the floppy image is bootable, else return 0
int verifyBootId(const floppy* disk);

void printFat12Info(const floppy* p);

void initDirWithRoot(directory* dir);

void printAllInDir(const floppy* disk, const directory* dir);

void printDirTree(const floppy* disk, const directory* dir);

// return 1 when directory is changed successfully, else return 0
int changeDirectory(const floppy* disk, directory* dir, const char* path);

// return 1 in case success, else return 0
int printFileContentByPath(const floppy* disk, const directory* dir, const char* path);

// copy file using path relative to directory, return 1 when succeed else return 0
int copyFileByPath(floppy* disk, const directory* dir, const char* src, const char* des);

// return 1 when succeed, else return 0
int removeFileByPath(floppy* disk, const directory* dir, const char* path);

// move file or dir using path relative to directory, return 1 when succeed else return 0
int moveFileByPath(floppy* disk, const directory* dir, const char* src, const char* des);

// return 1 when succeed else return 0
int makeDirByPath(floppy* disk, const directory* dir, const char* path);

// remove a directory (and everything in it). Return 1 when succeed else return 0
int removeDirByPath(floppy* disk, const directory* dir, const char* path);

// concat content of two files to one new file, return 1 when succeed else return 0
int concatFileByPath(floppy* disk, const directory* dir, 
    const char* src1,
    const char* src2,
    const char* des) ;

// return 1 when succeed else return 0
// for convenience this is a completement with low efficiency
int copyDirByPath(floppy* disk, const directory* dir, const char* src, const char* des);

// free memory allocated in `initDirWithRoot`
void destroyDir(directory* dir);

# endif