; boot.asm - A minimal bootloader for OS '미래'
; This code is assembled with NASM and loaded by the BIOS.
; It prints a welcome message on the screen using BIOS interrupts.
[org 0x7C00]       ; Bootloader is loaded at memory address 0x7C00
[bits 16]          ; 16-bit real mode

;------------------------------------------------
; Function: print_string
; Description: Print a null-terminated string using BIOS teletype.
; Input: DS:SI points to the string.
;------------------------------------------------
print_string:
    mov ah, 0x0E       ; BIOS teletype output function
.next_char:
    lodsb              ; Load next byte from DS:SI into AL
    cmp al, 0          ; Check for end of string (null terminator)
    je .done
    int 0x10           ; Call BIOS video interrupt to print character
    jmp .next_char
.done:
    ret

;------------------------------------------------
; Entry Point
;------------------------------------------------
start:
    cli                ; Disable interrupts during setup
    xor ax, ax
    mov ds, ax         ; Set DS = 0 (for simplicity)
    mov si, message    ; DS:SI now points to our message string
    call print_string  ; Print the welcome message

.hang:
    jmp .hang          ; Infinite loop to prevent bootloader from exiting

;------------------------------------------------
; Data Section
;------------------------------------------------
message db 'Welcome to 미래 OS!', 0

;------------------------------------------------
; Padding and Boot Signature
;------------------------------------------------
times 510 - ($ - $$) db 0  ; Pad the boot sector to 510 bytes
dw 0xAA55                ; Boot sector signature (must be at offset 510)
