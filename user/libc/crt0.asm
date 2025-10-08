[bits 32]

global _start
extern main
extern _exit
extern __libc_run_ctors
extern __libc_run_dtors

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
    ;push args for main and run ctors before calling main
    push ecx                ;envp
    push ebx                ;argv
    push eax                ;argc
    call __libc_run_ctors
    call main
    add esp, 12
    ;return value in eax
    ;run dtors before exiting
    push eax                ;preserve exit code
    call __libc_run_dtors
    pop eax
    push eax
    call _exit
    ;not reached
    hlt
    jmp $
