[org 0x7c00]
KERNEL_LOCATION equ 0x1000

mov [BOOT_DISK], dl

xor ax, ax
mov es, ax
mov ds, ax
mov bp, 0x8000
mov sp, bp

mov ah, 0x0e
mov al, 'R'
xor bh, bh          ; Ensure bh = 0 (video page 0)
int 0x10

; Set up disk read destination
mov bx, KERNEL_LOCATION

mov ah, 2
mov al, 40
mov ch, 0
mov dh, 0
mov cl, 2
mov dl, [BOOT_DISK]
int 0x13
mov [DISK_RESULT], ah

; Check for disk error IMMEDIATELY before flags are ruined
jc disk_error

; Safe to print disk result now
mov ah, 0x0e
mov al, [DISK_RESULT]
add al, '0'
xor bh, bh          ; Ensure bh = 0
int 0x10


CODE_SEG equ GDT_code - GDT_start
DATA_SEG equ GDT_data - GDT_start

cli
lgdt [GDT_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp CODE_SEG:start_protected_mode

jmp $

disk_error:
    mov si, DISK_ERROR_MSG  
    call print_string
    jmp $

print_string:
    pusha
    mov ah, 0x0e
    xor bh, bh              ; Ensure bh = 0 (page 0)
.repeat:
    lodsb                   ; Loads [ds:si] into al and increments si
    cmp al, 0
    je .done
    int 0x10
    jmp .repeat
.done:
    popa
    ret

BOOT_DISK: db 0
DISK_RESULT: db 0
DISK_ERROR_MSG db "Disk read error!", 0

GDT_start:
    GDT_null:
        dd 0x0
        dd 0x0

    GDT_code:
        dw 0xffff
        dw 0x0
        db 0x0
        db 0b10011010
        db 0b11001111
        db 0x0

    GDT_data:
        dw 0xffff
        dw 0x0
        db 0x0
        db 0b10010010
        db 0b11001111
        db 0x0
GDT_end:

GDT_descriptor:
    dw GDT_end - GDT_start - 1
    dd GDT_start

[bits 32]
start_protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    jmp KERNEL_LOCATION

times 510-($-$$) db 0
dw 0xaa55