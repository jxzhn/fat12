# FAT12 FS Emulation

This is a FAT12 file system emulation accomplished in C language for my OS class purpose, and it can be used in the OS kernel with minor modifications.

`fat12.h` provide all interfaces that could be used by program, whose function can be easily understand by name.

`fat12_internal.h` provide internal functions that used in public interfaces, these functions should **NOT** be used in program.