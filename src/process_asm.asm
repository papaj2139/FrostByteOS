section .text

global context_switch_asm
global switch_to_user_mode
global enter_user_with_pd

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

    ;restore data segment registers first (from context)
    mov ecx, [edx + 44]  ; ds
    mov ds, cx
    mov ecx, [edx + 48]  ; es
    mov es, cx
    mov ecx, [edx + 52]  ; fs
    mov fs, cx
    mov ecx, [edx + 56]  ; gs
    mov gs, cx

    ;determine target privilege level from CS selector in context
    mov ecx, [edx + 40]   ; cs
    test ecx, 3
    jnz .to_user          ;if RPL!=0, switch to user with iret

    ;kernel target (RPL=0): restore SS:ESP, EFLAGS, GPRs then near ret
    mov ecx, [edx + 60]   ; ss
    mov ss, cx
    mov esp, [edx + 24]   ; esp

    ;restore general purpose registers
    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    mov ebp, [edx + 28]

    ;restore EFLAGS
    push dword [edx + 36]
    popfd

    ;return to saved EIP in same privilege
    push dword [edx + 32]
    mov edx, [edx + 12]   ;restore EDX last
    ret

.to_user:
    ;disable interrupts while building the iret frame
    cli
    ;build an iret frame for a ring transition to user mode on the CURRENT (kernel) stack
    ;order expected by iret with CPL change: EIP, CS, EFLAGS, ESP, SS (push in reverse)
    push dword [edx + 60]      ; SS (user data)
    push dword [edx + 24]      ; ESP (user stack)
    push dword [edx + 36]      ; EFLAGS
    push dword [edx + 40]      ; CS (user code)
    push dword [edx + 32]      ; EIP (user entry)

    ;restore minimal register (EBP) before iret
    mov ebp, [edx + 28]

    ;ensure user data segments are loaded before dropping to CPL=3
    mov ax, 0x23            ;user data selector 
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;do not restore GPRs here; let user code set them and avoid clobbering

    ;perform the privilege-level switch
    iretd

.done:
    pop ebp
    ret

;atomically switch to a given page directory and enter user mode at (EIP, ESP)
;args (cdecl): [esp+4]=dir_phys, [esp+8]=user_eip, [esp+12]=user_esp
enter_user_with_pd:
    cli
    mov eax, [esp + 4]      ;dir_phys
    mov cr3, eax            ;switch to target page directory

    mov ecx, [esp + 8]      ;user EIP
    mov edx, [esp + 12]     ;user ESP

    ;load user data segments before iret
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;build iret frame: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23         ;SS (user data)
    push edx                ;ESP (user stack)
    pushfd                  ;EFLAGS
    pop eax
    and eax, ~0x200         ;IF=0 in user mode for first entry
    push eax
    push dword 0x1B         ;CS (user code)
    push ecx                ;EIP (user entry)
    iretd

;switches from kernel mode to user mode
switch_to_user_mode:
    cli                     ;disable interrupts during switch

    mov ecx, [esp + 4]      ;get EIP parameter into ECX
    mov edx, [esp + 8]      ;get ESP parameter into EDX

    ;setup user data segments before iret (use AX only)
    mov ax, 0x23            ;user data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;build iret frame: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23         ;SS (user data)
    push edx                ;ESP (user stack)
    pushfd                  ;EFLAGS
    pop eax
    or eax, 0x200           ;IF=1 in user mode (restore stable behavior)
    push eax
    push dword 0x1B         ;CS (user code)
    push ecx                ;EIP (user entry)
    iretd

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
