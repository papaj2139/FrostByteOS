section .text

global context_switch_asm
global switch_to_user_mode
global enter_user_with_pd

context_switch_asm:
    push ebp
    mov ebp, esp
    
    ;get parameters
    mov esi, [ebp + 8]  ;old_context
    mov edi, [ebp + 12] ;new_context
    
    ;save current context to old_context (if provided)
    test esi, esi
    jz .restore_new     ;skip save if old_context is NULL
    
    ;push all GPRs to capture their original values
    pushad                  ;pushes EAX, ECX, EDX, EBX, ESP(before), EBP, ESI, EDI
    
    ;after pushad layout at [esp+offset]:
    ; 0: EDI, 4: ESI, 8: EBP, 12: saved ESP, 16: EBX, 20: EDX, 24: ECX, 28: EAX
    ;store into cpu_context_t in order: eax, ebx, ecx, edx, esi, edi, esp, ebp
    mov eax, [esp + 28]     ;EAX
    mov [esi + 0], eax
    mov eax, [esp + 16]     ;EBX
    mov [esi + 4], eax
    mov eax, [esp + 24]     ;ECX
    mov [esi + 8], eax
    mov eax, [esp + 20]     ;EDX
    mov [esi + 12], eax
    mov eax, [esp + 4]      ;ESI
    mov [esi + 16], eax
    mov eax, [esp + 0]      ;EDI
    mov [esi + 20], eax
    mov eax, [esp + 12]     ;original ESP (before pushad)
    mov [esi + 24], eax
    mov eax, [esp + 8]      ;EBP
    mov [esi + 28], eax
    
    ;save eip (return address)
    mov ecx, [ebp + 4]      ;get return address from stack
    mov [esi + 32], ecx     ;save as eip
    
    ;save eflags
    pushfd
    pop ecx
    mov [esi + 36], ecx
    
    ;save segment registers (16-bit values)
    xor ecx, ecx
    mov cx, cs
    mov [esi + 40], ecx
    xor ecx, ecx
    mov cx, ds
    mov [esi + 44], ecx
    xor ecx, ecx
    mov cx, es
    mov [esi + 48], ecx
    xor ecx, ecx
    mov cx, fs
    mov [esi + 52], ecx
    xor ecx, ecx
    mov cx, gs
    mov [esi + 56], ecx
    xor ecx, ecx
    mov cx, ss
    mov [esi + 60], ecx

    ;unwind pushad
    add esp, 32

.restore_new:
    ;restore context from new_context
    test edi, edi
    jz .done            ;skip restore if new_context is NULL

    ;restore data segment registers first (from context)
    mov ecx, [edi + 44]  ;ds
    mov ds, cx
    mov ecx, [edi + 48]  ;es
    mov es, cx
    mov ecx, [edi + 52]  ;fs
    mov fs, cx
    mov ecx, [edi + 56]  ;gs
    mov gs, cx

    ;determine target privilege level from CS selector in context
    mov ecx, [edi + 40]   ;cs
    test ecx, 3
    jnz .to_user          ;if RPL!=0, switch to user with iret

    ;kernel target (RPL=0): restore SS:ESP, EFLAGS, GPRs then near ret
    ;use EDX as a stable base pointer to the context to avoid clobbering EDI early
    mov edx, edi
    mov ecx, [edx + 60]   ;ss
    mov ss, cx
    mov esp, [edx + 24]   ;esp

    ;restore general purpose registers (read from EDX base)
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
    push dword [edi + 60]      ;SS (user data)
    push dword [edi + 24]      ;ESP (user stack)
    push dword [edi + 36]      ;EFLAGS
    push dword [edi + 40]      ;CS (user code)
    push dword [edi + 32]      ;EIP (user entry)

    ;use EDX as a stable base pointer to context while restoring GPRs
    mov edx, edi

    ;restore minimal register (EBP) before iret
    mov ebp, [edx + 28]

    ;ensure user data segments are loaded before dropping to CPL=3
    mov ax, 0x23            ;user data selector 
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;restore general purpose registers from saved user context
    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    mov edx, [edx + 12]     ;restore EDX last (base no longer needed)

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
