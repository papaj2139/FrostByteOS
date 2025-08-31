
[bits 32]
[org 0x1000000]

%define SYS_CREAT 8
%define SYS_WRITE 4
%define SYS_CLOSE 6
%define SYS_OPEN 5
%define SYS_READ 3
%define SYS_EXIT 1

_start:
    ;try to open the file for writing first
    mov eax, SYS_OPEN
    mov ebx, file_name
    mov ecx, 1 ; O_WRONLY flag
    int 0x80
    mov [file_descriptor], eax
    
    ;check if open succeeded
    cmp eax, -1
    jne .write_file
    
    ;if open failed create the file
    mov eax, SYS_CREAT
    mov ebx, file_name
    mov ecx, 0 ; mode
    int 0x80
    mov [file_descriptor], eax
    
    ;if create also failed skip to reading existing file
    cmp eax, -1
    je .open_for_read

.write_file:
    ;write to the file
    mov eax, SYS_WRITE
    mov ebx, [file_descriptor]
    mov ecx, msg_hello
    mov edx, len_hello
    int 0x80

    ;close the file
    mov eax, SYS_CLOSE
    mov ebx, [file_descriptor]
    int 0x80

.open_for_read:
    ;open the file for reading
    mov eax, SYS_OPEN
    mov ebx, file_name
    mov ecx, 0 ; flags
    int 0x80
    mov [file_descriptor], eax

    ;read from the file
    mov eax, SYS_READ
    mov ebx, [file_descriptor]
    mov ecx, read_buffer
    mov edx, 100
    int 0x80

    ;write file content to stdout
    mov eax, SYS_WRITE
    mov ebx, 1 ; stdout
    mov ecx, read_buffer
    mov edx, len_hello
    int 0x80

    ;close the file
    mov eax, SYS_CLOSE
    mov ebx, [file_descriptor]
    int 0x80

    ;exit
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80

section .data
    msg_hello db 'Hello from user app', 0x0A, 0
    len_hello equ $ - msg_hello
    file_name db '/TEST.TXT', 0

section .bss
    file_descriptor resd 1
    read_buffer resb 100