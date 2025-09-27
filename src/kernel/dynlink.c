#include "dynlink.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../drivers/serial.h"
#include "../debug.h"
#include <string.h>

//silence dynlink debug unless enabled
#if !(LOG_ELF) && !(LOG_EXEC)
#define serial_write_string(x) do { (void)(x); } while(0)
#define serial_printf(...) do {} while(0)
#endif

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;

typedef struct {
    unsigned char e_ident[16];
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

typedef struct {
    int32_t d_tag;   //signed for negative tags
    union { Elf32_Word d_val; Elf32_Addr d_ptr; } d_un;
} Elf32_Dyn;

typedef struct {
    Elf32_Word st_name;
    Elf32_Addr st_value;
    Elf32_Word st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf32_Half st_shndx;
} Elf32_Sym;

//e_type
#define ET_DYN 3
//e_machine
#define EM_386 3
//p_type
#define PT_LOAD    1
#define PT_DYNAMIC 2
//p_flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

//dynamic tags (subset)
#define DT_NULL        0
#define DT_NEEDED      1
#define DT_PLTRELSZ    2
#define DT_PLTGOT      3
#define DT_HASH        4
#define DT_STRTAB      5
#define DT_SYMTAB      6
#define DT_STRSZ       10
#define DT_INIT        12
#define DT_FINI        13
#define DT_SONAME      14
#define DT_RPATH       15
#define DT_REL         17
#define DT_RELSZ       18
#define DT_RELENT      19
#define DT_PLTREL      20
#define DT_DEBUG       21
#define DT_TEXTREL     22
#define DT_JMPREL      23
#define DT_BIND_NOW    24
#define DT_INIT_ARRAY  25
#define DT_FINI_ARRAY  26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH     29

//i386 REL relocation types
#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8
#define R_386_COPY      5

//REL entry
typedef struct {
    Elf32_Addr r_offset;
    Elf32_Word r_info;   //sym << 8 | type
} Elf32_Rel;

#define ELF32_R_SYM(info)  ((info) >> 8)
#define ELF32_R_TYPE(info) ((uint8_t)(info))

static uint32_t find_free_region(page_directory_t dir, uint32_t length) {
    if (length == 0) return 0;
    //page align
    length = (length + 0xFFFu) & ~0xFFFu;
    uint32_t start = 0x04000000u;
    if (start < USER_VIRTUAL_START) start = USER_VIRTUAL_START;
    uint32_t end_limit = 0x70000000u;
    if (end_limit > USER_VIRTUAL_END) end_limit = USER_VIRTUAL_END;

    page_directory_t saved = vmm_get_current_directory();
    vmm_switch_directory(dir);
    for (uint32_t base = start; base + length <= end_limit; base += 0x1000u) {
        int ok = 1;
        for (uint32_t off = 0; off < length; off += 0x1000u) {
            if (vmm_get_physical_addr(base + off) != 0) { ok = 0; break; }
        }
        if (ok) { vmm_switch_directory(saved); return base; }
    }
    vmm_switch_directory(saved);
    return 0;
}

static int map_segment_into_dir(page_directory_t dir,
                                vfs_node_t* file,
                                const Elf32_Phdr* ph,
                                uint32_t load_base)
{
    if (ph->p_memsz == 0) return 0;
    uint32_t seg_start = (load_base + ph->p_vaddr) & ~0xFFFu;
    uint32_t seg_end   = (load_base + ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;

    for (uint32_t va = seg_start; va < seg_end; va += 0x1000u) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) return -1;
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITABLE;
        if (vmm_map_page_in_directory(dir, va, phys, flags) != 0) {
            return -1;
        }
        //zero and copy file bytes for this page
        uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
        const uint32_t TMP = 0x00800000u;
        if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            if (eflags_save & 0x200) __asm__ volatile ("sti");
            return -1;
        }
        memset((void*)TMP, 0, 0x1000u);
        //compute region of this page that belongs to file
        uint32_t page_data_start = 0;
        if (va < (load_base + ph->p_vaddr)) {
            page_data_start = (load_base + ph->p_vaddr) - va;
        }
        //calculate how many file bytes to copy into this page
        //file bytes start at ph->p_offset length p_filesz
        //where this page file portion begins relative to seg
        if (ph->p_filesz > 0) {
            //compute copy window within page
            uint32_t copy_start_in_seg = (va + page_data_start) - (load_base + ph->p_vaddr);
            if (copy_start_in_seg < ph->p_filesz) {
                uint32_t to_copy = 0x1000u - page_data_start;
                if (to_copy > (ph->p_filesz - copy_start_in_seg)) to_copy = ph->p_filesz - copy_start_in_seg;
                //copy from file
                int rr = vfs_read(file, ph->p_offset + copy_start_in_seg, to_copy, (char*)(TMP + page_data_start));
                (void)rr;
            }
        }
        vmm_unmap_page_nofree(TMP);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
    }
    return 0;
}

static int read_dyn_u32(page_directory_t dir, uint32_t va, uint32_t* out) {
    if (!va) {
        *out = 0;
        return 0;
    }
    page_directory_t saved = vmm_get_current_directory();
    vmm_switch_directory(dir);
    uint32_t phys = vmm_get_physical_addr(va & ~0xFFFu) & ~0xFFFu;
    uint32_t off = va & 0xFFFu;
    vmm_switch_directory(saved);

    if (!phys) return -1;
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    const uint32_t TMP = 0x00800000u;
    if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    *out = *(uint32_t*)(TMP + off);
    vmm_unmap_page_nofree(TMP);
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return 0;
}

static int read_dyn_u8(page_directory_t dir, uint32_t va, uint8_t* out) {
    if (!out) return -1;
    page_directory_t saved = vmm_get_current_directory();
    vmm_switch_directory(dir);
    uint32_t phys = vmm_get_physical_addr(va & ~0xFFFu) & ~0xFFFu;
    uint32_t off = va & 0xFFFu;
    vmm_switch_directory(saved);
    if (!phys) return -1;
    uint32_t eflags_save;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    const uint32_t TMP = 0x00800000u;
    if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    *out = *(uint8_t*)(TMP + off);
    vmm_unmap_page_nofree(TMP);
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return 0;
}

static int read_dyn_u16(page_directory_t dir, uint32_t va, uint16_t* out) {
    if (!out) return -1;
    uint8_t b0 = 0, b1 = 0;
    if (read_dyn_u8(dir, va + 0, &b0) != 0) return -1;
    if (read_dyn_u8(dir, va + 1, &b1) != 0) return -1;
    *out = (uint16_t)(b0 | ((uint16_t)b1 << 8));
    return 0;
}

static int write_dyn_u32(page_directory_t dir, uint32_t va, uint32_t val) {
    page_directory_t saved = vmm_get_current_directory();
    vmm_switch_directory(dir);
    uint32_t phys = vmm_get_physical_addr(va & ~0xFFFu) & ~0xFFFu;
    uint32_t off = va & 0xFFFu;
    vmm_switch_directory(saved);
    if (!phys) return -1;
    uint32_t eflags_save;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    const uint32_t TMP = 0x00800000u;
    if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    *(uint32_t*)(TMP + off) = val;
    vmm_unmap_page_nofree(TMP);
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return 0;
}

static uint32_t sysv_hash(const unsigned char *name) {
    uint32_t h = 0;
    while (*name) {
        h = (h << 4) + *name++;
        uint32_t g = h & 0xF0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static int dyn_extract_tables(dynobj_t* obj) {
    //expect DT_HASH, DT_STRTAB, DT_SYMTAB, DT_STRSZ at minimum
    if (!obj->hash || !obj->strtab || !obj->symtab || !obj->strsz) return -1;
    return 0;
}

static Elf32_Sym dyn_read_sym(dynobj_t* obj, uint32_t sym_index) {
    Elf32_Sym s;
    memset(&s, 0, sizeof(s));
    uint32_t sym_va = obj->symtab + sym_index * sizeof(Elf32_Sym);
    //read field by field using safe 8/16/32-bit access
    read_dyn_u32(obj->dir, sym_va + 0, &s.st_name);
    read_dyn_u32(obj->dir, sym_va + 4, &s.st_value);
    read_dyn_u32(obj->dir, sym_va + 8, &s.st_size);
    uint8_t info=0, other=0; uint16_t shndx=0;
    read_dyn_u8(obj->dir, sym_va + 12, &info);
    read_dyn_u8(obj->dir, sym_va + 13, &other);
    read_dyn_u16(obj->dir, sym_va + 14, &shndx);
    s.st_info = info; s.st_other = other; s.st_shndx = shndx;
    return s;
}

static int dyn_read_str(dynobj_t* obj, uint32_t off, char* out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    //read up to outsz-1 until NUL
    for (size_t i = 0; i < outsz - 1; i++) {
        uint8_t ch;
        if (read_dyn_u8(obj->dir, obj->strtab + off + (uint32_t)i, &ch) != 0) return -1;
        out[i] = (char)ch;
        if (ch == '\0') { return 0; }
    }
    out[outsz - 1] = '\0';
    return 0;
}

static uint32_t dyn_lookup_in_obj(dynobj_t* obj, const char* name) {
    if (!obj->hash) return 0;
    uint32_t nbucket=0, nchain=0;
    read_dyn_u32(obj->dir, obj->hash + 0, &nbucket);
    read_dyn_u32(obj->dir, obj->hash + 4, &nchain);
    if (nbucket == 0 || nchain == 0) return 0;
    uint32_t h = sysv_hash((const unsigned char*)name);
    uint32_t b = h % nbucket;
    uint32_t bucket_va = obj->hash + 8 + b * 4u;
    uint32_t idx = 0;
    read_dyn_u32(obj->dir, bucket_va, &idx);
    while (idx != 0 && idx < nchain) {
        Elf32_Sym s = dyn_read_sym(obj, idx);
        char nm[64]; nm[0] = 0;
        if (s.st_name < obj->strsz) {
            dyn_read_str(obj, s.st_name, nm, sizeof(nm));
        }
        if (nm[0] && strcmp(nm, name) == 0) {
            //skip undefined entries (SHN_UNDEF==0) so we fall through to other objects
            if (s.st_shndx != 0) {
                return obj->base + s.st_value;
            }
        }
        //follow chain
        uint32_t chain_va = obj->hash + 8 + nbucket * 4u + idx * 4u;
        read_dyn_u32(obj->dir, chain_va, &idx);
    }
    return 0;
}

void dynlink_ctx_init(dynlink_ctx_t* ctx, page_directory_t dir) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->dir = dir;
}

int dynlink_load_shared(dynlink_ctx_t* ctx, const char* path, dynobj_t** out_obj) {
    if (!ctx || !path) return -1;
    if (ctx->count >= DYNLINK_MAX_OBJS) return -1;

    vfs_node_t* node = vfs_open(path, VFS_FLAG_READ);
    if (!node) {
        serial_write_string("[DYNLINK] open fail ");
        serial_write_string(path);
        serial_write_string("\n");
        return -1;
    }

    Elf32_Ehdr eh; int r = vfs_read(node, 0, sizeof(eh), (char*)&eh);
    if (r != (int)sizeof(eh) || eh.e_machine != EM_386) {
        vfs_close(node);
        return -1;
    }
    //only support ET_DYN here
    if (eh.e_type != ET_DYN) {
        vfs_close(node);
        return -1;
    }

    //compute memory span of PT_LOAD segments relative to min vaddr
    uint32_t min_vaddr = 0xFFFFFFFFu, max_vaddr = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;
        Elf32_Off off = eh.e_phoff + (Elf32_Off)i * eh.e_phentsize;
        if (vfs_read(node, off, sizeof(ph), (char*)&ph) != (int)sizeof(ph)) {
            vfs_close(node);
            return -1;
        }
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        if (ph.p_vaddr < min_vaddr) min_vaddr = ph.p_vaddr;
        uint32_t end = ph.p_vaddr + ph.p_memsz;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == 0xFFFFFFFFu) { vfs_close(node); return -1; }
    uint32_t min_aligned = (min_vaddr & ~0xFFFu);
    uint32_t span = (max_vaddr - min_aligned + 0xFFFu) & ~0xFFFu;
    //choose a free block for the entire span and compute mapping base so that (base + min_aligned) == chosen_block
    uint32_t block_base = find_free_region(ctx->dir, span);
    if (!block_base) {
        vfs_close(node);
        return -1;
    }
    uint32_t map_base = block_base - min_aligned;

    //track PT_LOAD segments for later textrel toggling
    uint32_t seg_start[DYNLINK_MAX_SEGS];
    uint32_t seg_end[DYNLINK_MAX_SEGS];
    uint8_t  seg_w[DYNLINK_MAX_SEGS];
    int seg_count = 0;

    //map all PT_LOAD at VA = map_base + p_vaddr
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph; Elf32_Off off = eh.e_phoff + (Elf32_Off)i * eh.e_phentsize;
        if (vfs_read(node, off, sizeof(ph), (char*)&ph) != (int)sizeof(ph)) {
            vfs_close(node);
            return -1;
        }
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        if (map_segment_into_dir(ctx->dir, node, &ph, map_base) != 0) {
            vfs_close(node);
            return -1;
        }
        if (seg_count < DYNLINK_MAX_SEGS) {
            uint32_t s = (map_base + ph.p_vaddr) & ~0xFFFu;
            uint32_t e = (map_base + ph.p_vaddr + ph.p_memsz + 0xFFFu) & ~0xFFFu;
            seg_start[seg_count] = s;
            seg_end[seg_count] = e;
            seg_w[seg_count] = (ph.p_flags & PF_W) ? 1u : 0u;
            seg_count++;
        }
    }

    //record object
    dynobj_t* o = &ctx->objs[ctx->count];
    memset(o, 0, sizeof(*o));
    o->dir = ctx->dir;
    o->base = map_base;
    //store name (truncate)
    size_t nlen = strlen(path); if (nlen >= sizeof(o->name)) nlen = sizeof(o->name) - 1;
    memcpy(o->name, path, nlen); o->name[nlen] = '\0';

    //find PT_DYNAMIC and parse
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;
        Elf32_Off off = eh.e_phoff + (Elf32_Off)i * eh.e_phentsize;
        if (vfs_read(node, off, sizeof(ph), (char*)&ph) != (int)sizeof(ph)) {
            vfs_close(node);
            return -1;
        }
        if (ph.p_type != PT_DYNAMIC) continue;
        //iterate dynamic entries at user VA map_base + p_vaddr
        uint32_t dyn_va = map_base + ph.p_vaddr;
        o->dyn_va = dyn_va;
        //walk until DT_NULL
        for (uint32_t idx = 0;; idx++) {
            uint32_t tag = 0, val = 0;
            if (read_dyn_u32(o->dir, dyn_va + idx * 8u + 0, &tag) != 0) break;
            if (read_dyn_u32(o->dir, dyn_va + idx * 8u + 4, &val) != 0) break;
            if ((int32_t)tag == DT_NULL) break;
            switch ((int32_t)tag) {
                case DT_HASH:   o->hash = o->base + val; break;
                case DT_STRTAB: o->strtab = o->base + val; break;
                case DT_SYMTAB: o->symtab = o->base + val; break;
                case DT_STRSZ:  o->strsz = val; break;
                case DT_REL:    o->rel = o->base + val; break;
                case DT_RELSZ:  o->relsz = val; break;
                case DT_JMPREL: o->plt_rel = o->base + val; break;
                case DT_PLTRELSZ: o->plt_relsz = val; break;
                case DT_PLTREL: o->plt_rel_type = val; break;
                case DT_INIT:   o->init_addr = o->base + val; break;
                case DT_FINI:   o->fini_addr = o->base + val; break;
                case DT_INIT_ARRAY:   o->init_array = o->base + val; break;
                case DT_INIT_ARRAYSZ: o->init_arraysz = val; break;
                case DT_FINI_ARRAY:   o->fini_array = o->base + val; break;
                case DT_FINI_ARRAYSZ: o->fini_arraysz = val; break;
                case DT_RPATH:       o->rpath_off = val; break;
                case DT_RUNPATH:     o->runpath_off = val; break;
                case DT_SONAME:      o->soname_off = val; break;
                case DT_TEXTREL:     o->textrel = 1; break;
                default: break;
            }
        }
    }

    vfs_close(node);

    if (dyn_extract_tables(o) != 0) {
        serial_write_string("[DYNLINK] missing tables for "); serial_write_string(o->name); serial_write_string("\n");
        return -1;
    }

    //finalize SONAME if available
    if (o->soname_off && o->strtab && o->strsz) {
        dyn_read_str(o, o->soname_off, o->soname, sizeof(o->soname));
    }

    //persist tracked segments
    o->seg_count = seg_count;
    for (int i = 0; i < seg_count; i++) {
        o->seg_start[i] = seg_start[i];
        o->seg_end[i] = seg_end[i];
        o->seg_writable[i] = seg_w[i];
    }

    o->ready = 1;
    ctx->count++;
    if (out_obj) *out_obj = o;
    serial_write_string("[DYNLINK] loaded ");
    serial_write_string(o->name);
    serial_write_string(" base=0x");
    serial_printf("%x", o->base);
    serial_write_string("\n");
    return 0;
}

static uint32_t resolve_symbol_across(dynlink_ctx_t* ctx, const char* name) {
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        uint32_t va = dyn_lookup_in_obj(o, name);
        if (va) return va;
    }
    return 0;
}

static const char* rel_type_name(uint8_t t) {
    switch (t) {
        case R_386_NONE: return "R_386_NONE";
        case R_386_32: return "R_386_32";
        case R_386_PC32: return "R_386_PC32";
        case R_386_GLOB_DAT: return "R_386_GLOB_DAT";
        case R_386_JMP_SLOT: return "R_386_JMP_SLOT";
        case R_386_RELATIVE: return "R_386_RELATIVE";
        default: return "R_386_?";
    }
}

static void print_loaded_objects(dynlink_ctx_t* ctx) {
    serial_write_string("[DYNLINK] objects: ");
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* oo = &ctx->objs[i];
        if (!oo->ready) continue;
        serial_write_string(" ");
        serial_write_string(oo->name);
    }
    serial_write_string("\n");
}

static int try_dirlist(dynlink_ctx_t* ctx, const char* dirlist, const char* libname) {
    if (!ctx || !dirlist || !libname) return -1;
    const char* s = dirlist;
    const char* start = s;
    char path[128];
    while (1) {
        if (*s == ':' || *s == '\0') {
            size_t len = (size_t)(s - start);
            if (len > 0 && len < 100) {
                size_t pos = 0;
                if (len >= sizeof(path)) len = sizeof(path) - 1;
                memcpy(path, start, len);
                pos = len;
                if (pos == 0 || path[pos - 1] != '/') {
                    if (pos + 1 < sizeof(path)) path[pos++] = '/';
                }
                size_t nl = strlen(libname);
                if (pos + nl >= sizeof(path)) nl = sizeof(path) - pos - 1;
                memcpy(path + pos, libname, nl); path[pos + nl] = '\0';
                dynobj_t* child = NULL;
                if (dynlink_load_shared(ctx, path, &child) == 0 && child) {
                    (void)dynlink_load_needed(ctx, child);
                    return 0;
                }
            }
            if (*s == '\0') break;
            start = s + 1;
        }
        s++;
    }
    return -1;
}

static int apply_rel_table(dynobj_t* o, dynlink_ctx_t* ctx, uint32_t rel_va, uint32_t rel_sz) {
    if (!rel_va || rel_sz == 0) return 0;
    for (uint32_t off = 0; off + sizeof(Elf32_Rel) <= rel_sz; off += sizeof(Elf32_Rel)) {
        uint32_t r_off=0, r_info=0;
        if (read_dyn_u32(o->dir, rel_va + off + 0, &r_off) != 0) return -1;
        if (read_dyn_u32(o->dir, rel_va + off + 4, &r_info) != 0) return -1;
        uint8_t type = (uint8_t)ELF32_R_TYPE(r_info);
        uint32_t sym_index = ELF32_R_SYM(r_info);
        uint32_t A = 0; //addend (REL has implicit addend from memory content)
        //read current 32-bit value at relocation target as addend
        read_dyn_u32(o->dir, o->base + r_off, &A);
        switch (type) {
            case R_386_RELATIVE: {
                //B + A
                uint32_t val = o->base + A;
                if (write_dyn_u32(o->dir, o->base + r_off, val) != 0) return -1;
                break;
            }
            case R_386_COPY: {
                //only in main executable copy size bytes from shared object definition to P
                Elf32_Sym s = dyn_read_sym(o, sym_index);
                char nm[64]; nm[0] = 0;
                if (s.st_name < o->strsz) dyn_read_str(o, s.st_name, nm, sizeof(nm));
                uint32_t S = 0;
                if (nm[0]) S = resolve_symbol_across(ctx, nm);
                if (!S || s.st_size == 0) {
                    serial_write_string("[DYNLINK] COPY unresolved '");
                    serial_write_string(nm);
                        serial_write_string("'\n");
                    return -1;
                }
                uint32_t P = o->base + r_off;
                //copy s.st_size bytes from S to P
                //use a temp buffer in chunks to avoid many map ops
                uint8_t buf[64];
                uint32_t remaining = s.st_size;
                uint32_t src = S;
                uint32_t dst = P;
                while (remaining) {
                    uint32_t chunk = (remaining > sizeof(buf)) ? (uint32_t)sizeof(buf) : remaining;
                    //read chunk from src into buf
                    for (uint32_t i = 0; i < chunk; i += 4) {
                        uint32_t word = 0;
                        read_dyn_u32(o->dir, src + i, &word);
                        *(uint32_t*)(buf + i) = word;
                    }
                    //write chunk to dst
                    for (uint32_t i = 0; i < chunk; i += 4) {
                        uint32_t word = *(uint32_t*)(buf + i);
                        write_dyn_u32(o->dir, dst + i, word);
                    }
                    remaining -= chunk;
                    src += chunk;
                    dst += chunk;
                }
                break;
            }
            case R_386_GLOB_DAT:
            case R_386_JMP_SLOT:
            case R_386_32:
            case R_386_PC32: {
                //resolve symbol name
                Elf32_Sym s = dyn_read_sym(o, sym_index);
                char nm[64]; nm[0] = 0;
                if (s.st_name < o->strsz) dyn_read_str(o, s.st_name, nm, sizeof(nm));
                uint32_t S = 0;
                if (nm[0]) S = resolve_symbol_across(ctx, nm);
                if (!S) {
                    serial_write_string("[DYNLINK] unresolved symbol '");
                    serial_write_string(nm);
                    serial_write_string("' ");
                    serial_write_string("in ");
                    serial_write_string(o->name);
                    serial_write_string(" type=");
                    serial_write_string(rel_type_name(type));
                    serial_write_string(" off=0x");
                    serial_printf("%x", (unsigned)(o->base + r_off));
                    serial_write_string("\n");
                    print_loaded_objects(ctx);
                    return -1;
                }
                uint32_t P = o->base + r_off;
                uint32_t val = 0;
                if (type == R_386_GLOB_DAT || type == R_386_JMP_SLOT) {
                    val = S;
                } else if (type == R_386_32) {
                    val = S + A;
                } else { //R_386_PC32
                    val = S + A - P;
                }
                if (write_dyn_u32(o->dir, P, val) != 0) return -1;
                break;
            }
            default:
                serial_write_string("[DYNLINK] unsupported rel type ");
                serial_printf("%d", (int)type);
                serial_write_string(" in ");
                serial_write_string(o->name);
                serial_write_string("\n");
                return -1;
        }
    }
    return 0;
}

//toggle writability on text segments for DT_TEXTREL objects
static void dyn_toggle_text_writable(dynobj_t* o, int enable) {
    if (!o || !o->textrel || o->seg_count <= 0) return;
    page_directory_t saved = vmm_get_current_directory();
    vmm_switch_directory(o->dir);
    for (int i = 0; i < o->seg_count; i++) {
        if (o->seg_writable[i]) continue; //already writable leave as-is
        uint32_t start = o->seg_start[i];
        uint32_t end   = o->seg_end[i];
        for (uint32_t va = start; va < end; va += 0x1000u) {
            uint32_t phys = vmm_get_physical_addr(va) & ~0xFFFu;
            if (!phys) continue;
            uint32_t flags = PAGE_PRESENT | PAGE_USER | (enable ? PAGE_WRITABLE : 0);
            (void)vmm_map_page_in_directory(o->dir, va, phys, flags);
        }
    }
    //ensure updated PTE flags take effect
    flush_tlb();
    vmm_switch_directory(saved);
}

int dynlink_apply_relocations(dynlink_ctx_t* ctx) {
    if (!ctx) return -1;
    //first pass apply to all libraries (non-main) so that COPY in main can read fully relocated data
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        if (o->base == 0) continue; //likely main ET_EXEC
        if (o->textrel) dyn_toggle_text_writable(o, 1);
        int rc = 0;
        rc = apply_rel_table(o, ctx, o->rel, o->relsz);
        if (rc != 0) {
            if (o->textrel) dyn_toggle_text_writable(o, 0);
            return -1;
        }
        if (o->plt_rel && o->plt_relsz) {
            if (o->plt_rel_type != DT_REL) {
                if (o->textrel) dyn_toggle_text_writable(o, 0);
                serial_write_string("[DYNLINK] non-REL PLT not supported on IA-32\n");
                return -1;
            }
            rc = apply_rel_table(o, ctx, o->plt_rel, o->plt_relsz); if (rc != 0) {
                if (o->textrel) dyn_toggle_text_writable(o, 0);
                return -1;
            }
        }
        if (o->textrel) dyn_toggle_text_writable(o, 0);
    }
    //second pass apply to main (and any others) including R_386_COPY
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        if (o->base != 0) continue;
        //for main (ET_EXEC base==0) relocation writes use kernel TMP mapping and do not need toggling
        if (apply_rel_table(o, ctx, o->rel, o->relsz) != 0) return -1;
        if (o->plt_rel && o->plt_relsz) {
            if (o->plt_rel_type != DT_REL) {
                serial_write_string("[DYNLINK] non-REL PLT not supported on IA-32\n");
                return -1;
            }
            if (apply_rel_table(o, ctx, o->plt_rel, o->plt_relsz) != 0) return -1;
        }
    }
    return 0;
}

int dynlink_apply_relocations_from(dynlink_ctx_t* ctx, int start_index) {
    if (!ctx) return -1;
    if (start_index < 0) start_index = 0;
    for (int i = start_index; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        if (o->base != 0 && o->textrel) dyn_toggle_text_writable(o, 1);
        int rc = 0;
        rc = apply_rel_table(o, ctx, o->rel, o->relsz);
        if (rc != 0) {
            if (o->base != 0 && o->textrel) dyn_toggle_text_writable(o, 0);
            return -1;
            }
        if (o->plt_rel && o->plt_relsz) {
            if (o->plt_rel_type != DT_REL) {
                if (o->base != 0 && o->textrel) dyn_toggle_text_writable(o, 0);
                serial_write_string("[DYNLINK] non-REL PLT not supported on IA-32\n");
                return -1;
            }
            rc = apply_rel_table(o, ctx, o->plt_rel, o->plt_relsz); if (rc != 0) {
                if (o->base != 0 && o->textrel) dyn_toggle_text_writable(o, 0);
                return -1;
            }
        }
        if (o->base != 0 && o->textrel) dyn_toggle_text_writable(o, 0);
    }
    return 0;
}

void* dynlink_lookup_symbol(dynlink_ctx_t* ctx, const char* name) {
    if (!ctx || !name) return 0;
    uint32_t va = resolve_symbol_across(ctx, name);
    return (void*)(uintptr_t)va;
}

void* dynlink_lookup_symbol_in(dynlink_ctx_t* ctx, int index, const char* name) {
    if (!ctx || !name) return 0;
    if (index < 0 || index >= ctx->count) return 0;
    dynobj_t* o = &ctx->objs[index];
    if (!o->ready) return 0;
    uint32_t va = dyn_lookup_in_obj(o, name);
    return (void*)(uintptr_t)va;
}

static int name_has_slash(const char* s) {
    if (!s) return 0;
    while (*s) {
        if (*s == '/') return 1;
        s++;
    }
    return 0;
}

static const char* path_basename(const char* p) {
    const char* last = p;
    const char* s = p;
    while (*s) {
        if (*s == '/') last = s + 1;
        s++;
    }
    return last;
}

static int already_loaded(dynlink_ctx_t* ctx, const char* name_or_soname) {
    const char* base = path_basename(name_or_soname);
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        const char* obase = path_basename(o->name);
        if (strcmp(obase, base) == 0) return 1;
        if (o->soname[0] && strcmp(o->soname, name_or_soname) == 0) return 1;
    }
    return 0;
}

int dynlink_find_loaded(dynlink_ctx_t* ctx, const char* name_or_soname) {
    if (!ctx || !name_or_soname) return -1;
    const char* base = path_basename(name_or_soname);
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        const char* obase = path_basename(o->name);
        if (strcmp(obase, base) == 0) return i;
        if (o->soname[0] && strcmp(o->soname, name_or_soname) == 0) return i;
    }
    return -1;
}

int dynlink_load_needed(dynlink_ctx_t* ctx, dynobj_t* root) {
    if (!ctx || !root || !root->dyn_va) return 0;

    //iterate DT_NEEDED entries
    for (uint32_t idx = 0;; idx++) {
        uint32_t tag = 0, val = 0;
        if (read_dyn_u32(root->dir, root->dyn_va + idx * 8u + 0, &tag) != 0) break;
        if (read_dyn_u32(root->dir, root->dyn_va + idx * 8u + 4, &val) != 0) break;
        if ((int32_t)tag == DT_NULL) break;
        if ((int32_t)tag == DT_NEEDED) {
            //val is offset into root->strtab
            char nm[64]; nm[0] = 0;
            if (val < root->strsz) dyn_read_str(root, val, nm, sizeof(nm));
            if (!nm[0]) continue;
            //avoid duplicates
            if (already_loaded(ctx, nm)) continue;

            //if path contains '/' use as-is
            if (name_has_slash(nm)) {
                char path[96]; size_t l = strlen(nm); if (l >= sizeof(path)) l = sizeof(path) - 1;
                memcpy(path, nm, l); path[l] = '\0';
                dynobj_t* child = NULL;
                if (dynlink_load_shared(ctx, path, &child) == 0 && child) {
                    (void)dynlink_load_needed(ctx, child);
                } else {
                    serial_write_string("[DYNLINK] failed to load ");
                    serial_write_string(nm);
                    serial_write_string(" from explicit path\n");
                }
                continue;
            }

            //search order: LD_LIBRARY_PATH, RUNPATH, RPATH, fallback /lib
            if (ctx->ld_library_path[0]) {
                if (try_dirlist(ctx, ctx->ld_library_path, nm) == 0) continue;
            }
            char plist[128]; plist[0] = '\0';
            if (root->runpath_off && root->runpath_off < root->strsz) {
                dyn_read_str(root, root->runpath_off, plist, sizeof(plist));
            }
            if (plist[0]) {
                if (try_dirlist(ctx, plist, nm) == 0) continue;
            }
            plist[0] = '\0';
            if (root->rpath_off && root->rpath_off < root->strsz) {
                dyn_read_str(root, root->rpath_off, plist, sizeof(plist));
            }
            if (plist[0]) {
                if (try_dirlist(ctx, plist, nm) == 0) continue;
            }
            //fallback to /lib
            char path[128];
            const char* prefix = "/lib/";
            size_t pl = 5; size_t nl = strlen(nm);
            if (pl + nl >= sizeof(path)) nl = sizeof(path) - pl - 1;
            memcpy(path, prefix, pl);
            memcpy(path + pl, nm, nl);
            path[pl + nl] = '\0';
            dynobj_t* child = NULL;
            if (dynlink_load_shared(ctx, path, &child) == 0 && child) {
                (void)dynlink_load_needed(ctx, child);
            } else {
                serial_write_string("[DYNLINK] could not locate ");
                serial_write_string(nm);
                serial_write_string(" using RUNPATH/RPATH; tried /lib fallback\n");
            }
        }
    }
    return 0;
}

int dynlink_attach_from_memory(dynlink_ctx_t* ctx, uint32_t base, uint32_t dyn_va, const char* name, dynobj_t** out_obj) {
    if (!ctx || !dyn_va) return -1;
    if (ctx->count >= DYNLINK_MAX_OBJS) return -1;
    dynobj_t* o = &ctx->objs[ctx->count];
    memset(o, 0, sizeof(*o));
    o->dir = ctx->dir;
    o->base = base;
    if (name) {
        size_t nlen = strlen(name);
        if (nlen >= sizeof(o->name)) nlen = sizeof(o->name) - 1;
        memcpy(o->name, name, nlen); o->name[nlen] = '\0';
    } else {
        strcpy(o->name, "(main)");
    }

    //record dynamic table VA
    o->dyn_va = dyn_va;

    //parse DYNAMIC entries from memory
    for (uint32_t idx = 0;; idx++) {
        uint32_t tag = 0, val = 0;
        if (read_dyn_u32(o->dir, dyn_va + idx * 8u + 0, &tag) != 0) break;
        if (read_dyn_u32(o->dir, dyn_va + idx * 8u + 4, &val) != 0) break;
        if ((int32_t)tag == DT_NULL) break;
        switch ((int32_t)tag) {
            case DT_HASH:   o->hash = o->base + val; break;
            case DT_STRTAB: o->strtab = o->base + val; break;
            case DT_SYMTAB: o->symtab = o->base + val; break;
            case DT_STRSZ:  o->strsz = val; break;
            case DT_REL:    o->rel = o->base + val; break;
            case DT_RELSZ:  o->relsz = val; break;
            case DT_JMPREL: o->plt_rel = o->base + val; break;
            case DT_PLTRELSZ: o->plt_relsz = val; break;
            case DT_PLTREL: o->plt_rel_type = val; break;
            case DT_INIT:   o->init_addr = o->base + val; break;
            case DT_FINI:   o->fini_addr = o->base + val; break;
            case DT_INIT_ARRAY:   o->init_array = o->base + val; break;
            case DT_INIT_ARRAYSZ: o->init_arraysz = val; break;
            case DT_FINI_ARRAY:   o->fini_array = o->base + val; break;
            case DT_FINI_ARRAYSZ: o->fini_arraysz = val; break;
            case DT_RPATH:       o->rpath_off = val; break;
            case DT_RUNPATH:     o->runpath_off = val; break;
            case DT_SONAME:      o->soname_off = val; break;
            default: break;
        }
    }

    if (dyn_extract_tables(o) != 0) {
        serial_write_string("[DYNLINK] attach(main) missing tables\n");
        return -1;
    }
    //finalize SONAME if available
    if (o->soname_off && o->strtab && o->strsz) {
        dyn_read_str(o, o->soname_off, o->soname, sizeof(o->soname));
    }

    o->ready = 1;
    if (out_obj) *out_obj = o;
    ctx->count++;
    return 0;
}
