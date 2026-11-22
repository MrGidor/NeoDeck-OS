# Multitasking in NeoDeck

Several apps can be loaded at once, each in its **own physical RAM and its own
address space**, and the timer interrupt switches between them.

## Memory

```
physical (identity-mapped, supervisor-only, shared by every address space)
  0x00100000  kernel image + .bss + boot stack (idle task)
  0x00E00000  kernel page directory + page tables
  0x01000000  heap (kmalloc)  ->  per-task kernel stacks, page directories,
                                  page tables and every app's private RAM

virtual, per task (only exists in that task's page directory)
  0x40000000  APP_VIRT_BASE   [ image | .bss ... | stack (grows down) + argv ]  APP_MEM_SIZE = 512 KB
```

Every app is linked at `0x40000000` (`builtin/app/app.ld`). `task_create_user`
allocates `APP_MEM_SIZE` of heap RAM for it, zeroes it (that *is* the app's
.bss), copies the image in, builds `argc/argv` and a return trampoline at the
top, and gives the task a page directory that

* shares the kernel's identity map (no user bit -> ring 3 cannot touch it), and
* maps the window `0x40000000..+512 KB` onto the task's RAM with the user bit.

Two apps therefore have identical virtual addresses and different physical RAM
(`ps` shows the physical base). The kernel validates every pointer an app passes
to a syscall against its own window.

## Tasks (`kernel/kernel/task.cpp`)

| pid | kind | ring | notes |
|-----|------|------|-------|
| 0 | idle | 0 | `main()` after boot; runs `hlt` when nothing is ready |
| 1 | shell | 0 | kernel thread, owns the keyboard by default |
| n | apps | 3 | one per `exec` / `spawn` / `sys_spawn` |

Each task has a PCB (`task_t`): pid, state (`READY / RUNNING / BLOCKED / DEAD`),
saved kernel ESP, kernel stack (16 KB), page directory/table, app RAM, wait
reason, exit status, parent, CPU ticks. Max `TASK_MAX` = 16 tasks.

* **Context switch**: `task_switch(&old->esp, new->esp)` (`task_switch.cpp`) pushes
  ebp/ebx/esi/edi/eflags, swaps stacks, pops. A new task gets a hand-built frame
  whose "return address" is an entry stub (`task_arch.cpp`): apps `iret` into ring 3
  through a frame at the top of their kernel stack; kernel threads call their function.
* **Ring 3 and stacks**: `gdt.cpp` installs user segments and a TSS. On every
  interrupt/syscall from ring 3 the CPU switches to the task's own kernel stack
  (`TSS.ESP0`, set on every switch). The app's *user* stack is in its private RAM.
* **Policy**: round-robin. The PIT (IRQ0, 100 Hz) preempts after
  `SCHED_SLICE_TICKS` (2 = 20 ms) **only when it interrupted ring-3 code**. Kernel
  code (syscalls, the shell, the FAT32 driver) is never preempted, so it needs no
  locking. Tasks also give up the CPU by yielding, sleeping, waiting for a key or
  for another task, or exiting.
* **Blocking** is polled by the scheduler (sleep deadline, target dead, key
  available *and* caller is foreground), so wake-ups cannot be lost.
* **Cleanup**: a task that exits (or is killed, or crashes with a CPU exception)
  becomes DEAD; the *next* task to run frees its kernel stack, page tables and RAM
  (it cannot free the stack it is standing on). `spawn`ed (detached) tasks free
  their slot too; waitable ones stay as zombies until `task_wait`.
* **Crashing apps** (page fault, #GP, ...) only terminate that task, exit status
  `1000 + vector`; faults in kernel code still show the panic screen.

## Shell commands

| command | |
|---|---|
| `exec <file> [args]` | run in the foreground: it owns the keyboard, `Ctrl+C` kills it |
| `spawn <file> [args]` | run in the background, prompt returns at once |
| `ps` | list tasks (state, physical RAM base, CPU ticks) |
| `kill <pid>` | terminate an app |

Only the foreground task receives keys; `sys_get_key` returns 0 immediately for
background tasks.

## Syscalls added

`SYS_YIELD`, `SYS_GETPID`, `SYS_SPAWN(cmdline, flags)`, `SYS_WAIT(pid, &status)`,
`SYS_KILL(pid)`. `SYS_EXIT` now ends only the calling task. See
`builtin/sdk/include/neodeck_abi.h`.

## Trying it

```
./build.sh
builtin/app/build_app.sh hello ticker burner     # needs the i386-elf toolchain
python3 builtin/app/app_injector.py              # copies every builtin/app/*.bin onto the disk
./run.sh
```
(`hello.bin`, `ticker.bin` and `burner.bin` are also checked in, built for the new
`0x40000000` base.)

```
/> spawn ticker.bin A 6 300
/> spawn ticker.bin B 4 450
/> ps                                  <- two ticker tasks, different "PHYS RAM"
/> spawn burner.bin C 4
/> exec ticker.bin D 3 200             <- foreground; Ctrl+C aborts
```

Expected: A and B lines interleave (A every 300 ms, B every 450 ms), each prints
the same buffer address (`0x40...`) and `memory OK`; with `burner` running (it
never yields) the tickers keep their rhythm and the shell stays responsive,
which only works if the timer preempts ring 3.

`QEMU_DEBUG=1 ./run.sh` stops on a triple fault and logs interrupts to
`Binaries/qemu.log`.

## Tests

`tests/run_sched_tests.sh` runs the real scheduler, paging and heap code as a
32-bit Linux program (real stack switches; privileged instructions stubbed):
address-space isolation, argv/stack layout, preemption interleaving, sleep,
exit/wait/kill/fault, zombie and orphan cleanup, slot/heap exhaustion, leak checks,
and callee-saved register preservation. What it cannot cover (ring-3 `iret`, TSS,
the PIT, the keyboard IRQ) needs QEMU.

## Known limits / next steps

* One current directory shared by all tasks (`cd` in one app affects the shell).
* An app's image + .bss must fit below its stack (`APP_MEM_SIZE - APP_STACK_SIZE`
  = 480 KB); the loader cannot see .bss size in a flat binary.
* No signals, no dynamic memory syscall (apps use static buffers), no shared memory.
* For a GUI: map a framebuffer into an app window (a `sys_map_framebuffer`-style
  syscall) or add window/draw syscalls, and route input events per window instead
  of the single foreground task.
