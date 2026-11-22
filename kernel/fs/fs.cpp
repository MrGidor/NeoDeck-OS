#include "../include/types.h"
#include "neodeck_abi.h"
#include "../drivers/terminal.h"
#include "../drivers/timer.h"
#include "../drivers/ata.h"
#include "../shell/string.h"
#include "fat32.h"
#include "fs.h"

static const char* fs_strerror(int code) {
    switch (code) {
        case FAT_E_NOFS:     return "no FAT32 filesystem (run 'format')";
        case FAT_E_NOTFOUND: return "not found";
        case FAT_E_EXISTS:   return "already exists";
        case FAT_E_NOTDIR:   return "not a directory";
        case FAT_E_ISDIR:    return "is a directory";
        case FAT_E_NAME:     return "name must be 8.3 (max 8 + 3 chars, all lower or all upper case)";
        case FAT_E_FULL:     return "disk full";
        case FAT_E_NOTEMPTY: return "directory not empty";
        case FAT_E_TOOLONG:  return "path or name too long";
        case FAT_E_CORRUPT:  return "filesystem structure is damaged";
        case FAT_E_INVAL:    return "invalid argument";
        case FAT_E_BUSY:     return "it is the current directory";
        case FAT_E_NODISK:   return "no usable ATA disk found";
        default:             return "unknown error";
    }
}

static void report(int code, const char* what, const char* name) {
    kprint_error("FAT32 Error: ");
    kprint_error(what);
    if (name) { kprint_error(" '"); kprint_error(name); kprint_error("'"); }
    kprint_error(": ");
    kprint_error(fs_strerror(code));
    kprint_error("\n");
}

void fs_init() {
    int r = fat32_mount();
    if (r == FAT_OK) {
        kprint_log("FAT32 volume mounted.\n");
    } else {
        kprint_log("No FAT32 volume found on the disk. Type 'format' to create one.\n", false);
    }
}

void fs_format() {
    int r = fat32_format("NEODECK", timer_ticks() ^ 0x4E454F44u);
    if (r != FAT_OK) { report(r, "format failed", 0); return; }
    kprint_success("FAT32 filesystem created and mounted.\n");
}

const char* fs_cwd() { return fat32_cwd(); }

void fs_df() {
    if (!fat32_is_mounted()) { report(FAT_E_NOFS, "df", 0); return; }
    uint32_t cb = fat32_cluster_bytes();
    uint32_t total = fat32_total_clusters();
    uint32_t freec = fat32_free_clusters();
    kprint_info("FAT32 volume usage:\n");
    kprint("  Cluster size: "); kprint_dec(cb); kprint(" bytes\n");
    kprint("  Total:        "); kprint_dec((total * (cb / 512)) / 2); kprint(" KB\n");
    kprint("  Used:         "); kprint_dec(((total - freec) * (cb / 512)) / 2); kprint(" KB\n");
    kprint("  Free:         "); kprint_dec((freec * (cb / 512)) / 2); kprint(" KB\n");
}

struct ls_ctx { int count; };
static bool ls_cb(const fat32_dirent* e, void* c) {
    ls_ctx* ctx = (ls_ctx*)c;
    ctx->count++;
    kprint((e->attr & FAT_ATTR_DIR) ? "  [DIR]  " : "  [FILE] ");
    kprint(e->name);
    if (!(e->attr & FAT_ATTR_DIR)) {
        kprint("  ("); kprint_dec(e->size); kprint(" bytes)");
    }
    kprint("\n");
    return true;
}

void fs_ls(const char* path) {
    ls_ctx ctx; ctx.count = 0;
    kprint("Directory listing:\n");
    int r = fat32_list_dir(path, ls_cb, &ctx);
    if (r != FAT_OK) { report(r, "ls", path); return; }
    if (ctx.count == 0) kprint("  (empty directory)\n");
}

void fs_cd(const char* path) {
    int r = fat32_chdir(path);
    if (r != FAT_OK) report(r, "cd", path);
}

void fs_mkdir(const char* path) {
    int r = fat32_mkdir(path);
    if (r != FAT_OK) { report(r, "mkdir", path); return; }
    kprint("Directory '"); kprint(path); kprint("' created.\n");
}

void fs_touch(const char* path) {
    int r = fat32_create(path);
    if (r == FAT_E_EXISTS) return;                 /* touching an existing file is fine */
    if (r != FAT_OK) { report(r, "touch", path); return; }
    kprint("File '"); kprint(path); kprint("' created.\n");
}

bool fs_prepare_file(const char* path) {
    int r = fat32_create(path);
    if (r == FAT_OK || r == FAT_E_EXISTS) {
        if (r == FAT_E_EXISTS) {
            fat32_dirent e;
            if (fat32_stat(path, &e) == FAT_OK && (e.attr & FAT_ATTR_DIR)) { report(FAT_E_ISDIR, "edit", path); return false; }
        }
        return true;
    }
    report(r, "edit", path);
    return false;
}

void fs_write(const char* path, const char* text) {
    uint32_t len = 0;
    while (text[len]) len++;
    if (len == 0) kprint_warning("FAT32 Warning: empty text payload. Writing blank file.\n");
    int r = fat32_write_file(path, text, len);
    if (r != FAT_OK) { report(r, "write", path); return; }
    kprint_success("Wrote ");
    kprint_dec(len);
    kprint_success(" bytes.\n");
}

static void cat_cb(const uint8_t* data, uint32_t len, void*) {
    for (uint32_t i = 0; i < len; i++) terminal_put_char((char)data[i]);
}

void fs_cat(const char* path) {
    fat32_dirent e;
    int r = fat32_stat(path, &e);
    if (r != FAT_OK) { report(r, "cat", path); return; }
    if (e.attr & FAT_ATTR_DIR) { report(FAT_E_ISDIR, "cat", path); return; }
    if (e.size == 0) { kprint_info("(Empty File)\n"); return; }
    r = fat32_stream_file(path, cat_cb, 0);
    if (r != FAT_OK) { report(r, "cat", path); return; }
    kprint("\n");
}

void fs_rm(const char* path) {
    int r = fat32_remove(path);
    if (r != FAT_OK) { report(r, "rm", path); return; }
    kprint("File '"); kprint(path); kprint("' removed successfully.\n");
}

void fs_rmdir(const char* path) {
    int r = fat32_rmdir(path);
    if (r != FAT_OK) { report(r, "rmdir", path); return; }
    kprint("Directory '"); kprint(path); kprint("' removed.\n");
}

uint32_t fs_file_size(const char* path) {
    fat32_dirent e;
    if (fat32_stat(path, &e) != FAT_OK) return 0;
    if (e.attr & FAT_ATTR_DIR) return 0;
    return e.size;
}

uint32_t fs_read_raw(const char* path, void* out, uint32_t max) {
    uint32_t n = 0;
    if (fat32_read_file(path, out, max, &n) != FAT_OK) return 0;
    return n;
}

uint32_t fs_read_to_buffer(const char* path, char* out, uint32_t max_size) {
    if (max_size == 0) return 0;
    uint32_t n = 0;
    int r = fat32_read_file(path, out, max_size - 1, &n);
    if (r != FAT_OK) n = 0;
    out[n] = '\0';
    return n;
}

struct dirent_ctx { neodeck_dirent* out; uint32_t max; uint32_t count; };
static bool dirent_cb(const fat32_dirent* e, void* c) {
    dirent_ctx* ctx = (dirent_ctx*)c;
    if (ctx->count >= ctx->max) return false;
    neodeck_dirent* d = &ctx->out[ctx->count++];
    int i = 0;
    for (; i < NEODECK_NAME_MAX && e->name[i]; i++) d->name[i] = e->name[i];
    for (; i <= NEODECK_NAME_MAX; i++) d->name[i] = '\0';
    d->size = e->size;
    d->is_dir = (e->attr & FAT_ATTR_DIR) ? 1 : 0;
    d->reserved[0] = d->reserved[1] = d->reserved[2] = 0;
    return true;
}

int fs_get_dir_entries(void* dest, uint32_t max_entries) {
    dirent_ctx ctx;
    ctx.out = (neodeck_dirent*)dest;
    ctx.max = max_entries;
    ctx.count = 0;
    if (fat32_list_dir(0, dirent_cb, &ctx) != FAT_OK) return -1;
    return (int)ctx.count;
}

int fs_stat(const char* path, void* dest) {
    fat32_dirent e;
    int r = fat32_stat(path, &e);
    if (r != FAT_OK) return r;
    neodeck_stat* st = (neodeck_stat*)dest;
    st->size = e.size;
    st->is_dir = (e.attr & FAT_ATTR_DIR) ? 1 : 0;
    st->reserved[0] = st->reserved[1] = st->reserved[2] = 0;
    return 0;
}

int fs_remove_quiet(const char* path) { return fat32_remove(path); }
int fs_rmdir_quiet(const char* path)  { return fat32_rmdir(path); }
