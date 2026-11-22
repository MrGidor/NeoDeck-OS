#include "../include/types.h"
#include "execve.h"
#include "task.h"
#include "../fs/fs.h"
#include "../memory/memory.h"
#include "../shell/string.h"
#include "../drivers/terminal.h"
#include "../drivers/keyboard.h"

int exec_spawn(const char* cmdline, bool detach) {
    /* argv strings: task_create_user copies them into the new app's own stack. */
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
    if (bin_size > APP_MEM_SIZE - APP_STACK_SIZE) {
        kprint_error("Exec Error: Executable is too large for an app's address space.\n");
        return -1;
    }

    /* Staging buffer: the task constructor copies the image into the new app's RAM. */
    int blocks = (int)((bin_size + (BLOCK_SIZE - 1)) / BLOCK_SIZE);
    uint8_t* image = (uint8_t*)kmalloc(blocks);
    if (!image) {
        kprint_error("Exec Error: Out of memory while loading the executable.\n");
        return -1;
    }
    if (fs_read_raw(filename, image, bin_size) != bin_size) {
        kfree(image, blocks);
        kprint_error("Exec Error: Could not read the whole executable from disk.\n");
        return -1;
    }

    /* Task name = file name without any directory part. */
    const char* base = filename;
    for (const char* p = filename; *p; p++) if (*p == '/') base = p + 1;

    int pid = task_create_user(base, image, bin_size, argc, argv, detach);
    kfree(image, blocks);

    if (pid == -1) kprint_error("Exec Error: Too many tasks are running.\n");
    else if (pid == -2) kprint_error("Exec Error: Not enough memory to start another task.\n");
    else if (pid < 0) kprint_error("Exec Error: Executable or arguments too large.\n");
    return pid < 0 ? -1 : pid;
}

int sys_exec(const char* cmdline) {
    keyboard_ctrl_c_clear();                     /* ignore a Ctrl+C typed before the app existed */

    int pid = exec_spawn(cmdline, false);
    if (pid < 0) return -1;

    task_set_foreground(pid);                    /* the app owns the keyboard while we wait */
    int status = 0;
    int r = task_wait(pid, &status, true);
    if (r == -2) {                               /* Ctrl+C */
        keyboard_ctrl_c_clear();
        task_kill(pid);
        kprint_warning("^C\n");
        task_wait(pid, &status, false);          /* already dead: just collect it */
    }
    task_reset_foreground();

    kprint_success("Process finished with exit status ");
    if (status < 0) { kprint_success("-"); kprint_dec((uint32_t)(-status)); }
    else kprint_dec((uint32_t)status);
    if (status >= TASK_FAULT_BASE && status < TASK_FAULT_BASE + 32) kprint_success(" (CPU exception)");
    kprint_success(".\n");
    return status;
}
