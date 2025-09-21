#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdint.h>

//forward decl so we can reference the process type without including process.h here
struct process;

//execute an ELF32 (x86) binary at 'pathname' in the current process replacing
//its user address space and jumping to the new entry on success this does not return
//returns:
//0 success (not returned)
//-2 file is not a valid ELF x86 executable (caller may fallback to flat loader)
//-1 error
int elf_execve(const char* pathname, char* const argv[], char* const envp[]);

//load an ELF32 binary at 'pathname' into the provided process
//address space (proc->page_directory) set its initial user entry and stack
//and leave it runnable does not switch the current CPU to the new process
//returns:
//0 success (not returned)
//-2 file is not a valid ELF32 executable
//-1 error
int elf_load_into_process(const char* pathname, struct process* proc,
                          char* const argv[], char* const envp[]);

#endif
