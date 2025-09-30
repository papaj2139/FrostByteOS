 section .multiboot
align 4
    dd 0x1BADB002              ; magic
    dd 0x07                    ; flags: align modules | mem info | request video
    dd -(0x1BADB002 + 0x07)    ; checksum
    dd 0x00000001              ; video mode type: graphics
    dd 1024                    ; width
    dd 768                     ; height
    dd 32                      ; bpp

section .text
global start
extern kmain

start:
    push ebx
    push eax
    call kmain
    add esp, 8

    cli
    hlt
