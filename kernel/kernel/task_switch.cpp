#include "../include/types.h"
#include "task_arch.h"

/*
 * Stack layout of a task that is not running (lowest address first):
 *
 *     saved ESP -> [ eflags ][ edi ][ esi ][ ebx ][ ebp ][ return address ] ...
 *
 * task_switch pushes exactly that and pops exactly that, so a brand-new task
 * is started by hand-building the same frame (see task.cpp) with the address of
 * an entry stub as the "return address".
 */
asm(
    ".pushsection .text\n"
    ".global task_switch\n"
    "task_switch:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %edx\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushfl\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popfl\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n"
    ".popsection\n"
);
