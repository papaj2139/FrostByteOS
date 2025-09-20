[bits 32]
[org 0x1000000]

%define SYS_WRITE 4
%define SYS_READ  3
%define SYS_EXIT  1
%define SYS_IOCTL 54

;TTY ioctl and mode bits
%define TTY_IOCTL_SET_MODE 1
%define TTY_IOCTL_GET_MODE 2
%define TTY_MODE_CANON     1
%define TTY_MODE_ECHO      2

_start:
    ;enter the shell main loop
    call shell_main
    ;if shell_main ever returns exit
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80

;prints a string ECX=ptr to NUL-terminated string writes to fd=1
print_string:
    push eax
    push ebx
    push ecx
    push edx
    mov esi, ecx
    mov edi, esi
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
    mov ecx, esi
    int 0x80
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

;ECX=buffer, EDX=size; returns EAX=bytes read (line-terminated)
read_line:
    push ebx
    mov eax, SYS_READ
    mov ebx, 0
    int 0x80
    pop ebx
    ret

;remove trailing '\n' or '\r' if present ECX=buf EAX=len
chomp:
    push ebx
    test eax, eax
    jz .c_done
    dec eax
    js .c_done
    mov bl, [ecx + eax]
    cmp bl, 0x0A
    je .c_set0
    cmp bl, 0x0D
    je .c_set0
    jmp .c_done
.c_set0:
    mov byte [ecx + eax], 0
.c_done:
    pop ebx
    ret

;compare ECX buffer with ESI const returns EAX=1 if equal 0 otherwise
strcmp:
    push ecx
    push esi
.sc_loop:
    mov al, [ecx]
    mov bl, [esi]
    cmp al, bl
    jne .sc_ne
    test al, al
    je .sc_eq
    inc ecx
    inc esi
    jmp .sc_loop
.sc_eq:
    mov eax, 1
    jmp .sc_ret
.sc_ne:
    xor eax, eax
.sc_ret:
    pop esi
    pop ecx
    ret

;check if ECX buffer starts with ESI prefix EAX=1 if yes EDX=rest
startswith:
    push ecx
    push esi
.sw_loop:
    mov al, [esi]
    test al, al
    je .sw_ok
    mov bl, [ecx]
    cmp bl, al
    jne .sw_no
    inc ecx
    inc esi
    jmp .sw_loop
.sw_ok:
    mov eax, 1
    mov edx, ecx
    pop esi
    pop ecx
    ret
.sw_no:
    xor eax, eax
    pop esi
    pop ecx
    ret

;prints a prompt reads a line handles help/echo/exit
shell_main:
    mov ecx, welcome
    call print_string

.loop:
    mov ecx, prompt
    call print_string

    mov ecx, input_buf
    mov edx, 255
    call read_line          ;EAX = bytes read
    mov ecx, input_buf
    call chomp

    ; skip leading spaces
    mov ecx, input_buf
.skip:
    cmp byte [ecx], ' '
    jne .check_empty
    inc ecx
    jmp .skip

.check_empty:
    cmp byte [ecx], 0
    je .loop

    ;exit
    mov esi, cmd_exit
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    je .do_exit

    ;help
    mov esi, cmd_help
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    je .do_help

    ;stty (mode control)
    mov esi, cmd_stty
    push ecx
    call startswith
    pop ecx
    cmp eax, 1
    jne .echo_cmd
    ;EDX = rest of line after 'stty '
    push edx
    call handle_stty
    add esp, 4
    jmp .loop

.echo_cmd:
    ;echo
    mov esi, cmd_echo
    push ecx
    call startswith
    pop ecx
    cmp eax, 1
    jne .unknown
    mov ecx, edx            ;rest of line
    call print_string
    mov ecx, newline
    call print_string
    jmp .loop

.do_help:
    mov ecx, help_text
    call print_string
    jmp .loop

.do_exit:
    mov ecx, bye_text
    call print_string
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80

.unknown:
    mov ecx, unknown_text
    call print_string
    mov ecx, input_buf
    call print_string
    mov ecx, newline
    call print_string
    jmp .loop


handle_stty:
    push ebp
    mov ebp, esp
    mov ecx, [ebp + 8]    ; rest
    ;skip leading spaces
.hs_skip:
    cmp byte [ecx], ' '
    jne .hs_check
    inc ecx
    jmp .hs_skip
.hs_check:
    ;stty raw
    mov esi, arg_raw
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    jne .hs_canon
    ;mode = 0 (raw no echo)
    mov dword [mode_val], 0
    push dword mode_val
    push dword TTY_IOCTL_SET_MODE
    call tty_ioctl_set
    add esp, 8
    mov ecx, msg_raw
    call print_string
    jmp .hs_done

.hs_canon:
    mov esi, arg_canon
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    jne .hs_echo
    ;mode = CANON|ECHO
    mov dword [mode_val], TTY_MODE_CANON | TTY_MODE_ECHO
    push dword mode_val
    push dword TTY_IOCTL_SET_MODE
    call tty_ioctl_set
    add esp, 8
    mov ecx, msg_canon
    call print_string
    jmp .hs_done

.hs_echo:
    mov esi, arg_echo_on
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    je .hs_echo_on
    mov esi, arg_echo_off
    push ecx
    call strcmp
    pop ecx
    cmp eax, 1
    jne .hs_unknown
    ;echo off: get mode clear ECHO and  set
    push dword mode_val
    push dword TTY_IOCTL_GET_MODE
    call tty_ioctl_get
    add esp, 8
    mov eax, [mode_val]
    and eax, ~(TTY_MODE_ECHO)
    mov [mode_val], eax
    push dword mode_val
    push dword TTY_IOCTL_SET_MODE
    call tty_ioctl_set
    add esp, 8
    mov ecx, msg_echo_off
    call print_string
    jmp .hs_done

.hs_echo_on:
    ;echo on get mode set ECHO set
    push dword mode_val
    push dword TTY_IOCTL_GET_MODE
    call tty_ioctl_get
    add esp, 8
    mov eax, [mode_val]
    or eax, TTY_MODE_ECHO
    mov [mode_val], eax
    push dword mode_val
    push dword TTY_IOCTL_SET_MODE
    call tty_ioctl_set
    add esp, 8
    mov ecx, msg_echo_on
    call print_string
    jmp .hs_done

.hs_unknown:
    mov ecx, msg_stty_usage
    call print_string

.hs_done:
    mov esp, ebp
    pop ebp
    ret

;args: [esp+4]=cmd [esp+8]=ptr
tty_ioctl_set:
    push ebp
    mov ebp, esp
    mov eax, SYS_IOCTL
    mov ebx, 0                ;fd 0 (stdin/tty)
    mov ecx, [ebp + 8]        ;cmd
    mov edx, [ebp + 12]       ;arg ptr
    int 0x80
    mov esp, ebp
    pop ebp
    ret

;args: [esp+4]=cmd [esp+8]=ptr
tty_ioctl_get:
    push ebp
    mov ebp, esp
    mov eax, SYS_IOCTL
    mov ebx, 0
    mov ecx, [ebp + 8]
    mov edx, [ebp + 12]
    int 0x80
    mov esp, ebp
    pop ebp
    ret

section .bss
    input_buf resb 256
    mode_val  resd 1

section .data
    welcome      db 'FrostByte Shell', 0x0A, 'Type: help, echo <text>, exit', 0x0A, 0
    prompt       db 'fbsh> ', 0
    newline      db 0x0A, 0
    help_text    db 'Commands:', 0x0A, '  help', 0x0A, '  echo <text>', 0x0A, '  exit', 0x0A, 0
    bye_text     db 'Bye!', 0x0A, 0
    unknown_text db 'Unknown command: ', 0
    cmd_exit     db 'exit', 0
    cmd_help     db 'help', 0
    cmd_echo     db 'echo ', 0
    cmd_stty     db 'stty ', 0
    arg_raw      db 'raw', 0
    arg_canon    db 'canon', 0
    arg_echo_on  db 'echo on', 0
    arg_echo_off db 'echo off', 0
    msg_raw      db 0x0A, '[stty] raw mode (no echo)', 0x0A, 0
    msg_canon    db 0x0A, '[stty] canonical mode with echo', 0x0A, 0
    msg_echo_on  db 0x0A, '[stty] echo on', 0x0A, 0
    msg_echo_off db 0x0A, '[stty] echo off', 0x0A, 0
    msg_stty_usage db 0x0A, 'Usage: stty raw | stty canon | stty echo on|off', 0x0A, 0
