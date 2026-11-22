#ifndef VISTA_ABI_H
#define VISTA_ABI_H

/*
 * Kernel <-> app ABI. Included by BOTH the kernel and the SDK, so syscall
 * numbers and on-disk constants cannot drift between the two sides.
 *
 * Syscall convention: `int 0x80` from ring 3, EAX = number, EBX/ECX/EDX =
 * arguments, result (if any) comes back in EAX.
 *
 * App memory model: every app is linked at VISTA_APP_BASE and runs in ring 3
 * in its own address space (its private RAM is mapped there), so apps never see
 * each other's memory. The kernel rejects pointers outside that window.
 *   [ image | .bss ........ free ........ | stack (grows down) + argv ]
 *
 * App entry convention: the first byte of the binary is
 *     int entry(int argc, char** argv)        (cdecl)
 * Returning from it is the same as calling sys_exit(return value).
 * A task killed by a CPU exception exits with status 1000 + vector.
 */

#define VISTA_APP_BASE   0x40000000   /* = APP_VIRT_BASE in kernel/include/layout.h */
#define VISTA_APP_SIZE   0x80000      /* = APP_MEM_SIZE */

#define VISTA_SYS_PRINT              1
#define VISTA_SYS_CLEAR              2
#define VISTA_SYS_GET_KEY            3   /* blocks until a key arrives; returns 0 at once if the caller is not the foreground task */
#define VISTA_SYS_READ_FILE          4   /* EBX=path, ECX=buffer, EDX=buffer size; returns bytes read (NUL-terminated) */
#define VISTA_SYS_GET_DIR_ENTRIES    5   /* EBX=vista_dirent[], ECX=max entries; returns count, -1 on error */
#define VISTA_SYS_MKDIR              6
#define VISTA_SYS_CD                 7
#define VISTA_SYS_TOUCH              8
#define VISTA_SYS_WRITE_FILE         9
#define VISTA_SYS_CAT               10
#define VISTA_SYS_GET_CWD           11   /* EBX=buffer, ECX=size; returns string length */
#define VISTA_SYS_FORMAT            12
#define VISTA_SYS_EXIT              13   /* EBX = exit status, never returns */
#define VISTA_SYS_SLEEP             14   /* EBX = milliseconds */
#define VISTA_SYS_UPTIME_MS         15   /* returns milliseconds since boot */
#define VISTA_SYS_REMOVE            16   /* EBX=path, deletes a file; 0 = ok */
#define VISTA_SYS_RMDIR             17   /* EBX=path, deletes an empty directory; 0 = ok */
#define VISTA_SYS_STAT              18   /* EBX=path, ECX=vista_stat*; 0 = ok, negative = error */
#define VISTA_SYS_YIELD             19
#define VISTA_SYS_GETPID            20
#define VISTA_SYS_SPAWN             21   /* EBX="file arg1 arg2", ECX=flags; returns pid, negative = error */
#define VISTA_SYS_WAIT              22   /* EBX=pid, ECX=int* status (or 0); blocks until it exits; 0 = ok, -1 = not waitable */
#define VISTA_SYS_KILL              23   /* EBX=pid; 0 = ok */

#define VISTA_SPAWN_DETACH  1            /* nobody will sys_wait() for the child */

/* Filesystem: FAT32. Names are returned as plain ASCII (long names are cut at 127 chars). */
#define VISTA_NAME_MAX  127

struct vista_dirent {
    char          name[VISTA_NAME_MAX + 1];
    unsigned int  size;
    unsigned char is_dir;
    unsigned char reserved[3];
};

struct vista_stat {
    unsigned int  size;
    unsigned char is_dir;
    unsigned char reserved[3];
};

static_assert(sizeof(vista_dirent) == 136, "vista_dirent layout is part of the app ABI");
static_assert(sizeof(vista_stat) == 8, "vista_stat layout is part of the app ABI");

#endif
