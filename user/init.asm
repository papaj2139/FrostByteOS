[bits 32]
global _start
section .text

%define SYS_EXIT   1
%define SYS_EXECVE 11
%define SYS_WRITE  4

_start:
    ;exec /bin/forktest as it'll exec /bin/sh anyways
    mov eax, SYS_EXECVE
    mov ebx, path_forktest    ;pathname
    xor ecx, ecx        ;argv = NULL
    xor edx, edx        ;envp = NULL
    int 0x80

    ;if exec returns it failed print message and exit(1)
    push msg_fail
    call print_string
    add esp, 4

    mov eax, SYS_EXIT
    mov ebx, 1
    int 0x80

;print_string [esp+4] pointer to NUL string -> writes to fd=1
print_string:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    mov ecx, [ebp + 8]
    mov edi, ecx
    xor edx, edx
.ps_len:
    cmp byte [edi], 0
    je .ps_call
    inc edi
    inc edx
    jmp .ps_len
.ps_call:
    mov eax, SYS_WRITE
    mov ebx, 1
    int 0x80
    pop edx
    pop ecx
    pop ebx
    pop eax
    mov esp, ebp
    pop ebp
    ret

section .data
path_forktest db '/bin/forktest', 0
path_sh db '/bin/sh', 0
msg_fail db 'init: exec("/bin/sh") failed', 0x0A, 0