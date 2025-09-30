#ifndef KERNEL_DYNLINK_H
#define KERNEL_DYNLINK_H

#include <stdint.h>
#include <stddef.h>
#include "mm/vmm.h"

//maximum number of shared objects to load for one process (MVP)
#define DYNLINK_MAX_OBJS 8
//maximum PT_LOAD segments tracked per object (for textrel toggling)
#define DYNLINK_MAX_SEGS 8

//a loaded dynamic object (ET_DYN or PIE main)
typedef struct dynobj {
    //target address space we mapped into
    page_directory_t dir;

    //preferred/load base for ET_DYN (chosen by loader)
    uint32_t base;

    //dynamic table location (user VA)
    uint32_t dyn_va;     //VA of PT_DYNAMIC table

    //dynamic table pointers (VA in user space)
    uint32_t strtab;     //DT_STRTAB
    uint32_t strsz;      //DT_STRSZ
    uint32_t symtab;     //DT_SYMTAB
    uint32_t hash;       //DT_HASH (SysV)

    //relocations (REL and JMPREL/PLT)
    uint32_t rel;        //DT_REL
    uint32_t relsz;      //DT_RELSZ
    uint32_t plt_rel;    //DT_JMPREL
    uint32_t plt_relsz;  //size of PLT reloc table
    uint32_t plt_rel_type; //DT_PLTREL (should be DT_REL for IA-32)

    //init/fini (optional for later phases)
    uint32_t init_addr;      //DT_INIT
    uint32_t fini_addr;      //DT_FINI
    uint32_t init_array;     //DT_INIT_ARRAY (VA)
    uint32_t init_arraysz;   //DT_INIT_ARRAYSZ (bytes)
    uint32_t fini_array;     //DT_FINI_ARRAY (VA)
    uint32_t fini_arraysz;   //DT_FINI_ARRAYSZ (bytes)

    //search path hints
    uint32_t rpath_off;      //DT_RPATH (offset into strtab)
    uint32_t runpath_off;    //DT_RUNPATH (offset into strtab)
    uint32_t soname_off;     //DT_SONAME (offset into strtab)

    //bookkeeping
    char     name[64];   //load path (truncated)
    char     soname[64]; //SONAME if present
    int      ready;      //parsed and mapped

    //text relocation indicator (DT_TEXTREL present)
    int      textrel;

    //tracked PT_LOAD segments for temporary text writability toggling
    int      seg_count;
    uint32_t seg_start[DYNLINK_MAX_SEGS];
    uint32_t seg_end[DYNLINK_MAX_SEGS];
    uint8_t  seg_writable[DYNLINK_MAX_SEGS]; //1 if PF_W originally set
} dynobj_t;

//a linking context for a process (holds the loaded set)
typedef struct dynlink_ctx {
    dynobj_t objs[DYNLINK_MAX_OBJS];
    int count;
    page_directory_t dir;
    char ld_library_path[128]; //process-level LD_LIBRARY_PATH
} dynlink_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

//initalzie an empty context for the given address space
void dynlink_ctx_init(dynlink_ctx_t* ctx, page_directory_t dir);

//load a shared object (.so) into the address space and parse its dynamic section
//returns 0 on success <0 on error.
int dynlink_load_shared(dynlink_ctx_t* ctx, const char* path, dynobj_t** out_obj);

//apply REL relocations for all loaded objects (eager binding of PLT as well)
//returns 0 on success <0 on error
int dynlink_apply_relocations(dynlink_ctx_t* ctx);

//apply relocations only for objects loaded at or after start_index
//does not touch previously relocated objects returns 0 on success
int dynlink_apply_relocations_from(dynlink_ctx_t* ctx, int start_index);

//look up a symbol across all loaded objects returns user VA or 0 if not found
void* dynlink_lookup_symbol(dynlink_ctx_t* ctx, const char* name);

//look up a symbol in a specific loaded object by index returns VA or 0
void* dynlink_lookup_symbol_in(dynlink_ctx_t* ctx, int index, const char* name);

//find a loaded object by SONAME or basename of path returns index or -1
int dynlink_find_loaded(dynlink_ctx_t* ctx, const char* name_or_soname);

//attach a main or pre-mapped object by parsing a PT_DYNAMIC located at dyn_va
//base is the load base to add to DYNAMIC pointer values (0 for ET_EXEC mains)
int dynlink_attach_from_memory(dynlink_ctx_t* ctx, uint32_t base, uint32_t dyn_va, const char* name, dynobj_t** out_obj);

//load all DT_NEEDED dependencies for the given object (recursively)
//returns 0 on success (or if none) <0 on error
int dynlink_load_needed(dynlink_ctx_t* ctx, dynobj_t* root);

#ifdef __cplusplus
}
#endif

#endif
