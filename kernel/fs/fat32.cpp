#include "../include/types.h"
#include "../drivers/ata.h"
#include "fat32.h"

/*
 * FAT32 driver. Layout of a volume:
 *
 *   [reserved sectors: boot sector, FSInfo, backup boot ...][FAT #1][FAT #2][data clusters]
 *
 * Cluster N (N >= 2) lives at data_lba + (N - 2) * sectors_per_cluster.
 * Every directory (including the root) is an ordinary cluster chain made of
 * 32-byte entries; long names are stored in extra "LFN" entries that precede
 * the 8.3 entry they belong to.
 *
 * Coding note: this file avoids struct copies and big "= {0}" initialisers so
 * the compiler never emits memcpy/memset calls (the kernel has no libc).
 */

#define SECTOR_SIZE     512
#define FAT_EOC         0x0FFFFFF8u   /* >= this: end of chain */
#define FAT_EOC_MARK    0x0FFFFFFFu
#define FAT_MASK        0x0FFFFFFFu
#define FSINFO_UNKNOWN  0xFFFFFFFFu
#define FAT_DEFAULT_DATE 0x5821       /* 2024-01-01 (no RTC yet) */
#define LFN_CHARS_PER_ENTRY 13

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void mem_zero(void* dst, uint32_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}
static void mem_copy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst; const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool ci_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static uint32_t str_len(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }

static struct {
    bool     mounted;
    uint32_t part_lba;
    uint32_t total_sectors;
    uint32_t spc;
    uint32_t reserved;
    uint32_t nfats;
    uint32_t fat_size;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t fsinfo_lba;
    uint32_t free_count;
    uint32_t next_free;
    bool     fsinfo_dirty;
} vol;

static uint32_t cwd_cluster = 0;
static char     cwd_path[FAT32_PATH_MAX] = "/";

alignas(4) static uint8_t fat_buf[SECTOR_SIZE];
static uint32_t fat_buf_sector = 0xFFFFFFFFu;

bool fat32_is_mounted() { return vol.mounted; }
const char* fat32_cwd() { return cwd_path; }
uint32_t fat32_cluster_bytes() { return vol.spc * SECTOR_SIZE; }
uint32_t fat32_total_clusters() { return vol.cluster_count; }

static inline bool valid_cluster(uint32_t c) { return c >= 2 && c < vol.cluster_count + 2; }
static inline uint32_t cluster_lba(uint32_t c) { return vol.data_lba + (c - 2) * vol.spc; }

static void fat_load(uint32_t fat_sec) {
    if (fat_buf_sector != fat_sec) {
        ata_read_sector(vol.fat_lba + fat_sec, fat_buf);
        fat_buf_sector = fat_sec;
    }
}

static uint32_t fat_get(uint32_t cl) {
    fat_load(cl / 128);
    return rd32(&fat_buf[(cl % 128) * 4]) & FAT_MASK;
}

static void fat_set(uint32_t cl, uint32_t value) {
    uint32_t sec = cl / 128;
    fat_load(sec);
    uint8_t* p = &fat_buf[(cl % 128) * 4];
    wr32(p, (rd32(p) & 0xF0000000u) | (value & FAT_MASK));   /* top 4 bits are reserved */
    for (uint32_t f = 0; f < vol.nfats; f++)
        ata_write_sector(vol.fat_lba + f * vol.fat_size + sec, fat_buf);
}

static void fsinfo_flush() {
    if (!vol.fsinfo_dirty || vol.fsinfo_lba == 0) { vol.fsinfo_dirty = false; return; }
    alignas(4) uint8_t sec[SECTOR_SIZE];
    ata_read_sector(vol.fsinfo_lba, sec);
    if (rd32(&sec[0]) == 0x41615252u && rd32(&sec[484]) == 0x61417272u) {
        wr32(&sec[488], vol.free_count);
        wr32(&sec[492], vol.next_free);
        ata_write_sector(vol.fsinfo_lba, sec);
    }
    vol.fsinfo_dirty = false;
}

/* Returns a newly allocated cluster marked end-of-chain, or 0 if the disk is full. */
static uint32_t alloc_cluster() {
    uint32_t total = vol.cluster_count;
    uint32_t cl = vol.next_free;
    if (cl < 2 || cl >= total + 2) cl = 2;

    for (uint32_t n = 0; n < total; n++) {
        if (fat_get(cl) == 0) {
            fat_set(cl, FAT_EOC_MARK);
            vol.next_free = cl + 1;
            if (vol.free_count != FSINFO_UNKNOWN && vol.free_count > 0) vol.free_count--;
            vol.fsinfo_dirty = true;
            return cl;
        }
        cl++;
        if (cl >= total + 2) cl = 2;
    }
    return 0;
}

static void free_chain(uint32_t cl) {
    uint32_t guard = vol.cluster_count;
    while (valid_cluster(cl) && guard--) {
        uint32_t next = fat_get(cl);
        fat_set(cl, 0);
        if (vol.free_count != FSINFO_UNKNOWN) vol.free_count++;
        if (cl < vol.next_free) vol.next_free = cl;
        cl = next;
    }
    vol.fsinfo_dirty = true;
}

static void zero_cluster(uint32_t cl) {
    alignas(4) uint8_t z[SECTOR_SIZE];
    mem_zero(z, SECTOR_SIZE);
    for (uint32_t s = 0; s < vol.spc; s++) ata_write_sector(cluster_lba(cl) + s, z);
}

uint32_t fat32_free_clusters() {
    if (!vol.mounted) return 0;
    if (vol.free_count == FSINFO_UNKNOWN || vol.free_count > vol.cluster_count) {
        uint32_t n = 0;
        for (uint32_t c = 2; c < vol.cluster_count + 2; c++) if (fat_get(c) == 0) n++;
        vol.free_count = n;
        vol.fsinfo_dirty = true;
        fsinfo_flush();
    }
    return vol.free_count;
}

struct dir_iter {
    uint32_t cluster;
    uint32_t sec;
    uint32_t idx;
    uint32_t lba;
    bool     loaded;
    alignas(4) uint8_t buf[SECTOR_SIZE];
};

static void iter_init(dir_iter* it, uint32_t cluster) {
    it->cluster = valid_cluster(cluster) ? cluster : 0;
    it->sec = 0; it->idx = 0; it->lba = 0; it->loaded = false;
}

/* Next raw 32-byte entry (pointer stays valid until the next call). */
static uint8_t* iter_raw(dir_iter* it, uint32_t* lba, uint16_t* off) {
    if (it->cluster == 0) return NULL;
    if (!it->loaded) {
        it->lba = cluster_lba(it->cluster) + it->sec;
        ata_read_sector(it->lba, it->buf);
        it->loaded = true;
    }
    uint8_t* e = &it->buf[it->idx * 32];
    *lba = it->lba;
    *off = (uint16_t)(it->idx * 32);

    if (++it->idx == SECTOR_SIZE / 32) {
        it->idx = 0;
        it->loaded = false;
        if (++it->sec >= vol.spc) {
            it->sec = 0;
            uint32_t next = fat_get(it->cluster);
            it->cluster = valid_cluster(next) ? next : 0;
        }
    }
    return e;
}

static uint8_t sfn_checksum(const uint8_t* sfn) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i]);
    return sum;
}

static void sfn_to_name(const uint8_t* e, char* out) {
    bool lower_base = (e[12] & 0x08) != 0;
    bool lower_ext  = (e[12] & 0x10) != 0;
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) {
        char c = (char)e[i];
        out[n++] = (lower_base && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) {
            char c = (char)e[i];
            out[n++] = (lower_ext && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
    }
    out[n] = '\0';
}

static bool dir_next(dir_iter* it, fat32_dirent* out) {
    static const uint8_t lfn_pos[LFN_CHARS_PER_ENTRY] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};

    char     lname[FAT32_NAME_MAX + 1];
    bool     lfn_ok = false;
    int      lfn_expect = 0;
    uint8_t  lfn_chk = 0;
    uint32_t lfn_n = 0;
    lname[0] = '\0';

    for (;;) {
        uint32_t lba; uint16_t off;
        uint8_t* e = iter_raw(it, &lba, &off);
        if (!e) return false;

        uint8_t first = e[0];
        if (first == 0x00) { it->cluster = 0; return false; }     /* end marker */
        if (first == 0xE5) { lfn_ok = false; lfn_n = 0; continue; }  /* deleted */

        uint8_t attr = e[11];

        if ((attr & 0x3F) == 0x0F) {                               /* long-name entry */
            uint8_t seq = first & 0x1F;
            if (seq < 1 || seq > FAT32_MAX_LFN_ENTRIES) { lfn_ok = false; lfn_n = 0; continue; }

            if (first & 0x40) {                                    /* last part = first on disk */
                lfn_ok = true; lfn_n = 0; lfn_chk = e[13]; lfn_expect = seq;
                for (int i = 0; i <= FAT32_NAME_MAX; i++) lname[i] = '\0';
            } else if (!lfn_ok || seq != lfn_expect - 1 || e[13] != lfn_chk) {
                lfn_ok = false; lfn_n = 0; continue;
            } else {
                lfn_expect = seq;
            }

            if (lfn_n < FAT32_MAX_LFN_ENTRIES) {
                out->lfn_lba[lfn_n] = lba;
                out->lfn_off[lfn_n] = off;
                lfn_n++;
            }
            for (int k = 0; k < LFN_CHARS_PER_ENTRY; k++) {
                uint16_t ch = rd16(e + lfn_pos[k]);
                int pos = (seq - 1) * LFN_CHARS_PER_ENTRY + k;
                if (pos >= FAT32_NAME_MAX) continue;               /* too long: keep the prefix */
                if (ch == 0x0000 || ch == 0xFFFF) lname[pos] = '\0';
                else lname[pos] = (ch < 0x80) ? (char)ch : '?';
            }
            continue;
        }

        if (attr & FAT_ATTR_VOLUME) { lfn_ok = false; lfn_n = 0; continue; }   /* volume label */

        for (int i = 0; i < 11; i++) out->sfn[i] = e[i];
        out->attr    = attr;
        out->cluster = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
        out->size    = rd32(e + 28);
        out->lba     = lba;
        out->off     = off;

        if (lfn_ok && lfn_expect == 1 && lfn_chk == sfn_checksum(out->sfn) && lname[0] != '\0') {
            out->lfn_count = (uint8_t)lfn_n;
            int i = 0;
            for (; i < FAT32_NAME_MAX && lname[i]; i++) out->name[i] = lname[i];
            out->name[i] = '\0';
        } else {
            out->lfn_count = 0;
            sfn_to_name(e, out->name);
        }
        return true;
    }
}

static bool entry_name_matches(const fat32_dirent* e, const char* name) {
    if (ci_equal(e->name, name)) return true;
    if (e->lfn_count) {                       /* also accept the 8.3 alias */
        char alias[16];
        uint8_t tmp[32];
        mem_zero(tmp, 32);
        for (int i = 0; i < 11; i++) tmp[i] = e->sfn[i];
        sfn_to_name(tmp, alias);
        return ci_equal(alias, name);
    }
    return false;
}

static int find_in_dir(uint32_t dir_cluster, const char* name, fat32_dirent* out) {
    dir_iter it;
    iter_init(&it, dir_cluster);
    while (dir_next(&it, out)) {
        if (entry_name_matches(out, name)) return FAT_OK;
    }
    return FAT_E_NOTFOUND;
}

static void put_entry(uint8_t* e, const uint8_t sfn[11], uint8_t nt, uint8_t attr,
                      uint32_t cluster, uint32_t size) {
    mem_zero(e, 32);
    for (int i = 0; i < 11; i++) e[i] = sfn[i];
    e[11] = attr;
    e[12] = nt;
    wr16(e + 14, 0);
    wr16(e + 16, FAT_DEFAULT_DATE);
    wr16(e + 18, FAT_DEFAULT_DATE);
    wr16(e + 20, (uint16_t)(cluster >> 16));
    wr16(e + 22, 0);
    wr16(e + 24, FAT_DEFAULT_DATE);
    wr16(e + 26, (uint16_t)cluster);
    wr32(e + 28, size);
}

static int dir_add_entry(uint32_t dir_cluster, const uint8_t sfn[11], uint8_t nt, uint8_t attr,
                         uint32_t cluster, uint32_t size) {
    alignas(4) uint8_t buf[SECTOR_SIZE];
    uint32_t cl = dir_cluster, last = dir_cluster;
    uint32_t guard = vol.cluster_count + 1;

    while (valid_cluster(cl) && guard--) {
        for (uint32_t s = 0; s < vol.spc; s++) {
            uint32_t lba = cluster_lba(cl) + s;
            ata_read_sector(lba, buf);
            for (uint32_t i = 0; i < SECTOR_SIZE / 32; i++) {
                uint8_t first = buf[i * 32];
                if (first == 0x00 || first == 0xE5) {
                    put_entry(&buf[i * 32], sfn, nt, attr, cluster, size);
                    ata_write_sector(lba, buf);
                    return FAT_OK;
                }
            }
        }
        last = cl;
        uint32_t next = fat_get(cl);
        if (!valid_cluster(next)) break;
        cl = next;
    }

    /* directory full: grow it by one (zeroed) cluster */
    uint32_t nc = alloc_cluster();
    if (!nc) return FAT_E_FULL;
    zero_cluster(nc);
    fat_set(last, nc);

    ata_read_sector(cluster_lba(nc), buf);
    put_entry(&buf[0], sfn, nt, attr, cluster, size);
    ata_write_sector(cluster_lba(nc), buf);
    return FAT_OK;
}

static void entry_update(uint32_t lba, uint16_t off, uint32_t cluster, uint32_t size) {
    alignas(4) uint8_t buf[SECTOR_SIZE];
    ata_read_sector(lba, buf);
    wr16(&buf[off + 20], (uint16_t)(cluster >> 16));
    wr16(&buf[off + 26], (uint16_t)cluster);
    wr32(&buf[off + 28], size);
    wr16(&buf[off + 24], FAT_DEFAULT_DATE);
    ata_write_sector(lba, buf);
}

static void entry_delete(const fat32_dirent* e) {
    alignas(4) uint8_t buf[SECTOR_SIZE];
    for (uint32_t i = 0; i < e->lfn_count; i++) {
        ata_read_sector(e->lfn_lba[i], buf);
        buf[e->lfn_off[i]] = 0xE5;
        ata_write_sector(e->lfn_lba[i], buf);
    }
    ata_read_sector(e->lba, buf);
    buf[e->off] = 0xE5;
    ata_write_sector(e->lba, buf);
}

static bool sfn_char_ok(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return true;
    const char* extra = "$%'-_@~!(){}^#&";
    for (int i = 0; extra[i]; i++) if (c == extra[i]) return true;
    return false;
}

/* Turns "readme.txt" into the 11-byte padded form. The NT flag byte records
 * all-lowercase base (0x08) / extension (0x10), so "readme.txt" round-trips
 * with its case. Mixed case ("ReadMe.txt") would need a long name -> FAT_E_NAME. */
int fat32_make_sfn(const char* name, uint8_t sfn[11], uint8_t* nt_flags) {
    uint32_t len = str_len(name);
    if (len == 0 || len > 12) return FAT_E_NAME;
    if (name[0] == '.' ) return FAT_E_NAME;

    int dot = -1;
    for (uint32_t i = 0; i < len; i++) {
        if (name[i] == '.') { if (dot != -1) return FAT_E_NAME; dot = (int)i; }
        else if (!sfn_char_ok(name[i])) return FAT_E_NAME;
    }

    uint32_t base_len = (dot == -1) ? len : (uint32_t)dot;
    uint32_t ext_len  = (dot == -1) ? 0   : len - dot - 1;
    if (base_len < 1 || base_len > 8 || ext_len > 3) return FAT_E_NAME;
    if (dot != -1 && ext_len == 0) return FAT_E_NAME;          /* "name." */

    bool base_lo = false, base_up = false, ext_lo = false, ext_up = false;
    for (int i = 0; i < 11; i++) sfn[i] = ' ';

    for (uint32_t i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') { base_lo = true; c = (char)(c - 32); }
        else if (c >= 'A' && c <= 'Z') base_up = true;
        sfn[i] = (uint8_t)c;
    }
    for (uint32_t i = 0; i < ext_len; i++) {
        char c = name[dot + 1 + i];
        if (c >= 'a' && c <= 'z') { ext_lo = true; c = (char)(c - 32); }
        else if (c >= 'A' && c <= 'Z') ext_up = true;
        sfn[8 + i] = (uint8_t)c;
    }
    if ((base_lo && base_up) || (ext_lo && ext_up)) return FAT_E_NAME;

    *nt_flags = (uint8_t)((base_lo ? 0x08 : 0) | (ext_lo ? 0x10 : 0));
    return FAT_OK;
}

static bool next_component(const char** pp, char* comp, int* too_long) {
    const char* p = *pp;
    while (*p == '/') p++;
    if (!*p) { *pp = p; return false; }
    int n = 0;
    while (*p && *p != '/') {
        if (n < FAT32_NAME_MAX) comp[n++] = *p;
        else *too_long = 1;
        p++;
    }
    comp[n] = '\0';
    *pp = p;
    return true;
}

static void path_pop(char* path) {
    int n = (int)str_len(path);
    while (n > 1 && path[n - 1] == '/') n--;
    while (n > 1 && path[n - 1] != '/') n--;
    if (n > 1) n--;                  /* drop the separator unless we are at the root */
    path[n] = '\0';
}

static int path_push(char* path, const char* name) {
    int n = (int)str_len(path);
    int add = (int)str_len(name);
    if (n + add + 2 > FAT32_PATH_MAX) return FAT_E_TOOLONG;
    if (n > 1) path[n++] = '/';
    for (int i = 0; i < add; i++) path[n++] = name[i];
    path[n] = '\0';
    return FAT_OK;
}

/* Walks `path` (all components must be directories). Optionally rebuilds the
 * canonical absolute path text in path_out (FAT32_PATH_MAX bytes). */
static int resolve_dir(const char* path, uint32_t* out_cluster, char* path_out) {
    uint32_t cur;
    char     tmp[FAT32_PATH_MAX];

    if (path[0] == '/') {
        cur = vol.root_cluster;
        tmp[0] = '/'; tmp[1] = '\0';
    } else {
        cur = cwd_cluster;
        for (uint32_t i = 0; i <= str_len(cwd_path); i++) tmp[i] = cwd_path[i];
    }

    const char* p = path;
    char comp[FAT32_NAME_MAX + 1];
    int too_long = 0;
    fat32_dirent e;

    while (next_component(&p, comp, &too_long)) {
        if (too_long) return FAT_E_TOOLONG;
        if (comp[0] == '.' && comp[1] == '\0') continue;

        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            if (cur != vol.root_cluster) {
                if (find_in_dir(cur, "..", &e) != FAT_OK) return FAT_E_CORRUPT;
                cur = (e.cluster == 0) ? vol.root_cluster : e.cluster;
            }
            path_pop(tmp);
            continue;
        }

        int r = find_in_dir(cur, comp, &e);
        if (r != FAT_OK) return r;
        if (!(e.attr & FAT_ATTR_DIR)) return FAT_E_NOTDIR;
        cur = (e.cluster == 0) ? vol.root_cluster : e.cluster;
        r = path_push(tmp, e.name);
        if (r != FAT_OK) return r;
    }

    *out_cluster = cur;
    if (path_out) for (uint32_t i = 0; i <= str_len(tmp); i++) path_out[i] = tmp[i];
    return FAT_OK;
}

static int split_path(const char* path, uint32_t* parent, char* leaf) {
    char tmp[FAT32_PATH_MAX];
    uint32_t n = str_len(path);
    if (n == 0) return FAT_E_INVAL;
    if (n >= FAT32_PATH_MAX) return FAT_E_TOOLONG;
    for (uint32_t i = 0; i <= n; i++) tmp[i] = path[i];

    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = '\0';
    if (n == 1 && tmp[0] == '/') return FAT_E_INVAL;

    int slash = -1;
    for (int i = (int)n - 1; i >= 0; i--) if (tmp[i] == '/') { slash = i; break; }

    const char* name;
    if (slash == -1) {
        *parent = cwd_cluster;
        name = tmp;
    } else {
        name = tmp + slash + 1;
        if (slash == 0) { *parent = vol.root_cluster; }
        else {
            tmp[slash] = '\0';
            int r = resolve_dir(tmp, parent, NULL);
            if (r != FAT_OK) return r;
        }
    }

    uint32_t len = str_len(name);
    if (len == 0) return FAT_E_INVAL;
    if (len > FAT32_NAME_MAX) return FAT_E_TOOLONG;
    for (uint32_t i = 0; i <= len; i++) leaf[i] = name[i];
    return FAT_OK;
}

static int lookup(const char* path, fat32_dirent* out, uint32_t* parent_out) {
    uint32_t parent;
    char leaf[FAT32_NAME_MAX + 1];
    int r = split_path(path, &parent, leaf);
    if (r != FAT_OK) return r;
    if (parent_out) *parent_out = parent;
    return find_in_dir(parent, leaf, out);
}

static bool looks_like_fat32_bpb(const uint8_t* s) {
    if (rd16(s + 510) != 0xAA55) return false;
    if (rd16(s + 11) != SECTOR_SIZE) return false;
    uint8_t spc = s[13];
    if (spc == 0 || (spc & (spc - 1)) != 0) return false;
    if (rd16(s + 14) == 0) return false;
    if (s[16] == 0) return false;
    if (rd16(s + 17) != 0 || rd16(s + 22) != 0) return false;
    if (rd32(s + 36) == 0) return false;
    return true;
}

int fat32_mount() {
    vol.mounted = false;
    fat_buf_sector = 0xFFFFFFFFu;
    cwd_cluster = 0;
    cwd_path[0] = '/'; cwd_path[1] = '\0';

    alignas(4) uint8_t sec[SECTOR_SIZE];
    uint32_t base = 0;

    ata_read_sector(0, sec);
    if (!looks_like_fat32_bpb(sec)) {
        if (rd16(sec + 510) != 0xAA55) return FAT_E_NOFS;
        bool found = false;
        for (int i = 0; i < 4; i++) {
            uint8_t type = sec[446 + i * 16 + 4];
            if (type == 0x0B || type == 0x0C) {
                base = rd32(&sec[446 + i * 16 + 8]);
                found = true;
                break;
            }
        }
        if (!found || base == 0) return FAT_E_NOFS;
        ata_read_sector(base, sec);
        if (!looks_like_fat32_bpb(sec)) return FAT_E_NOFS;
    }

    vol.part_lba     = base;
    vol.spc          = sec[13];
    vol.reserved     = rd16(sec + 14);
    vol.nfats        = sec[16];
    vol.fat_size     = rd32(sec + 36);
    vol.root_cluster = rd32(sec + 44);
    vol.total_sectors = rd16(sec + 19) ? rd16(sec + 19) : rd32(sec + 32);
    uint32_t fsinfo  = rd16(sec + 48);

    uint32_t meta = vol.reserved + vol.nfats * vol.fat_size;
    if (vol.total_sectors <= meta) return FAT_E_NOFS;

    vol.fat_lba       = base + vol.reserved;
    vol.data_lba      = base + meta;
    vol.cluster_count = (vol.total_sectors - meta) / vol.spc;
    if (vol.cluster_count < 1 || vol.cluster_count > 0x0FFFFFF0u) return FAT_E_NOFS;
    if (vol.root_cluster < 2 || vol.root_cluster >= vol.cluster_count + 2) return FAT_E_NOFS;

    vol.fsinfo_lba   = 0;
    vol.free_count   = FSINFO_UNKNOWN;
    vol.next_free    = 2;
    vol.fsinfo_dirty = false;
    if (fsinfo != 0 && fsinfo < vol.reserved) {
        ata_read_sector(base + fsinfo, sec);
        if (rd32(&sec[0]) == 0x41615252u && rd32(&sec[484]) == 0x61417272u &&
            rd32(&sec[508]) == 0xAA550000u) {
            vol.fsinfo_lba = base + fsinfo;
            uint32_t fc = rd32(&sec[488]), nf = rd32(&sec[492]);
            if (fc <= vol.cluster_count) vol.free_count = fc;
            if (nf >= 2 && nf < vol.cluster_count + 2) vol.next_free = nf;
        }
    }

    cwd_cluster = vol.root_cluster;
    vol.mounted = true;
    return FAT_OK;
}

int fat32_format(const char* label, uint32_t volume_id) {
    uint32_t total = ata_total_sectors();
    if (total == 0) return FAT_E_NODISK;
    if (total < 4200) return FAT_E_INVAL;

    const uint32_t spc = 1;
    const uint32_t reserved = 32;
    const uint32_t nfats = 2;
    /* Microsoft's formula for sectors per FAT (FAT32). */
    uint32_t t1 = total - reserved;
    uint32_t t2 = (256 * spc + nfats) / 2;
    uint32_t fat_size = (t1 + (t2 - 1)) / t2;

    alignas(4) uint8_t z[SECTOR_SIZE];
    alignas(4) uint8_t sec[SECTOR_SIZE];
    mem_zero(z, SECTOR_SIZE);

    vol.mounted = false;
    fat_buf_sector = 0xFFFFFFFFu;

    mem_zero(sec, SECTOR_SIZE);
    sec[0] = 0xEB; sec[1] = 0x58; sec[2] = 0x90;
    const char* oem = "VISTA   ";
    for (int i = 0; i < 8; i++) sec[3 + i] = (uint8_t)oem[i];
    wr16(&sec[11], SECTOR_SIZE);
    sec[13] = (uint8_t)spc;
    wr16(&sec[14], (uint16_t)reserved);
    sec[16] = (uint8_t)nfats;
    wr16(&sec[17], 0);                       /* root entries (FAT12/16 only) */
    wr16(&sec[19], 0);                       /* total sectors 16 */
    sec[21] = 0xF8;                          /* media: fixed disk */
    wr16(&sec[22], 0);                       /* sectors per FAT 16 */
    wr16(&sec[24], 63);                      /* sectors per track */
    wr16(&sec[26], 255);                     /* heads */
    wr32(&sec[28], 0);                       /* hidden sectors */
    wr32(&sec[32], total);
    wr32(&sec[36], fat_size);
    wr16(&sec[40], 0);                       /* ext flags: both FATs active, mirrored */
    wr16(&sec[42], 0);                       /* version 0.0 */
    wr32(&sec[44], 2);                       /* root cluster */
    wr16(&sec[48], 1);                       /* FSInfo sector */
    wr16(&sec[50], 6);                       /* backup boot sector */
    sec[64] = 0x80;                          /* drive number */
    sec[66] = 0x29;                          /* extended boot signature */
    wr32(&sec[67], volume_id);
    for (int i = 0; i < 11; i++) sec[71 + i] = ' ';
    for (int i = 0; i < 11 && label && label[i]; i++) {
        char c = label[i];
        sec[71 + i] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    const char* fstype = "FAT32   ";
    for (int i = 0; i < 8; i++) sec[82 + i] = (uint8_t)fstype[i];
    sec[510] = 0x55; sec[511] = 0xAA;
    ata_write_sector(0, sec);
    ata_write_sector(6, sec);

    /* FSInfo (free count = everything except the root cluster) */
    uint32_t data_sectors = total - reserved - nfats * fat_size;
    uint32_t clusters = data_sectors / spc;
    mem_zero(sec, SECTOR_SIZE);
    wr32(&sec[0], 0x41615252u);
    wr32(&sec[484], 0x61417272u);
    wr32(&sec[488], clusters - 1);
    wr32(&sec[492], 3);
    wr32(&sec[508], 0xAA550000u);
    ata_write_sector(1, sec);
    ata_write_sector(7, sec);

    for (uint32_t s = 2; s < reserved; s++) {
        if (s == 6 || s == 7) continue;
        ata_write_sector(s, z);
    }

    /* both FATs: all free except entries 0, 1 and the root cluster */
    for (uint32_t f = 0; f < nfats; f++) {
        for (uint32_t s = 0; s < fat_size; s++) {
            if (s == 0) {
                mem_zero(sec, SECTOR_SIZE);
                wr32(&sec[0], 0x0FFFFFF8u);      /* media descriptor */
                wr32(&sec[4], 0x0FFFFFFFu);
                wr32(&sec[8], FAT_EOC_MARK);     /* cluster 2 = root directory */
                ata_write_sector(reserved + f * fat_size + s, sec);
            } else {
                ata_write_sector(reserved + f * fat_size + s, z);
            }
        }
    }

    uint32_t data_lba = reserved + nfats * fat_size;
    for (uint32_t s = 0; s < spc; s++) ata_write_sector(data_lba + s, z);

    int r = fat32_mount();
    if (r != FAT_OK) return r;

    fat32_mkdir("home");
    fat32_mkdir("bin");
    fat32_mkdir("etc");
    fat32_mkdir("var");
    fat32_mkdir("tmp");
    const char* readme =
        "Welcome to Vista OS! This is a simple text file created on the root directory "
        "of your FAT32 virtual disk. Feel free to explore the filesystem, create new "
        "directories and files, and write your own content.\n";
    fat32_write_file("readme.txt", readme, str_len(readme));
    return FAT_OK;
}

int fat32_chdir(const char* path) {
    if (!vol.mounted) return FAT_E_NOFS;
    uint32_t cl;
    char np[FAT32_PATH_MAX];
    int r = resolve_dir(path, &cl, np);
    if (r != FAT_OK) return r;
    cwd_cluster = cl;
    for (uint32_t i = 0; i <= str_len(np); i++) cwd_path[i] = np[i];
    return FAT_OK;
}

int fat32_list_dir(const char* path, fat32_list_cb cb, void* ctx) {
    if (!vol.mounted) return FAT_E_NOFS;
    uint32_t cl = cwd_cluster;
    if (path && path[0]) {
        int r = resolve_dir(path, &cl, NULL);
        if (r != FAT_OK) return r;
    }
    dir_iter it;
    fat32_dirent e;
    iter_init(&it, cl);
    while (dir_next(&it, &e)) {
        if (e.name[0] == '.' && (e.name[1] == '\0' || (e.name[1] == '.' && e.name[2] == '\0'))) continue;
        if (!cb(&e, ctx)) break;
    }
    return FAT_OK;
}

int fat32_stat(const char* path, fat32_dirent* out) {
    if (!vol.mounted) return FAT_E_NOFS;
    return lookup(path, out, NULL);
}

int fat32_create(const char* path) {
    if (!vol.mounted) return FAT_E_NOFS;
    uint32_t parent;
    char leaf[FAT32_NAME_MAX + 1];
    int r = split_path(path, &parent, leaf);
    if (r != FAT_OK) return r;

    uint8_t sfn[11], nt;
    r = fat32_make_sfn(leaf, sfn, &nt);
    if (r != FAT_OK) return r;

    fat32_dirent e;
    if (find_in_dir(parent, leaf, &e) == FAT_OK) return FAT_E_EXISTS;

    r = dir_add_entry(parent, sfn, nt, FAT_ATTR_ARCHIVE, 0, 0);
    fsinfo_flush();
    return r;
}

int fat32_mkdir(const char* path) {
    if (!vol.mounted) return FAT_E_NOFS;
    uint32_t parent;
    char leaf[FAT32_NAME_MAX + 1];
    int r = split_path(path, &parent, leaf);
    if (r != FAT_OK) return r;

    uint8_t sfn[11], nt;
    r = fat32_make_sfn(leaf, sfn, &nt);
    if (r != FAT_OK) return r;

    fat32_dirent e;
    if (find_in_dir(parent, leaf, &e) == FAT_OK) return FAT_E_EXISTS;

    uint32_t cl = alloc_cluster();
    if (!cl) { fsinfo_flush(); return FAT_E_FULL; }
    zero_cluster(cl);

    /* "." and ".." (a parent of the root is stored as cluster 0) */
    alignas(4) uint8_t sec[SECTOR_SIZE];
    mem_zero(sec, SECTOR_SIZE);
    uint8_t dot[11], dotdot[11];
    for (int i = 0; i < 11; i++) { dot[i] = ' '; dotdot[i] = ' '; }
    dot[0] = '.';
    dotdot[0] = '.'; dotdot[1] = '.';
    put_entry(&sec[0],  dot,    0, FAT_ATTR_DIR, cl, 0);
    put_entry(&sec[32], dotdot, 0, FAT_ATTR_DIR, parent == vol.root_cluster ? 0 : parent, 0);
    ata_write_sector(cluster_lba(cl), sec);

    r = dir_add_entry(parent, sfn, nt, FAT_ATTR_DIR, cl, 0);
    if (r != FAT_OK) free_chain(cl);
    fsinfo_flush();
    return r;
}

int fat32_remove(const char* path) {
    if (!vol.mounted) return FAT_E_NOFS;
    fat32_dirent e;
    int r = lookup(path, &e, NULL);
    if (r != FAT_OK) return r;
    if (e.attr & FAT_ATTR_DIR) return FAT_E_ISDIR;

    if (e.cluster) free_chain(e.cluster);
    entry_delete(&e);
    fsinfo_flush();
    return FAT_OK;
}

int fat32_rmdir(const char* path) {
    if (!vol.mounted) return FAT_E_NOFS;
    fat32_dirent e;
    int r = lookup(path, &e, NULL);
    if (r != FAT_OK) return r;
    if (!(e.attr & FAT_ATTR_DIR)) return FAT_E_NOTDIR;
    if (e.name[0] == '.') return FAT_E_INVAL;
    if (e.cluster == cwd_cluster) return FAT_E_BUSY;

    dir_iter it;
    fat32_dirent child;
    iter_init(&it, e.cluster);
    while (dir_next(&it, &child)) {
        bool dots = child.name[0] == '.' &&
                    (child.name[1] == '\0' || (child.name[1] == '.' && child.name[2] == '\0'));
        if (!dots) return FAT_E_NOTEMPTY;
    }

    free_chain(e.cluster);
    entry_delete(&e);
    fsinfo_flush();
    return FAT_OK;
}

static int walk_file(const fat32_dirent* e, uint32_t limit, fat32_stream_cb cb, void* ctx) {
    alignas(4) uint8_t sec[SECTOR_SIZE];
    uint32_t remaining = e->size < limit ? e->size : limit;
    uint32_t cl = e->cluster;
    uint32_t guard = vol.cluster_count + 1;

    while (remaining && valid_cluster(cl) && guard--) {
        for (uint32_t s = 0; s < vol.spc && remaining; s++) {
            ata_read_sector(cluster_lba(cl) + s, sec);
            uint32_t n = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            cb(sec, n, ctx);
            remaining -= n;
        }
        cl = fat_get(cl);
    }
    return remaining ? FAT_E_CORRUPT : FAT_OK;   /* chain ended before the file did */
}

int fat32_stream_file(const char* path, fat32_stream_cb cb, void* ctx) {
    if (!vol.mounted) return FAT_E_NOFS;
    fat32_dirent e;
    int r = lookup(path, &e, NULL);
    if (r != FAT_OK) return r;
    if (e.attr & FAT_ATTR_DIR) return FAT_E_ISDIR;
    return walk_file(&e, 0xFFFFFFFFu, cb, ctx);
}

struct read_ctx { uint8_t* out; uint32_t done; };
static void read_cb(const uint8_t* data, uint32_t len, void* c) {
    read_ctx* rc = (read_ctx*)c;
    mem_copy(rc->out + rc->done, data, len);
    rc->done += len;
}

int fat32_read_file(const char* path, void* buf, uint32_t max, uint32_t* out_len) {
    if (!vol.mounted) return FAT_E_NOFS;
    fat32_dirent e;
    int r = lookup(path, &e, NULL);
    if (r != FAT_OK) return r;
    if (e.attr & FAT_ATTR_DIR) return FAT_E_ISDIR;

    read_ctx rc;
    rc.out = (uint8_t*)buf;
    rc.done = 0;
    r = walk_file(&e, max, read_cb, &rc);
    if (out_len) *out_len = rc.done;
    return r;
}

int fat32_write_file(const char* path, const void* data, uint32_t len) {
    if (!vol.mounted) return FAT_E_NOFS;
    uint32_t parent;
    char leaf[FAT32_NAME_MAX + 1];
    int r = split_path(path, &parent, leaf);
    if (r != FAT_OK) return r;

    uint8_t sfn[11], nt;
    r = fat32_make_sfn(leaf, sfn, &nt);
    if (r != FAT_OK) {
        /* an existing long-named file can still be rewritten */
        fat32_dirent probe;
        if (find_in_dir(parent, leaf, &probe) != FAT_OK) return r;
    }

    fat32_dirent e;
    bool exists = (find_in_dir(parent, leaf, &e) == FAT_OK);
    if (exists && (e.attr & FAT_ATTR_DIR)) return FAT_E_ISDIR;

    if (exists && e.cluster) free_chain(e.cluster);

    const uint8_t* src = (const uint8_t*)data;
    uint32_t first = 0, prev = 0, written = 0;
    alignas(4) uint8_t sec[SECTOR_SIZE];

    while (written < len) {
        uint32_t cl = alloc_cluster();
        if (!cl) {
            if (first) free_chain(first);
            if (exists) entry_update(e.lba, e.off, 0, 0);
            fsinfo_flush();
            return FAT_E_FULL;
        }
        if (prev) fat_set(prev, cl); else first = cl;

        for (uint32_t s = 0; s < vol.spc; s++) {
            mem_zero(sec, SECTOR_SIZE);
            if (written < len) {
                uint32_t n = len - written;
                if (n > SECTOR_SIZE) n = SECTOR_SIZE;
                mem_copy(sec, src + written, n);
                written += n;
            }
            ata_write_sector(cluster_lba(cl) + s, sec);
        }
        prev = cl;
    }

    if (exists) {
        entry_update(e.lba, e.off, first, len);
    } else {
        r = dir_add_entry(parent, sfn, nt, FAT_ATTR_ARCHIVE, first, len);
        if (r != FAT_OK) { if (first) free_chain(first); fsinfo_flush(); return r; }
    }
    fsinfo_flush();
    return FAT_OK;
}
