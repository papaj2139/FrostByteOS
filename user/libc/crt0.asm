[bits 32]

global _start
extern main
extern _exit

section .text
_start:
    ;stack layout on entry:
    ;[esp + 0] = argc
    ;[esp + 4] = argv[0]
    ;[esp + 8] = argv[1]
    ;...
    ;[esp + 4 + 4*argc] = NULL
    ;[then envp vector...] followed by NULL
    mov eax, [esp]          ;argc
    lea ebx, [esp + 4]      ;argv = &argv[0]
    mov ecx, eax            ;ecx = argc
    inc ecx                 ;argc + 1 (for argv NULL)
    shl ecx, 2              ;(argc + 1) * 4
    add ecx, ebx            ;envp = &argv[argc + 1]
    push ecx
    push ebx
    push eax
    call main
    add esp, 12
    ;return value in eax
    push eax
    call _exit
    ;not reached
    hlt
    jmp $
