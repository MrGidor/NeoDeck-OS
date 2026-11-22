[bits 32]

%include "layout.inc"           ; generated from kernel/include/layout.h

global _start
global load_idt
global enable_paging
global default_stub_handler
global isr80
global isr_stub_table
global irq_stub_table

extern main
extern exception_handler
extern irq_dispatch
extern syscall_handler
extern __bss_start              ; from linker.ld
extern __bss_end

; ---------------------------------------------------------------------------
; Entry point. The bootloader copies the image to KERNEL_BASE (1 MB) and jumps
; here in 32-bit protected mode; the linker script guarantees this section
; is the very first thing in the image.
; ---------------------------------------------------------------------------
section .text.boot progbits alloc exec nowrite align=16
_start:
    cli
    lgdt [kgdt_descriptor]      ; our own GDT, inside the kernel image
    jmp 0x08:.reload_cs
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, kernel_stack_top   ; real stack, lives in .bss

    cld
    mov edi, __bss_start        ; zero .bss (nothing may touch the stack yet)
    mov ecx, __bss_end
    sub ecx, edi
    add ecx, 3
    shr ecx, 2
    xor eax, eax
    rep stosd

    call main
.hang:
    cli
    hlt
    jmp .hang

section .text

load_idt:
    mov eax, [esp + 4]
    lidt [eax]
    ret

enable_paging:
    mov eax, [esp + 4]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    ret

; ---------------------------------------------------------------------------
; CPU exceptions 0-31. Vectors 8,10-14,17,21,29,30 get an error code pushed
; by the CPU; for the rest we push a dummy 0 so the frame is always the same.
; ---------------------------------------------------------------------------
%macro ISR_NOERR 1
isr_stub_%+%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%+%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Frame seen by exception_handler (lowest address first):
;   cr2, edi,esi,ebp,esp,ebx,edx,ecx,eax, vector, error, eip, cs, eflags
isr_common:
    pusha
    mov eax, cr2
    push eax
    mov eax, esp                ; pointer to the frame
    mov esp, panic_stack_top    ; run the panic code on its own stack
    cld
    push eax
    call exception_handler
.hang:                          ; exception_handler never returns
    cli
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; Hardware IRQ 0-15 (vectors 32-47)
; ---------------------------------------------------------------------------
%macro IRQ_STUB 1
irq_stub_%+%1:
    push dword %1
    jmp irq_common
%endmacro

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

irq_common:
    pusha
    cld
    mov eax, esp                ; pointer to the saved registers (struct irq_frame)
    push eax
    mov eax, [esp + 36]         ; IRQ number: above the pusha block (32) + the push above (4)
    push eax
    call irq_dispatch           ; irq_dispatch(irq, frame)
    add esp, 8
    popa
    add esp, 4                  ; drop the IRQ number
    iret

; Any vector we have no handler for (48-255 except 0x80)
default_stub_handler:
    iret

; ---------------------------------------------------------------------------
; int 0x80 syscall gate
; ---------------------------------------------------------------------------
isr80:
    pusha
    cld
    push esp
    call syscall_handler
    add esp, 4
    popa                        ; EAX now holds the syscall result
    iret

; ---------------------------------------------------------------------------
; Tables of stub addresses for idt.cpp
; ---------------------------------------------------------------------------
section .data
align 4
isr_stub_table:
%assign i 0
%rep 32
    dd isr_stub_%+i
%assign i i+1
%endrep

irq_stub_table:
%assign i 0
%rep 16
    dd irq_stub_%+i
%assign i i+1
%endrep

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

; ---------------------------------------------------------------------------
; Stacks live in .bss (zeroed above, so the stack must not be in use then).
; ---------------------------------------------------------------------------
section .bss nobits alloc noexec write align=16
panic_stack:    resb 4096
panic_stack_top:
kernel_stack:   resb KERNEL_STACK_SIZE
kernel_stack_top:
