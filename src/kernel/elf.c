#include "elf.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../process.h"
#include <string.h>
#include <stddef.h>

//expose syscall exit marker so exec can jump straight to user without returning
extern void syscall_mark_exit(void);
extern void switch_to_user_mode(uint32_t eip, uint32_t esp);

#define EI_NIDENT 16

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

//e_ident indices
#define EI_MAG0   0
#define EI_MAG1   1
#define EI_MAG2   2
#define EI_MAG3   3
#define EI_CLASS  4
#define EI_DATA   5
#define EI_VERSION 6

//constants
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

//build user stack in SysV style:
//[esp + 0] = argc
//[esp + 4] = argv[0]
//[esp + 8] = argv[1]
//...
//[esp + 4 + 4*argc] = NULL
//[then envp pointers..] followed by NULL
static int build_user_stack(page_directory_t new_dir,
                            uint32_t ustack_top,
                            uint32_t new_stack_phys,
                            char* const argv[], char* const envp[],
                            uint32_t* out_esp) {
    (void)new_dir; //unused stack page is mapped via kernel temp mapping
    const uint32_t ustack_va = ustack_top - 0x1000;
    const uint32_t temp_kmap = 0x00800000;

    //map stack page into kernel temporarily and zero it
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    if (vmm_map_page(temp_kmap, new_stack_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    memset((void*)temp_kmap, 0, PAGE_SIZE);

    //count argc/envc
    int argc = 0; if (argv) { while (argv[argc]) argc++; }
    int envc = 0; if (envp) { while (envp[envc]) envc++; }
    serial_write_string("[ELF] build_user_stack argc=\n");
    serial_printf("%d", argc);
    serial_write_string(" envc=\n");
    serial_printf("%d", envc);
    serial_write_string("\n");
    if (argc > 0 && argv && argv[0]) {
        serial_write_string("[ELF] argv0=\"\n");
        serial_write_string(argv[0]);
        serial_write_string("\"\n");
    }

    //compute total bytes needed within the top page
    uint32_t strings_size = 0;
    for (int i = 0; i < argc; i++) strings_size += (uint32_t)strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++) strings_size += (uint32_t)strlen(envp[i]) + 1;
    uint32_t argv_vec_bytes = 4u * ((uint32_t)argc + 1u);
    uint32_t envp_vec_bytes = 4u * ((uint32_t)envc + 1u);
    //plus one word for argc
    if (strings_size + argv_vec_bytes + envp_vec_bytes + 4u + 16u > 4096u) {
        vmm_unmap_page_nofree(temp_kmap);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }

    //store string addresses as we place them
    uint32_t* argv_user = (argc > 0) ? (uint32_t*)kmalloc(sizeof(uint32_t) * (uint32_t)argc) : NULL;
    uint32_t* envp_user = (envc > 0) ? (uint32_t*)kmalloc(sizeof(uint32_t) * (uint32_t)envc) : NULL;

    uint32_t sp = ustack_top;

    //copy envp strings first (top-most)
    for (int i = envc - 1; i >= 0; i--) {
        const char* s = envp[i];
        uint32_t len = (uint32_t)strlen(s) + 1;
        sp -= len;
        memcpy((void*)(temp_kmap + (sp - ustack_va)), s, len);
        envp_user[i] = sp;
    }
    //copy argv strings
    for (int i = argc - 1; i >= 0; i--) {
        const char* s = argv[i];
        uint32_t len = (uint32_t)strlen(s) + 1;
        sp -= len;
        memcpy((void*)(temp_kmap + (sp - ustack_va)), s, len);
        argv_user[i] = sp;
    }

    //align stack pointer for vectors and argc
    sp &= ~0xFu;

    //determine where vectors and argc will live
    uint32_t vec_total = argv_vec_bytes + envp_vec_bytes;
    uint32_t vec_base = sp - vec_total; //argv vector at vec_base, envp vector immediately after
    uint32_t argv_vec_va = vec_base;
    uint32_t envp_vec_va = vec_base + argv_vec_bytes;
    //SysV i386 ABI: argv[] must begin immediately after argc at [esp+4]
    //so do not insert padding between argc and the argv vector
    uint32_t esp0 = vec_base - 4u; //argc at esp0, argv[0] at esp0+4

    //fill argv vector
    for (uint32_t i = 0; i < (uint32_t)argc; i++) {
        *(uint32_t*)(temp_kmap + (argv_vec_va - ustack_va) + i * 4u) = argv_user[i];
    }
    *(uint32_t*)(temp_kmap + (argv_vec_va - ustack_va) + (uint32_t)argc * 4u) = 0; //NULL

    //fill envp vector
    for (uint32_t i = 0; i < (uint32_t)envc; i++) {
        *(uint32_t*)(temp_kmap + (envp_vec_va - ustack_va) + i * 4u) = envp_user[i];
    }
    *(uint32_t*)(temp_kmap + (envp_vec_va - ustack_va) + (uint32_t)envc * 4u) = 0; //NULL

    //write argc at esp0
    *(uint32_t*)(temp_kmap + (esp0 - ustack_va)) = (uint32_t)argc;

    if (argv_user) kfree(argv_user);
    if (envp_user) kfree(envp_user);

    vmm_unmap_page_nofree(temp_kmap);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

    *out_esp = esp0;
    return 0;
}

//load ELF into a specific process address space set its entry/stack don't switch
int elf_load_into_process(const char* pathname, struct process* proc,
                          char* const argv[], char* const envp[]) {
    if (!pathname || !proc) return -1;
    vfs_node_t* node = vfs_open(pathname, VFS_FLAG_READ);
    if (!node) return -1;

    Elf32_Ehdr eh;
    int r = vfs_read(node, 0, sizeof(eh), (char*)&eh);
    serial_write_string("[ELF] read header bytes=\n");
    serial_printf("%d", r);
    serial_write_string("\n");
    if (r != (int)sizeof(eh)) { vfs_close(node); serial_write_string("[ELF] header read short\n"); return -2; }
    if (!(eh.e_ident[EI_MAG0] == ELFMAG0 && eh.e_ident[EI_MAG1] == ELFMAG1 &&
          eh.e_ident[EI_MAG2] == ELFMAG2 && eh.e_ident[EI_MAG3] == ELFMAG3)) {
            vfs_close(node);
            return -2; }
    if (eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        vfs_close(node);
        return -2; }
    if (eh.e_type != ET_EXEC || eh.e_machine != EM_386 || eh.e_version != EV_CURRENT) {
        vfs_close(node);
        return -2;
    }
    if (eh.e_phoff == 0 || eh.e_phnum == 0) {
        vfs_close(node);
        return -2;
    }

    page_directory_t dir = proc->page_directory;
    if (!dir) {
        dir = vmm_create_directory();
        if (!dir) { vfs_close(node); return -1; }
        vmm_map_kernel_space(dir);
        vmm_map_page_in_directory(dir, 0x000B8000, 0x000B8000, PAGE_PRESENT | PAGE_WRITABLE);
        proc->page_directory = dir;
    }

    //unmap any existing mapping at future stack VA to free its phys
    const uint32_t ustack_top = 0x02000000;
    const uint32_t ustack_va = ustack_top - 0x1000;
    page_directory_t saved_cr3_dir = vmm_get_kernel_directory();
    //switch to target directory to query physical addresses for cleanup
    vmm_switch_directory(dir);
    uint32_t old_stack_phys = vmm_get_physical_addr(ustack_va) & ~0xFFF;
    //switch back to kernel directory for safe temp mappings
    vmm_switch_directory(saved_cr3_dir);

    //load PT_LOAD segments
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;
        Elf32_Off off = eh.e_phoff + (Elf32_Off)i * (Elf32_Off)eh.e_phentsize;
        r = vfs_read(node, off, sizeof(ph), (char*)&ph);
        if (r != (int)sizeof(ph)) {
            vfs_close(node);
            return -1; }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_memsz == 0) continue;

        uint32_t seg_start = ph.p_vaddr & ~0xFFFu;
        uint32_t seg_end   = (ph.p_vaddr + ph.p_memsz + 0xFFFu) & ~0xFFFu;
        uint32_t file_remaining = ph.p_filesz;
        uint32_t file_cursor = 0;
        for (uint32_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_page();
            if (!phys) { vfs_close(node); return -1; }
            uint32_t flags = PAGE_PRESENT | PAGE_USER;
            if (ph.p_flags & PF_W) flags |= PAGE_WRITABLE;
            if (vmm_map_page_in_directory(dir, va, phys, flags) != 0) {
                vfs_close(node);
                return -1;
            }

            uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
            const uint32_t TMP = 0x00800000;
            if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                if (eflags_save & 0x200) __asm__ volatile ("sti");
                vfs_close(node); return -1;
            }
            memset((void*)TMP, 0, PAGE_SIZE);
            uint32_t page_data_start = 0;
            if (va < ph.p_vaddr) page_data_start = ph.p_vaddr - va;
            if (file_remaining > 0) {
                uint32_t to_copy = PAGE_SIZE - page_data_start;
                if (to_copy > file_remaining) to_copy = file_remaining;
                int rr = vfs_read(node, ph.p_offset + file_cursor, to_copy, (char*)(TMP + page_data_start));
                (void)rr;
                file_remaining -= to_copy;
                file_cursor += to_copy;
            }
            vmm_unmap_page_nofree(TMP);
            if (eflags_save & 0x200) __asm__ volatile ("sti");
        }
    }

    //map new user stack 4 pages
    uint32_t new_stack_top_phys = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) {
            vfs_close(node);
            return -1;
        }
        uint32_t va = ustack_top - (uint32_t)(i + 1) * 0x1000u;
        if (vmm_map_page_in_directory(dir, va, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
            vfs_close(node);
            return -1;
        }
        if (i == 0) new_stack_top_phys = phys;
    }

    uint32_t new_esp = 0;
    if (build_user_stack(dir, ustack_top, new_stack_top_phys, argv, envp, &new_esp) != 0) {
        vfs_close(node);
        return -1;
    }

    vfs_close(node);

    //free old top stack phys if present and different (top page at ustack_top-0x1000)
    if (old_stack_phys && old_stack_phys != new_stack_top_phys) {
        pmm_free_page(old_stack_phys);
    }

    //update process context and name
    proc->context.eip = eh.e_entry;
    proc->context.esp = new_esp;
    proc->context.ebp = new_esp;
    proc->user_eip = eh.e_entry;
    strncpy(proc->name, pathname, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    //default TTY mode
    proc->tty_mode = TTY_MODE_CANON | TTY_MODE_ECHO;

    //record argv[0] for /proc/<pid>/cmdline (fallback to pathname)
    proc->cmdline[0] = '\0';
    if (argv && argv[0]) {
        strncpy(proc->cmdline, argv[0], sizeof(proc->cmdline) - 1);
        proc->cmdline[sizeof(proc->cmdline) - 1] = '\0';
    } else if (pathname) {
        strncpy(proc->cmdline, pathname, sizeof(proc->cmdline) - 1);
        proc->cmdline[sizeof(proc->cmdline) - 1] = '\0';
    }

    return 0;
}

int elf_execve(const char* pathname, char* const argv[], char* const envp[]) {
    if (!pathname) return -1;

    serial_write_string("[ELF] exec pathname=\"");
    serial_write_string(pathname);
    serial_write_string("\"\n");

    vfs_node_t* node = vfs_open(pathname, VFS_FLAG_READ);
    if (!node) {
        serial_write_string("[ELF] vfs_open failed\n");
        return -1;
    }

    //read ELF header
    Elf32_Ehdr eh;
    int r = vfs_read(node, 0, sizeof(eh), (char*)&eh);
    if (r != (int)sizeof(eh)) { vfs_close(node); return -2; }

    //validate ELF header
    if (!(eh.e_ident[EI_MAG0] == ELFMAG0 && eh.e_ident[EI_MAG1] == ELFMAG1 &&
          eh.e_ident[EI_MAG2] == ELFMAG2 && eh.e_ident[EI_MAG3] == ELFMAG3)) {
            serial_write_string("[ELF] bad magic\n");
            vfs_close(node);
            return -2;
        }
    if (eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        vfs_close(node);
        return -2; }
    if (eh.e_type != ET_EXEC || eh.e_machine != EM_386 || eh.e_version != EV_CURRENT) {
        vfs_close(node);
        return -2;
    }
    if (eh.e_phoff == 0 || eh.e_phnum == 0) {
        vfs_close(node);
        return -2;
    }

    //create a new page directory and map kernel space
    page_directory_t new_dir = vmm_create_directory();
    if (!new_dir) { vfs_close(node); return -1; }
    vmm_map_kernel_space(new_dir);
    //ensure VGA text buffer mapping for safety (panic/print)
    vmm_map_page_in_directory(new_dir, 0x000B8000, 0x000B8000, PAGE_PRESENT | PAGE_WRITABLE);

    //load program headers
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;
        Elf32_Off off = eh.e_phoff + (Elf32_Off)i * (Elf32_Off)eh.e_phentsize;
        r = vfs_read(node, off, sizeof(ph), (char*)&ph);
        if (r != (int)sizeof(ph)) {
            serial_write_string("[ELF] phdr read failed\n");
            vfs_close(node);
            return -1;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_memsz == 0) continue;

        uint32_t seg_start = ph.p_vaddr & ~0xFFFu;
        uint32_t seg_end   = (ph.p_vaddr + ph.p_memsz + 0xFFFu) & ~0xFFFu;
        uint32_t file_remaining = ph.p_filesz;
        uint32_t file_cursor = 0;
        for (uint32_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            //allocate physical page and map into new_dir
            uint32_t phys = pmm_alloc_page();
            if (!phys) {
                vfs_close(node);
                return -1;
            }
            uint32_t flags = PAGE_PRESENT | PAGE_USER;
            if (ph.p_flags & PF_W) flags |= PAGE_WRITABLE; //X bit ignored by paging
            if (vmm_map_page_in_directory(new_dir, va, phys, flags) != 0) {
                vfs_close(node);
                return -1;
            }

            //temp map in kernel to zero and copy segment bytes
            uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
            const uint32_t TMP = 0x00800000; //same temp mapping as elsewhere
            if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                if (eflags_save & 0x200) __asm__ volatile ("sti");
                vfs_close(node); return -1;
            }
            memset((void*)TMP, 0, PAGE_SIZE);

            //compute copy region within this page
            uint32_t page_data_start = 0;
            if (va < ph.p_vaddr) page_data_start = ph.p_vaddr - va;
            if (file_remaining > 0) {
                uint32_t to_copy = PAGE_SIZE - page_data_start;
                if (to_copy > file_remaining) to_copy = file_remaining;
                //read from file into temp mapping at correct offset
                int rr = vfs_read(node, ph.p_offset + file_cursor, to_copy, (char*)(TMP + page_data_start));
                (void)rr; //in practice rr==to_copy if OK
                file_remaining -= to_copy;
                file_cursor += to_copy;
            }

            vmm_unmap_page_nofree(TMP);
            if (eflags_save & 0x200) __asm__ volatile ("sti");
        }
    }

    //duplicate argv/envp into kernel memory first (user pointers may be invalidated during exec)
    int argc = 0; if (argv) { while (argv[argc]) argc++; }
    int envc = 0; if (envp) { while (envp[envc]) envc++; }
    char** kargv = NULL; char** kenvp = NULL;
    if (argc > 0) {
        kargv = (char**)kmalloc(sizeof(char*) * (uint32_t)(argc + 1));
        if (!kargv) {
            vfs_close(node);
            return -1;
        }
        for (int i = 0; i < argc; i++) {
            size_t len = strlen(argv[i]);
            char* s = (char*)kmalloc(len + 1);
            if (!s) {
                vfs_close(node);
                return -1;
            }
            memcpy(s, argv[i], len + 1);
            kargv[i] = s;
        }
        kargv[argc] = NULL;
    }
    if (envc > 0) {
        kenvp = (char**)kmalloc(sizeof(char*) * (uint32_t)(envc + 1));
        if (!kenvp) {
            vfs_close(node);
            return -1;
        }
        for (int i = 0; i < envc; i++) {
            size_t len = strlen(envp[i]);
            char* s = (char*)kmalloc(len + 1);
            if (!s) {
                vfs_close(node);
                return -1;
            }
            memcpy(s, envp[i], len + 1);
            kenvp[i] = s;
        }
        kenvp[envc] = NULL;
    }

    //build a fresh user stack at 0x02000000 mapping 4 pages
    const uint32_t ustack_top = 0x02000000;
    uint32_t new_stack_top_phys = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) { vfs_close(node); return -1; }
        uint32_t va = ustack_top - (uint32_t)(i + 1) * 0x1000u;
        if (vmm_map_page_in_directory(new_dir, va, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
            vfs_close(node); return -1;
        }
        if (i == 0) new_stack_top_phys = phys;
    }

    uint32_t new_esp = 0;
    if (build_user_stack(new_dir, ustack_top, new_stack_top_phys, (char* const*)kargv, (char* const*)kenvp, &new_esp) != 0) {
        vfs_close(node);
        return -1;
    }

    {
        const uint32_t ustack_va = ustack_top - 0x1000;
        const uint32_t TMP = 0x00800000;
        uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
        if (vmm_map_page(TMP, new_stack_top_phys, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
            uint32_t argc_dbg = *(uint32_t*)(TMP + (new_esp - ustack_va));
            uint32_t argv0_ptr = *(uint32_t*)(TMP + (new_esp - ustack_va + 4));
            serial_write_string("[ELF] stack argc=\n");
            serial_printf("%d", (int)argc_dbg);
            serial_write_string(" argv0_ptr=0x\n");
            serial_printf("%x", argv0_ptr);
            serial_write_string("\n");
            if (argv0_ptr) {
                const char* s0 = (const char*)(TMP + (argv0_ptr - ustack_va));
                serial_write_string("[ELF] stack argv0=\"\n");
                serial_write_string(s0);
                serial_write_string("\"\n");
            }
            vmm_unmap_page_nofree(TMP);
        }
        if (eflags_save & 0x200) __asm__ volatile ("sti");
    }

    vfs_close(node);

    //swap process address space and jump
    process_t* cur = process_get_current();
    if (!cur) return -1;
    page_directory_t old_dir = cur->page_directory;
    cur->page_directory = new_dir;
    //update process context and name
    cur->context.eip = eh.e_entry;
    cur->context.esp = new_esp;
    cur->context.ebp = new_esp;
    cur->user_eip = eh.e_entry;
    cur->tty_mode = TTY_MODE_CANON | TTY_MODE_ECHO;

    //record argv[0] for /proc/<pid>/cmdline (fallback to pathname)
    cur->cmdline[0] = '\0';
    if (kargv && kargv[0]) {
        strncpy(cur->cmdline, kargv[0], sizeof(cur->cmdline) - 1);
        cur->cmdline[sizeof(cur->cmdline) - 1] = '\0';
    } else if (pathname) {
        strncpy(cur->cmdline, pathname, sizeof(cur->cmdline) - 1);
        cur->cmdline[sizeof(cur->cmdline) - 1] = '\0';
    }

    //switch to new directory and destroy the old one (if not kernel)
    vmm_switch_directory(new_dir);
    if (old_dir && old_dir != vmm_get_kernel_directory()) {
        vmm_destroy_directory(old_dir);
    }

    //leave kernel path and enter user
    syscall_mark_exit();
    switch_to_user_mode(eh.e_entry, new_esp);
    return 0; //not reached
}
