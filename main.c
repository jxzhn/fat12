#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat12.h"

void printHelpInfo() {
    printf("info        -- print FAT12 header infomation of the disk.\n");
    printf("bootable    -- check if the floppy is bootable. (by verifying 0x55AA)\n");
    printf("ls          -- list all file and sub-directory in current directory.\n");
    printf("cd {path}   -- change current directory to {path}.\n");
    printf("type {file} -- print the content of {file}. (decode as ASCII)\n");
    printf("tree        -- print directory tree of current directory.\n");
    printf("cp {src} {des}-- copy from {src} file to {des} file.\n");
    printf("mv {src} {des}-- move {src} file or directory to {des} position.\n");
    printf("rm {file}   -- delete {file}.\n");
    printf("mkdir {dir} -- create a new directory {dir}.\n");
    printf("rmdir {dir} -- delete directory {dir} (include file and sub-directory in it)\n");
    printf("cpdir {src} {des}-- copy from {src} directory to {des} directory (recursive)\n");
    printf("concat {1} {2} {des}-- concat content of file {1} and {2} to {des} file.\n");
    printf("quit        -- quit and save all changed.\n");
}

int main() {
    printf("Input file name: ");
    char name[256];
    scanf("%s", name);

    floppy* disk = (floppy*)malloc(sizeof(floppy));
    if (!readFloppyDisk(name, disk)) {
        printf("Failed to read image from file.\n");
        free(disk);
        return 1;
    }

    directory dir;
    initDirWithRoot(&dir);

    char* buffer = (char*)malloc(1024);
    char* const command = buffer;
    char* const path = buffer + 256;
    char* const path2 = buffer + 256 * 2;
    char* const path3 = buffer + 256 * 3;
    int changed = 0; // if the disk is written
    printf("Input \"help\" to get help infomation.\n");
    while (1) {
        printf("[%s]$ ", dir.path_str);
        scanf("%s", command);
        if (!strcmp(command, "help")) {
            printHelpInfo();
        } else if (!strcmp(command, "bootable")) {
            if (verifyBootId(disk)) {
                printf("This image is bootable.\n");
            } else {
                printf("This image is NOT bootable.\n");
            }
        } else if (!strcmp(command, "info")) {
            printFat12Info(disk);
        } else if (!strcmp(command, "ls")) {
            printAllInDir(disk, &dir);
        } else if (!strcmp(command, "cd")) {
            scanf("%s", path);
            if (!changeDirectory(disk, &dir, path)) {
                printf("Failed to change directory into \"%s\"\n", path);
            }
        } else if (!strcmp(command, "type")) {
            scanf("%s", path);
            if (!printFileContentByPath(disk, &dir, path)) {
                printf("Failed to read content of file \"%s\"\n", path);
            }
        } else if (!strcmp(command, "tree")) {
            printDirTree(disk, &dir);
        } else if (!strcmp(command, "cp")) {
            scanf("%s %s", path, path2);
            if (!copyFileByPath(disk, &dir, path, path2)) {
                printf("Failed to copy file from \"%s\" to \"%s\"\n", path, path2);
            } else changed = 1;
        } else if (!strcmp(command, "mv")) {
            scanf("%s %s", path, path2);
            if (!moveFileByPath(disk, &dir, path, path2)) {
                printf("Failed to move file from \"%s\" to \"%s\"\n", path, path2);
            } else changed = 1;
        } else if (!strcmp(command, "rm")) {
            scanf("%s", path);
            if (!removeFileByPath(disk, &dir, path)) {
                printf("Failed to remove file \"%s\"\n", path);
            } else changed = 1;
        } else if (!strcmp(command, "mkdir")) {
            scanf("%s", path);
            if (!makeDirByPath(disk, &dir, path)) {
                printf("Failed to make directory \"%s\"\n", path);
            } else changed = 1;
        } else if (!strcmp(command, "rmdir")) {
            scanf("%s", path);
            if (!removeDirByPath(disk, &dir, path)) {
                printf("Failed to remove directory \"%s\"\n", path);
            } else changed = 1;
        } else if (!strcmp(command, "cpdir")) {
            scanf("%s %s", path, path2);
            if (!copyDirByPath(disk, &dir, path, path2)) {
                printf("Failed to copy directory \"%s\" to \"%s\"\n", path, path2);
            } else changed = 1;
        } else if (!strcmp(command, "concat")) {
            scanf("%s %s %s", path, path2, path3);
            if (!concatFileByPath(disk, &dir, path, path2, path3)) {
                printf("Failed to concat \"%s\" and \"%s\" to \"%s\"\n", path, path2, path3);
            } else changed = 1;
        } else if (!strcmp(command, "quit")) {
            break;
        } else {
            printf("Unkown command: %s\n", command);
        }
    }
    free(buffer);
    destroyDir(&dir);
    if (changed) {
        printf("Disk content has been changed.Trying to writing back...\n");
        if (!writeFloppyDisk(name, disk)) {
            printf("Failed to write the file back.\n");
        } else {
            printf("Successfully write back.\n");
        }
    }
    free(disk);
    return 0;
}