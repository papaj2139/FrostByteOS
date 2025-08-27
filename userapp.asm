
[bits 32]
[org 0x1000000]

_start:
    mov eax, 4          ;SYS_WRITE
    
    ;file descriptor
    mov ebx, 1          ;fd = 1 (stdout)
    
    ;address of the string to write
    mov ecx, msg_hello  ;buf = address of msg_hello
    
    ;number of bytes tow rite
    mov edx, len_hello  ;length of msg_hello
    
    ;syscall interrupt
    int 0x80

    mov eax, 1          ;SYS_EXIT
    
    mov ebx, 0          
    
    ;syscall interrupt
    int 0x80
    
section .data
msg_hello db 'Hello, world!', 0x0A, 0x00
len_hello equ $ - msg_hello               ;lenth of string