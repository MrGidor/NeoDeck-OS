[bits 32]
global _start
global load_idt
global isr0          ; Expose our Divide-by-Zero handler to C++
global isr21         ; Expose keyboard ISR to C++

_start:
    ; The bootloader's GDT lives at 0x7C00+, which the kernel's .bss overlaps.
    ; Switch to a GDT that is part of the kernel image before anything runs.
    lgdt [kgdt_descriptor]
    jmp 0x08:.reload_cs
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    [extern main]
    call main
    jmp $

align 8
kgdt_start:
    dd 0x0, 0x0                     ; null
    dw 0xFFFF, 0x0000               ; code: base 0, limit 4G
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000               ; data: base 0, limit 4G
    db 0x00, 10010010b, 11001111b, 0x00
kgdt_end:

kgdt_descriptor:
    dw kgdt_end - kgdt_start - 1
    dd kgdt_start

; Assembly function to physically hand the IDT pointer to the CPU
load_idt:
    mov eax, [esp + 4]  ; Get the pointer passed from C++
    lidt [eax]          ; Load Interrupt Descriptor Table register
    ret

; ISR 0: Divide by Zero Exception Handler Wrapper
isr0:
    pusha               ; Push edi, esi, ebp, esp, ebx, edx, ecx, eax
    
    [extern isr0_handler]
    call isr0_handler   ; Jump to our C++ handler logic
    
    popa                ; Restore all registers safely
    iret                ; Return from interrupt back to normal execution

; ISR 21: Keyboard Interrupt Wrapper
isr21:
    pusha
    [extern keyboard_handler]
    call keyboard_handler
    popa
    iret

global default_stub_handler

default_stub_handler:
    pusha
    [extern fallback_handler]
    call fallback_handler
    popa
    iret

global enable_paging
enable_paging:
    mov eax, [esp + 4]    ; Read the FIRST parameter directly from the stack
    mov cr3, eax          ; Set CR3 to point directly to our page directory table

    mov eax, cr0          ; Grab current CR0 layout
    or eax, 0x80000000    ; Set the Paging Enable (PG) bit 31 to 1
    mov cr0, eax          ; flip mmu
    ret

global isr80
[extern syscall_handler]

isr80:
    pusha                ; 1. Save all application registers safely (EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX)

    push esp             
    call syscall_handler 
    add esp, 4           ; 4. Clean up the passed argument off the stack loop

    popa                 ; 5. Restore registers (EAX will now hold the updated value!)
    iret