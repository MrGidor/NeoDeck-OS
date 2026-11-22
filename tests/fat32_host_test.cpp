// Host-side test for the FAT32 driver + fs front-end.
// The kernel sources are compiled unchanged; only the ATA driver and the terminal
// are replaced by file-backed / stdout stubs. tools/fat32img.py is used as an
// independent implementation to create images and to check what the kernel wrote.
//
//   tests/run_fat32_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <string>
#include <vector>

#include "../kernel/include/types.h"
#include "../kernel/drivers/terminal.h"
#include "../kernel/drivers/timer.h"
#include "../kernel/drivers/ata.h"
#include "../kernel/fs/fat32.h"
#include "../kernel/fs/fs.h"
#include "neodeck_abi.h"

// ---------------------------------------------------------------- stubs
static FILE* g_disk = nullptr;
static uint32_t g_sectors = 0;
static unsigned g_writes = 0;

static void open_disk(const char* path) {
    if (g_disk) fclose(g_disk);
    g_disk = fopen(path, "r+b");
    if (!g_disk) { perror(path); exit(2); }
    fseek(g_disk, 0, SEEK_END);
    g_sectors = (uint32_t)(ftell(g_disk) / 512);
}
void ata_read_sector(uint32_t lba, uint8_t* buf) {
    if (lba >= g_sectors) { printf("FATAL: read past end of disk (lba %u)\n", lba); exit(3); }
    fseek(g_disk, (long)lba * 512, SEEK_SET);
    if (fread(buf, 1, 512, g_disk) != 512) { printf("FATAL: short read\n"); exit(3); }
}
void ata_write_sector(uint32_t lba, uint8_t* buf) {
    if (lba >= g_sectors) { printf("FATAL: write past end of disk (lba %u)\n", lba); exit(3); }
    fseek(g_disk, (long)lba * 512, SEEK_SET);
    fwrite(buf, 1, 512, g_disk);
    g_writes++;
}
uint32_t ata_total_sectors() { return g_sectors; }
uint32_t timer_ticks() { return 12345; }

static bool g_quiet = true;
void terminal_put_char(char c) { if (!g_quiet) putchar(c); }
void kprint(const char* s) { if (!g_quiet) fputs(s, stdout); }
void kprint_log(const char* s, bool) { if (!g_quiet) fputs(s, stdout); }
void kprint_color(const char* s, uint8_t) { if (!g_quiet) fputs(s, stdout); }
void kprint_success(const char* s) { kprint(s); }
void kprint_warning(const char* s) { kprint(s); }
void kprint_error(const char* s) { kprint(s); }
void kprint_info(const char* s) { kprint(s); }
void kprint_dec(uint32_t v) { if (!g_quiet) printf("%u", v); }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a == _b) g_pass++; else { g_fail++; printf("  FAIL %s:%d: %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

// The disk is a stdio FILE: flush before any external tool looks at the image.
static void sh(const std::string& cmd) { if (g_disk) fflush(g_disk); int r = system(cmd.c_str()); (void)r; }
static int  sh_rc(const std::string& cmd) {
    if (g_disk) fflush(g_disk);
    int st = system(cmd.c_str());
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void py_check(const char* img, const char* what) {
    std::string cmd = std::string("python3 tools/fat32img.py check ") + img + " > /tmp/fsck.out 2>&1";
    int rc = sh_rc(cmd);
    if (rc != 0) { sh("cat /tmp/fsck.out"); }
    printf("  [fsck after %s] %s\n", what, rc == 0 ? "clean" : "PROBLEMS");
    CHECK_EQ(rc, 0);
}

static std::vector<uint8_t> pattern(uint32_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < n; i++) { x = x * 1103515245u + 12345u; v[i] = (uint8_t)(x >> 16); }
    return v;
}

static std::vector<uint8_t> readback(const char* path) {
    fat32_dirent e;
    std::vector<uint8_t> out;
    if (fat32_stat(path, &e) != FAT_OK) return out;
    out.resize(e.size);
    uint32_t n = 0;
    fat32_read_file(path, out.data(), e.size, &n);
    out.resize(n);
    return out;
}

struct names_ctx { std::vector<std::string> names; };
static bool collect(const fat32_dirent* e, void* c) { ((names_ctx*)c)->names.push_back(e->name); return true; }
static std::vector<std::string> list(const char* path = nullptr) {
    names_ctx c; fat32_list_dir(path, collect, &c); return c.names;
}
static bool has(const std::vector<std::string>& v, const char* n) {
    for (auto& s : v) if (s == n) return true;
    return false;
}

// tests
static void test_mount_python_image() {
    printf("[1] mount an image made by tools/fat32img.py\n");
    sh("python3 tools/fat32img.py create /tmp/a.img 64 > /dev/null");
    sh("printf 'hello from the host' > /tmp/host.txt; python3 tools/fat32img.py put /tmp/a.img /tmp/host.txt /bin/host.txt > /dev/null");
    open_disk("/tmp/a.img");
    CHECK_EQ(fat32_mount(), FAT_OK);
    auto root = list();
    CHECK(has(root, "home") && has(root, "bin") && has(root, "readme.txt"));
    CHECK_EQ(strcmp(fat32_cwd(), "/"), 0);
    auto rd = readback("readme.txt");
    CHECK(rd.size() == 207 && memcmp(rd.data(), "Welcome to NeoDeck OS!", 22) == 0);
    auto h = readback("/bin/host.txt");
    CHECK(h.size() == 19 && memcmp(h.data(), "hello from the host", 19) == 0);
    CHECK_EQ(fat32_chdir("bin"), FAT_OK);
    CHECK_EQ(strcmp(fat32_cwd(), "/bin"), 0);
    CHECK(has(list(), "host.txt"));
    CHECK_EQ(fat32_chdir(".."), FAT_OK);
    CHECK_EQ(strcmp(fat32_cwd(), "/"), 0);
    uint32_t total = fat32_total_clusters();
    CHECK(total > 65525);   // a "real" FAT32 by cluster count
    CHECK(fat32_free_clusters() < total);
}

static void test_format_and_python_agrees() {
    printf("[2] kernel format -> python fsck + listing\n");
    sh("rm -f /tmp/b.img; truncate -s 64M /tmp/b.img");
    open_disk("/tmp/b.img");
    CHECK_EQ(fat32_mount(), FAT_E_NOFS);                       // blank disk
    CHECK_EQ(fat32_format("NEODECK", 0x1234), FAT_OK);
    CHECK(fat32_is_mounted());
    py_check("/tmp/b.img", "format");
    CHECK_EQ(sh_rc("python3 tools/fat32img.py ls /tmp/b.img | grep -q readme.txt"), 0);
    CHECK_EQ(sh_rc("python3 tools/fat32img.py cat /tmp/b.img /readme.txt | grep -q 'Welcome to NeoDeck OS'"), 0);
    CHECK_EQ(sh_rc("file /tmp/b.img | grep -q 'FAT (32 bit)'"), 0);
    // remount from disk: everything must come back
    CHECK_EQ(fat32_mount(), FAT_OK);
    CHECK(has(list(), "home") && has(list(), "tmp"));
}

static void test_dirs_and_paths() {
    printf("[3] directories, relative/absolute paths, cwd text\n");
    CHECK_EQ(fat32_mkdir("/home/user"), FAT_OK);
    CHECK_EQ(fat32_mkdir("home/user/docs"), FAT_OK);
    CHECK_EQ(fat32_mkdir("home/user/docs"), FAT_E_EXISTS);
    CHECK_EQ(fat32_mkdir("nodir/x"), FAT_E_NOTFOUND);
    CHECK_EQ(fat32_chdir("/home/user/docs"), FAT_OK);
    CHECK_EQ(strcmp(fat32_cwd(), "/home/user/docs"), 0);
    CHECK_EQ(fat32_chdir("../.."), FAT_OK);
    CHECK_EQ(strcmp(fat32_cwd(), "/home"), 0);
    CHECK_EQ(fat32_chdir("USER/./docs/../docs"), FAT_OK);       // case-insensitive, "." and ".."
    CHECK_EQ(strcmp(fat32_cwd(), "/home/user/docs"), 0);        // canonical text uses real names
    CHECK_EQ(fat32_chdir("/"), FAT_OK);
    CHECK_EQ(strcmp(fat32_cwd(), "/"), 0);
    CHECK_EQ(fat32_chdir(".."), FAT_OK);                         // ".." at the root stays at the root
    CHECK_EQ(strcmp(fat32_cwd(), "/"), 0);
    CHECK_EQ(fat32_chdir("readme.txt"), FAT_E_NOTDIR);
    CHECK_EQ(fat32_chdir("missing"), FAT_E_NOTFOUND);
    CHECK_EQ(strcmp(fat32_cwd(), "/"), 0);                       // failed cd leaves cwd alone

    // ".." entry of a subdirectory directly under the root must be cluster 0
    fat32_dirent e;
    CHECK_EQ(fat32_stat("/home/..", &e), FAT_OK);
    CHECK_EQ(e.cluster, 0);
    CHECK_EQ(fat32_stat("/home/user/..", &e), FAT_OK);
    fat32_dirent h; fat32_stat("/home", &h);
    CHECK_EQ(e.cluster, h.cluster);
    py_check("/tmp/b.img", "mkdir tree");
}

static void test_files() {
    printf("[4] create / write / read / overwrite / remove\n");
    CHECK_EQ(fat32_chdir("/home/user"), FAT_OK);
    CHECK_EQ(fat32_create("empty.txt"), FAT_OK);
    CHECK_EQ(fat32_create("empty.txt"), FAT_E_EXISTS);
    fat32_dirent e;
    CHECK_EQ(fat32_stat("empty.txt", &e), FAT_OK);
    CHECK(e.size == 0 && e.cluster == 0);

    const char* msg = "hello neodeck";
    CHECK_EQ(fat32_write_file("hello.txt", msg, strlen(msg)), FAT_OK);       // write creates
    auto rd = readback("hello.txt");
    CHECK(rd.size() == strlen(msg) && memcmp(rd.data(), msg, rd.size()) == 0);

    // multi-cluster payloads of awkward sizes, including exact sector multiples
    uint32_t sizes[] = {1, 511, 512, 513, 1024, 4096, 70000, 300000};
    for (uint32_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        auto data = pattern(sizes[i], i + 1);
        CHECK_EQ(fat32_write_file("big.bin", data.data(), data.size()), FAT_OK);
        auto back = readback("big.bin");
        CHECK(back == data);
    }
    py_check("/tmp/b.img", "multi-size overwrite");

    // shrink to empty releases the chain
    uint32_t free_before = fat32_free_clusters();
    CHECK_EQ(fat32_write_file("big.bin", "", 0), FAT_OK);
    CHECK(fat32_free_clusters() > free_before);
    CHECK(readback("big.bin").empty());
    CHECK_EQ(fat32_stat("big.bin", &e), FAT_OK);
    CHECK(e.size == 0 && e.cluster == 0);

    // absolute path write/read from another cwd
    CHECK_EQ(fat32_chdir("/"), FAT_OK);
    CHECK_EQ(fat32_write_file("/home/user/docs/note.txt", "abs", 3), FAT_OK);
    CHECK(readback("home/user/docs/note.txt").size() == 3);

    // remove
    CHECK_EQ(fat32_remove("/home/user/hello.txt"), FAT_OK);
    CHECK_EQ(fat32_stat("/home/user/hello.txt", &e), FAT_E_NOTFOUND);
    CHECK_EQ(fat32_remove("/home/user/hello.txt"), FAT_E_NOTFOUND);
    CHECK_EQ(fat32_remove("/home/user/docs"), FAT_E_ISDIR);
    CHECK_EQ(fat32_write_file("/home/user/docs", "x", 1), FAT_E_ISDIR);

    // rmdir rules
    CHECK_EQ(fat32_rmdir("/home/user/docs"), FAT_E_NOTEMPTY);
    CHECK_EQ(fat32_remove("/home/user/docs/note.txt"), FAT_OK);
    CHECK_EQ(fat32_chdir("/home/user/docs"), FAT_OK);
    CHECK_EQ(fat32_rmdir("/home/user/docs"), FAT_E_BUSY);        // cwd
    CHECK_EQ(fat32_chdir("/"), FAT_OK);
    CHECK_EQ(fat32_rmdir("/home/user/docs"), FAT_OK);
    CHECK_EQ(fat32_rmdir("readme.txt"), FAT_E_NOTDIR);
    CHECK_EQ(fat32_rmdir("/home/user/docs"), FAT_E_NOTFOUND);
    py_check("/tmp/b.img", "remove/rmdir");
}

static void test_names() {
    printf("[5] 8.3 name rules and case round-trip\n");
    CHECK_EQ(fat32_chdir("/tmp"), FAT_OK);
    CHECK_EQ(fat32_create("readme.txt"), FAT_OK);
    CHECK_EQ(fat32_create("README.TXT"), FAT_E_EXISTS);          // case-insensitive
    CHECK_EQ(fat32_create("UPPER.BIN"), FAT_OK);
    CHECK_EQ(fat32_create("Mixed.txt"), FAT_E_NAME);
    CHECK_EQ(fat32_create("toolongname.txt"), FAT_E_NAME);
    CHECK_EQ(fat32_create("a.toolong"), FAT_E_NAME);
    CHECK_EQ(fat32_create("a.b.c"), FAT_E_NAME);
    CHECK_EQ(fat32_create("has space"), FAT_E_NAME);
    CHECK_EQ(fat32_create(".hidden"), FAT_E_NAME);
    CHECK_EQ(fat32_create("noext"), FAT_OK);
    CHECK_EQ(fat32_create("x1.c"), FAT_OK);
    auto n = list();
    CHECK(has(n, "readme.txt") && has(n, "UPPER.BIN") && has(n, "noext") && has(n, "x1.c"));
    CHECK(!has(n, "README.TXT"));
    CHECK_EQ(fat32_create(""), FAT_E_INVAL);
    CHECK_EQ(fat32_create("/"), FAT_E_INVAL);
    std::string longname(200, 'a');
    CHECK_EQ(fat32_create(longname.c_str()), FAT_E_TOOLONG);
    CHECK_EQ(fat32_chdir("/"), FAT_OK);
    py_check("/tmp/b.img", "names");
}

static void test_directory_growth() {
    printf("[6] directory grows past one cluster; slots are reused after delete\n");
    CHECK_EQ(fat32_mkdir("/var/many"), FAT_OK);
    CHECK_EQ(fat32_chdir("/var/many"), FAT_OK);
    char name[32];
    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof name, "f%03d.txt", i);
        CHECK_EQ(fat32_write_file(name, name, strlen(name)), FAT_OK);
    }
    CHECK_EQ(list().size(), 100);
    for (int i = 0; i < 100; i += 7) {
        snprintf(name, sizeof name, "f%03d.txt", i);
        auto d = readback(name);
        CHECK(d.size() == strlen(name) && memcmp(d.data(), name, d.size()) == 0);
    }
    for (int i = 0; i < 100; i += 2) {
        snprintf(name, sizeof name, "f%03d.txt", i);
        CHECK_EQ(fat32_remove(name), FAT_OK);
    }
    CHECK_EQ(list().size(), 50);
    for (int i = 0; i < 20; i++) {                       // fills the holes, must not grow further
        snprintf(name, sizeof name, "g%03d.txt", i);
        CHECK_EQ(fat32_create(name), FAT_OK);
    }
    CHECK_EQ(list().size(), 70);
    CHECK_EQ(fat32_chdir("/"), FAT_OK);
    py_check("/tmp/b.img", "directory growth");
}

static void test_front_end() {
    printf("[7] fs.cpp wrappers (dirents, stat, read_to_buffer, raw reads)\n");
    CHECK_EQ(fat32_chdir("/home/user"), FAT_OK);
    fs_write("t.txt", "front end");
    CHECK_EQ(fs_file_size("t.txt"), 9);
    CHECK_EQ(fs_file_size("missing"), 0);
    CHECK_EQ(fs_file_size("/home"), 0);                   // directory
    char buf[32];
    CHECK_EQ(fs_read_to_buffer("t.txt", buf, sizeof buf), 9);
    CHECK_EQ(strcmp(buf, "front end"), 0);
    CHECK_EQ(fs_read_to_buffer("t.txt", buf, 5), 4);       // truncates, keeps room for the NUL
    CHECK_EQ(strcmp(buf, "fron"), 0);
    CHECK_EQ(fs_read_to_buffer("missing", buf, sizeof buf), 0);
    CHECK_EQ(buf[0], 0);

    neodeck_dirent ents[8];
    int n = fs_get_dir_entries(ents, 8);
    CHECK(n >= 2);
    bool found_t = false;
    for (int i = 0; i < n; i++) if (strcmp(ents[i].name, "t.txt") == 0) { found_t = (ents[i].size == 9 && !ents[i].is_dir); }
    CHECK(found_t);
    CHECK_EQ(fs_get_dir_entries(ents, 1), 1);              // honours max

    neodeck_stat st;
    CHECK_EQ(fs_stat("t.txt", &st), 0);
    CHECK(st.size == 9 && !st.is_dir);
    CHECK_EQ(fs_stat("/home", &st), 0);
    CHECK(st.is_dir);
    CHECK(fs_stat("nope", &st) != 0);

    CHECK_EQ(fs_remove_quiet("t.txt"), 0);
    CHECK(fs_remove_quiet("t.txt") != 0);
    CHECK_EQ(fat32_chdir("/"), FAT_OK);

    // the old shell flows still work through the wrappers
    g_quiet = true;
    fs_ls(); fs_cd("home"); fs_mkdir("x"); fs_touch("y"); fs_write("y", "z"); fs_cat("y");
    fs_rm("y"); fs_rmdir("x"); fs_cd(".."); fs_df();
    CHECK_EQ(strcmp(fs_cwd(), "/"), 0);
    py_check("/tmp/b.img", "front end");
}

static void test_lfn() {
    printf("[8] long file names written by another OS: list, find by long name and alias, delete\n");
    sh("python3 tools/fat32img.py create /tmp/l.img 64 > /dev/null");
    // inject LFN entries with an independent script (this is what Linux/Windows would write)
    sh("python3 - <<'PY'\n"
       "import sys; sys.path.insert(0,'tools')\n"
       "import fat32img as F, struct\n"
       "fs = F.Fat32('/tmp/l.img', True)\n"
       "def lfn_entries(long, sfn):\n"
       "    chk = F.Fat32.checksum(sfn)\n"
       "    u = long.encode('utf-16-le') + b'\\0\\0'\n"
       "    u = u.ljust(((len(u)+25)//26)*26, b'\\xff')\n"
       "    n = len(u)//26\n"
       "    out = []\n"
       "    for seq in range(n, 0, -1):\n"
       "        part = u[(seq-1)*26:seq*26]\n"
       "        e = bytearray(32)\n"
       "        e[0] = seq | (0x40 if seq == n else 0)\n"
       "        e[1:11] = part[0:10]; e[11] = 0x0F; e[13] = chk; e[14:26] = part[10:22]; e[28:32] = part[22:26]\n"
       "        out.append(bytes(e))\n"
       "    return out\n"
       "def add(long, sfn, data):\n"
       "    first = fs.write_chain(data)\n"
       "    ents = lfn_entries(long, sfn) + [fs.make_entry(sfn, 0, 0x20, first, len(data))]\n"
       "    for e in ents: fs.add_entry(fs.root, e)\n"
       "add('Long File Name Example.txt', b'LONGFI~1TXT', b'long name payload')\n"
       "add('A name that is quite a bit longer than thirteen characters.dat', b'ANAMET~1DAT', b'second')\n"
       "fs.sync()\n"
       "PY");
    open_disk("/tmp/l.img");
    CHECK_EQ(fat32_mount(), FAT_OK);
    auto n = list();
    CHECK(has(n, "Long File Name Example.txt"));
    CHECK(has(n, "A name that is quite a bit longer than thirteen characters.dat"));
    CHECK(has(n, "readme.txt"));
    CHECK_EQ(n.size(), 8);                                   // 5 dirs + readme + 2 long files
    auto d = readback("long file name example.txt");        // case-insensitive long name
    CHECK(d.size() == 17 && memcmp(d.data(), "long name payload", 17) == 0);
    d = readback("LONGFI~1.TXT");                            // 8.3 alias
    CHECK(d.size() == 17);
    // rewriting a long-named file keeps the name (no new 8.3 entry)
    CHECK_EQ(fat32_write_file("Long File Name Example.txt", "rewritten", 9), FAT_OK);
    CHECK_EQ(readback("Long File Name Example.txt").size(), 9);
    CHECK_EQ(list().size(), 8);
    // deleting must remove the LFN entries too, otherwise they would be orphans
    CHECK_EQ(fat32_remove("Long File Name Example.txt"), FAT_OK);
    CHECK_EQ(fat32_remove("A name that is quite a bit longer than thirteen characters.dat"), FAT_OK);
    CHECK_EQ(list().size(), 6);
    py_check("/tmp/l.img", "LFN delete");
    CHECK_EQ(sh_rc("python3 tools/fat32img.py ls /tmp/l.img | grep -qi 'long'"), 1);   // really gone
}

static void test_disk_full() {
    printf("[9] disk full handling does not leak clusters\n");
    sh("rm -f /tmp/s.img; truncate -s 3M /tmp/s.img");
    open_disk("/tmp/s.img");
    CHECK_EQ(fat32_format("SMALL", 1), FAT_OK);
    uint32_t free0 = fat32_free_clusters();
    std::vector<uint8_t> big(4 * 1024 * 1024, 0xAB);
    CHECK_EQ(fat32_write_file("huge.bin", big.data(), big.size()), FAT_E_FULL);
    CHECK_EQ(fat32_free_clusters(), free0);                  // everything handed back
    py_check("/tmp/s.img", "failed big write");
    // fill nearly everything, then replace the file with a smaller one
    std::vector<uint8_t> most((free0 - 8) * 512, 0xCD);
    CHECK_EQ(fat32_write_file("most.bin", most.data(), most.size()), FAT_OK);
    CHECK(fat32_free_clusters() == 8);
    CHECK_EQ(fat32_write_file("more.bin", big.data(), 100000), FAT_E_FULL);
    CHECK_EQ(fat32_write_file("most.bin", most.data(), 1000), FAT_OK);   // replace frees first
    py_check("/tmp/s.img", "nearly full");
    CHECK_EQ(fat32_mkdir("d1"), FAT_OK);
    // tiny disks are formattable; refuse absurdly small ones
    sh("rm -f /tmp/t.img; truncate -s 1M /tmp/t.img");
    open_disk("/tmp/t.img");
    CHECK_EQ(fat32_format("T", 1), FAT_E_INVAL);
}

static void test_mbr_partition() {
    printf("[10] volume inside an MBR partition\n");
    sh("python3 tools/fat32img.py create /tmp/p_inner.img 40 > /dev/null");
    sh("python3 - <<'PY'\n"
       "import struct\n"
       "inner = open('/tmp/p_inner.img','rb').read()\n"
       "start = 2048\n"
       "mbr = bytearray(512)\n"
       "mbr[446+4] = 0x0C\n"
       "struct.pack_into('<II', mbr, 446+8, start, len(inner)//512)\n"
       "mbr[510:512] = b'\\x55\\xAA'\n"
       "open('/tmp/p.img','wb').write(bytes(mbr) + bytes(512*(start-1)) + inner)\n"
       "PY");
    open_disk("/tmp/p.img");
    CHECK_EQ(fat32_mount(), FAT_OK);
    CHECK(has(list(), "readme.txt"));
    CHECK_EQ(fat32_write_file("/home/inpart.txt", "partition ok", 12), FAT_OK);
    CHECK_EQ(readback("/home/inpart.txt").size(), 12);
    // the MBR itself must be untouched and the data must have landed inside the partition
    sh("python3 - <<'PY'\n"
       "d = open('/tmp/p.img','rb').read()\n"
       "assert d[510:512] == b'\\x55\\xAA' and d[446+4] == 0x0C\n"
       "assert b'partition ok' in d[2048*512:]\n"
       "assert b'partition ok' not in d[:2048*512]\n"
       "open('/tmp/p_extract.img','wb').write(d[2048*512:])\n"
       "PY");
    py_check("/tmp/p_extract.img", "MBR partition write");
    // garbage disk is rejected
    sh("head -c 1048576 /dev/urandom > /tmp/g.img");
    open_disk("/tmp/g.img");
    CHECK_EQ(fat32_mount(), FAT_E_NOFS);
    CHECK_EQ(fat32_mkdir("x"), FAT_E_NOFS);
    CHECK_EQ(fat32_chdir("/"), FAT_E_NOFS);
}

int main() {
    test_mount_python_image();
    test_format_and_python_agrees();
    test_dirs_and_paths();
    test_files();
    test_names();
    test_directory_growth();
    test_front_end();
    test_lfn();
    test_disk_full();
    test_mbr_partition();
    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
