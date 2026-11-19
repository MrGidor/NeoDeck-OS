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
#define NEODECK_SYS_READ_FILE          4
#define NEODECK_SYS_GET_DIR_ENTRIES    5
#define NEODECK_SYS_MKDIR              6
#define NEODECK_SYS_CD                 7
#define NEODECK_SYS_TOUCH              8
#define NEODECK_SYS_WRITE_FILE         9
#define NEODECK_SYS_CAT               10
#define NEODECK_SYS_GET_ACTIVE_DIR    11
#define NEODECK_SYS_FORMAT            12
#define NEODECK_SYS_EXIT              13   /* EBX = exit status, never returns */
#define NEODECK_SYS_SLEEP             14   /* EBX = milliseconds */
#define NEODECK_SYS_UPTIME_MS         15   /* returns milliseconds since boot */

/* NeoFS on-disk layout shared with userspace */
#define NEOFS_INODE_TABLE_SECTOR  2
#define NEOFS_ROOT_DIR_SECTOR     3
#define NEOFS_MAX_INODES          16
#define NEOFS_MAX_DIR_ENTRIES     16
#define NEOFS_MAX_FILENAME        14
#define NEOFS_TYPE_FILE           1
#define NEOFS_TYPE_DIR            2

struct neofs_dir_entry {
    unsigned int  inode_num;
    char          name[NEOFS_MAX_FILENAME];
    unsigned char used;
} __attribute__((packed));

static_assert(sizeof(neofs_dir_entry) == 19, "neofs_dir_entry must be 19 bytes on disk");

#endif
