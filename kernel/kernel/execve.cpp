#include "../include/types.h"
#include "execve.h"
#include "../fs/fs.h"
#include "../memory/memory.h"
#include "../shell/string.h"
#include "../drivers/terminal.h"

/* kernel_entry.asm: calls entry(argc, argv); returns its result, or the value
 * passed to app_exit() from anywhere (stack is unwound to this call). */
extern "C" int  app_enter(void* entry, int argc, char** argv);
extern "C" void app_exit(int status);

extern "C" uint32_t current_app_base_address;

static volatile bool app_running = false;

bool     exec_app_running() { return app_running; }
uint32_t exec_app_base()    { return current_app_base_address; }

void exec_terminate(int status) {
    if (!app_running) {
        kprint_warning("sys_exit called with no app running.\n");
        return;
    }
    app_exit(status);   /* does not return */
}

int sys_exec(const char* cmdline) {
    if (app_running) {
        kprint_error("Exec Error: an app is already running.\n");
        return -1;
    }

    /* argv strings live here; this frame stays alive while the app runs. */
    char  arg_storage[EXEC_CMDLINE_MAX];
    char* argv[EXEC_MAX_ARGS + 1];

    int n = 0;
    while (cmdline[n] && n < EXEC_CMDLINE_MAX - 1) { arg_storage[n] = cmdline[n]; n++; }
    arg_storage[n] = '\0';

    int argc = ktokenize(arg_storage, argv, EXEC_MAX_ARGS);
    if (argc == 0) {
        kprint_error("Usage: exec <file> [args...]\n");
        return -1;
    }
    const char* filename = argv[0];

    uint32_t bin_size = fs_file_size(filename);
    if (bin_size == 0) {
        kprint_error("Exec Error: Executable not found, empty, or a directory.\n");
        return -1;
    }

    /* +1 keeps a terminating NUL after the image, like the old loader did. */
    int blocks_needed = (int)((bin_size + 1 + (BLOCK_SIZE - 1)) / BLOCK_SIZE);

    uint8_t* program_space = (uint8_t*)kmalloc(blocks_needed);
    if (program_space == NULL) {
        kprint_error("Exec Error: Failed to allocate process memory.\n");
        return -1;
    }

    uint32_t got = fs_read_raw(filename, program_space, bin_size);
    if (got != bin_size) {
        kfree(program_space, blocks_needed);
        kprint_error("Exec Error: Could not read the whole executable from disk.\n");
        return -1;
    }
    program_space[bin_size] = 0;

    current_app_base_address = (uint32_t)program_space;
    app_running = true;
    int status = app_enter(program_space, argc, argv);
    app_running = false;

    kfree(program_space, blocks_needed);

    kprint_success("Process finished with exit status ");
    kprint_dec((uint32_t)status);
    kprint_success(".\n");
    return status;
}
