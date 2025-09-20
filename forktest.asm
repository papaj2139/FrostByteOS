[bits 32]
[org 0x1000000]

%define SYS_EXIT    1
%define SYS_FORK    2
%define SYS_WAIT    7
%define SYS_WRITE   4
%define SYS_GETPID  20
%define SYS_SLEEP   162
%define SYS_EXECVE  11

_start:
    ;print banner
    mov ecx, msg_start
    call print_string

    ;fork
    mov eax, SYS_FORK
    int 0x80
    cmp eax, 0
    je .child
    jl .fork_err

    ;parent path: EAX = child pid
    mov [child_pid], eax
    mov ecx, msg_parent1
    call print_string
    mov eax, [child_pid]
    call print_dec
    mov ecx, newline
    call print_string

    ;wait for child
    mov eax, SYS_WAIT
    mov ebx, status
    int 0x80
    mov [wait_pid], eax

    ;print wait result
    mov ecx, msg_parent2
    call print_string
    mov eax, [wait_pid]
    call print_dec
    mov ecx, msg_parent3
    call print_string
    mov eax, [status]
    call print_dec
    mov ecx, newline
    call print_string

    ;execve("/bin/sh", NULL, NULL)
    mov eax, SYS_EXECVE
    mov ebx, path_sh
    xor ecx, ecx
    xor edx, edx
    int 0x80

    ;if exec fails then exit(1)
    mov eax, SYS_EXIT
    mov ebx, 1
    int 0x80

.child:
    ;child print pid
    mov eax, SYS_GETPID
    int 0x80
    mov [self_pid], eax
    mov ecx, msg_child
    call print_string
    mov eax, [self_pid]
    call print_dec
    mov ecx, newline
    call print_string

    ;sleep 1 second
    mov eax, SYS_SLEEP
    mov ebx, 1
    int 0x80

    ;exit(42)
    mov eax, SYS_EXIT
    mov ebx, 42
    int 0x80

.fork_err:
    mov ecx, msg_forkerr
    call print_string
    mov eax, SYS_EXIT
    mov ebx, 1
    int 0x80

;print_string ECX=ptr to NUL string
print_string:
    push eax
    push ebx
    push edx
    mov eax, SYS_WRITE
    mov ebx, 1
    mov edx, 0
.str_len:
    cmp byte [ecx + edx], 0
    je .str_go
    inc edx
    jmp .str_len
.str_go:
    int 0x80
    pop edx
    pop ebx
    pop eax
    ret

;print_dec EAX=value
print_dec:
    push eax
    push ebx
    push ecx
    push edx
    mov ecx, num_buf + 11
    mov byte [ecx], 0
    mov ebx, 10
    cmp eax, 0
    jne .pd_loop
    dec ecx
    mov byte [ecx], '0'
    jmp .pd_done
.pd_loop:
    xor edx, edx
    div ebx            ;EAX=EAX/10 EDX=EAX%10
    add dl, '0'
    dec ecx
    mov [ecx], dl
    test eax, eax
    jnz .pd_loop
.pd_done:
    mov eax, SYS_WRITE
    mov ebx, 1
    mov edx, 0
.len:
    cmp byte [ecx + edx], 0
    je .out
    inc edx
    jmp .len
.out:
    int 0x80
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

section .bss
status    resd 1
child_pid resd 1
wait_pid  resd 1
self_pid  resd 1
num_buf   resb 12

section .data
msg_start   db 'forktest: starting', 0x0A, 0
msg_parent1 db 'parent: forked PID ', 0
msg_parent2 db 'parent: waited PID ', 0
msg_parent3 db ' with status ', 0
msg_child   db 'child: pid ', 0
msg_forkerr db 'forktest: fork failed', 0x0A, 0
newline     db 0x0A, 0
path_sh     db '/bin/sh', 0
