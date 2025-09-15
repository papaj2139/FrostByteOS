section .text

global context_switch_asm
global switch_to_user_mode

context_switch_asm:
    push ebp
    mov ebp, esp
    
    ;get parameters
    mov eax, [ebp + 8]  ;old_context
    mov edx, [ebp + 12] ;new_context
    
    ;save current context to old_context
    test eax, eax
    jz .restore_new     ;skip save if old_context is NULL
    
    ;save general purpose registers
    mov [eax + 0],  eax     ;save eax
    mov [eax + 4],  ebx     ;save ebx
    mov [eax + 8],  ecx     ;save ecx
    mov [eax + 12], edx     ;save edx (will be overwritten save original)
    mov [eax + 16], esi     ;save esi
    mov [eax + 20], edi     ;save edi
    mov [eax + 24], esp     ;save esp
    mov [eax + 28], ebp     ;save ebp
    
    ;save eip (return address)
    mov ecx, [ebp + 4]      ;get return address from stack
    mov [eax + 32], ecx     ;save as eip
    
    ;save eflags
    pushfd
    pop ecx
    mov [eax + 36], ecx
    
    ;save segment registers (16-bit values)
    mov cx, cs
    mov [eax + 40], ecx
    mov cx, ds
    mov [eax + 44], ecx
    mov cx, es
    mov [eax + 48], ecx
    mov cx, fs
    mov [eax + 52], ecx
    mov cx, gs
    mov [eax + 56], ecx
    mov cx, ss
    mov [eax + 60], ecx

.restore_new:
    ;restore context from new_context
    test edx, edx
    jz .done            ;skip restore if new_context is NULL
    
    ;restore segment registers first
    mov ecx, [edx + 44]  ; ds
    mov ds, cx
    mov ecx, [edx + 48]  ; es
    mov es, cx
    mov ecx, [edx + 52]  ; fs
    mov fs, cx
    mov ecx, [edx + 56]  ; gs
    mov gs, cx
    ;don't restore SS yet need the stack
    
    ;restore eflags
    mov ecx, [edx + 36]
    push ecx
    popfd
    
    ;restore stack pointer and segment
    mov ecx, [edx + 60]  ;ss
    mov ss, cx
    mov esp, [edx + 24]  ;esp
    
    ;restore general purpose registers
    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    mov ebp, [edx + 28]
    
    ;get the saved eip and edx
    push dword [edx + 32]  ;push eip onto stack
    mov edx, [edx + 12]    ;restore edx
    
    ;jump to new eip
    ret                    ;return to pushed eip

.done:
    pop ebp
    ret

;switches from kernel mode to user mode
switch_to_user_mode:
    cli                     ;disable interrupts during switch
    
    mov eax, [esp + 4]      ;get eip parameter
    mov ebx, [esp + 8]      ;get esp parameter
    
    ;set up stack for iret to user mode
    ;stack layout for iret: SS, ESP, EFLAGS, CS, EIP
    
    push 0x23               ;user data segment (SS) - GDT entry 4, RPL=3
    push ebx                ;user ESP
    pushfd                  ;current EFLAGS
    or dword [esp], 0x200   ;ensure interrupts enabled in user mode
    push 0x1B               ;user code segment (CS) - GDT entry 3, RPL=3  
    push eax                ;user EIP
    
    ;setup user data segments
    mov ax, 0x23            ;user data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ;switch to user mode
    iret

;helper function to save current context (used by scheduler)
global save_current_context
save_current_context:
    push ebp
    mov ebp, esp
    
    mov eax, [ebp + 8]      ;get context pointer
    
    ;save all registers
    mov [eax + 0],  eax
    mov [eax + 4],  ebx
    mov [eax + 8],  ecx
    mov [eax + 12], edx
    mov [eax + 16], esi
    mov [eax + 20], edi
    mov [eax + 24], esp
    mov [eax + 28], ebp
    
    ;save return address as eip
    mov ecx, [ebp + 4]
    mov [eax + 32], ecx
    
    ;save eflags
    pushfd
    pop ecx
    mov [eax + 36], ecx
    
    ;save segments
    mov cx, cs
    mov [eax + 40], cx
    mov cx, ds
    mov [eax + 44], cx
    mov cx, es
    mov [eax + 48], cx
    mov cx, fs
    mov [eax + 52], cx
    mov cx, gs
    mov [eax + 56], cx
    mov cx, ss
    mov [eax + 60], cx
    
    pop ebp
    ret
