#ifndef NEODECK_ABI_H
#define NEODECK_ABI_H

/*
 * Kernel <-> app ABI. Included by BOTH the kernel and the SDK, so syscall
 * numbers and on-disk constants cannot drift between the two sides.
 *
 * Syscall convention: `int 0x80`, EAX = number, EBX/ECX = arguments,
 * result (if any) comes back in EAX.
 *
 * App entry convention: the first byte of the binary is
 *     int entry(int argc, char** argv)        (cdecl)
 * Returning from it is the same as calling sys_exit(return value).
 */

#define NEODECK_SYS_PRINT              1
#define NEODECK_SYS_CLEAR              2
#define NEODECK_SYS_GET_KEY            3   /* blocks until a key arrives */
#define NEODECK_SYS_READ_FILE          4   /* EBX=path, ECX=buffer, EDX=buffer size; returns bytes read (NUL-terminated) */
#define NEODECK_SYS_GET_DIR_ENTRIES    5   /* EBX=neodeck_dirent[], ECX=max entries; returns count, -1 on error */
#define NEODECK_SYS_MKDIR              6
#define NEODECK_SYS_CD                 7
#define NEODECK_SYS_TOUCH              8
#define NEODECK_SYS_WRITE_FILE         9
#define NEODECK_SYS_CAT               10
#define NEODECK_SYS_GET_CWD           11   /* EBX=buffer, ECX=size; returns string length */
#define NEODECK_SYS_FORMAT            12
#define NEODECK_SYS_EXIT              13   /* EBX = exit status, never returns */
#define NEODECK_SYS_SLEEP             14   /* EBX = milliseconds */
#define NEODECK_SYS_UPTIME_MS         15   /* returns milliseconds since boot */
#define NEODECK_SYS_REMOVE            16   /* EBX=path, deletes a file; 0 = ok */
#define NEODECK_SYS_RMDIR             17   /* EBX=path, deletes an empty directory; 0 = ok */
#define NEODECK_SYS_STAT              18   /* EBX=path, ECX=neodeck_stat*; 0 = ok, negative = error */

/* Filesystem: FAT32. Names are returned as plain ASCII (long names are cut at 127 chars). */
#define NEODECK_NAME_MAX  127

struct neodeck_dirent {
    char          name[NEODECK_NAME_MAX + 1];
    unsigned int  size;          /* bytes, 0 for directories */
    unsigned char is_dir;
    unsigned char reserved[3];
};

struct neodeck_stat {
    unsigned int  size;
    unsigned char is_dir;
    unsigned char reserved[3];
};

static_assert(sizeof(neodeck_dirent) == 136, "neodeck_dirent layout is part of the app ABI");
static_assert(sizeof(neodeck_stat) == 8, "neodeck_stat layout is part of the app ABI");

#endif
