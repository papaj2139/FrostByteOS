#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "io.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "device_manager.h"
#include "drivers/pc_speaker.h"
#include "drivers/fb.h"
#include "drivers/fbcon.h"
#include "gui/vga.h"
#include "interrupts/idt.h"
#include "interrupts/pic.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/ahci.h"
#include "drivers/sb16.h"
#include "drivers/apic.h"
#include "syscall.h"
#include "interrupts/gdt.h"
#include "interrupts/tss.h"
#include "drivers/mouse.h"
#include "drivers/tty.h"
#include "fs/fs.h"
#include "fs/fat16.h"
#include "fs/vfs.h"
#include "fs/initramfs.h"
#include "fs/initramfs_cpio.h"
#include "fs/procfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "process.h"
#include "ipc/shm.h"
#include "ipc/socket.h"
#include "kernel/cga.h"
#include "kernel/panic.h"
#include "kernel/kreboot.h"
#include "kernel/kshutdown.h"
#include "kernel/elf.h"
#include "kernel/multiboot.h"
#include <stdbool.h>

//global console quiet flag (0=verbose to screen 1=suppress)
int g_console_quiet = 1;  //default to quiet, serial debug only

static uint32_t total_memory_mb = 0;
char *bsodVer = "classic"; //BSOD style for kernel panic

extern char kernel_start, kernel_end;

//VBE mode info subset
typedef struct __attribute__((packed)) {
    uint8_t  _pad0[0x10];
    uint16_t BytesPerScanLine; //0x10
    uint16_t XResolution;      //0x12
    uint16_t YResolution;      //0x14
    uint8_t  _pad1[0x03];
    uint8_t  BitsPerPixel;     //0x19
    uint8_t  _pad2[0x0E];
    uint32_t PhysBasePtr;      //0x28
} vbe_mode_info_t;

//spawn a user program from VFS path
//returns 1 on success 0 on failure
static int spawn_user_from_vfs(const char* path) {
    if (!path || !*path) return 0;
    #if DEBUG_ENABLED
    serial_write_string("\nLoading app from VFS: ");
    serial_write_string(path);
    serial_write_string("\n");
    #endif

    //create a fresh user process
    process_t* proc = process_create(path, (void*)0, true);
    if (!proc) {
        serial_write_string("[VFS] process_create failed\n");
        return 0;
    }

    //try ELF loader first
    char* elf_argv[2] = { (char*)path, NULL };
    char* elf_envp[1] = { NULL };
    int er = elf_load_into_process(path, proc, elf_argv, elf_envp);
    if (er == 0) {
        //success let scheduler run it
        process_yield();
        return 1;
    }
    //error or not an ELF
    serial_write_string("[ELF] not an ELF executable or load error\n");
    process_destroy(proc);
    return 0;
}

void kmain(uint32_t magic, uint32_t addr) {
    (void)magic; // unused
    multiboot_info_t* mbi = (multiboot_info_t*)addr;
    uint32_t mem_lower = (mbi && (mbi->flags & MBI_FLAG_MEM)) ? mbi->mem_lower : 0;
    uint32_t mem_upper = (mbi && (mbi->flags & MBI_FLAG_MEM)) ? mbi->mem_upper : 0;

    total_memory_mb = (mem_lower + mem_upper) / 1024 + 1;

    //expose kernel cmdline via procfs for init to read
    const char* boot_cmdline = (mbi && mbi->cmdline) ? (const char*)mbi->cmdline : "";
    procfs_set_cmdline(boot_cmdline);
    int disable_vesa = 0;
    int apic_override = 0;  //0=auto 1=force APIC -1=force PIC
    if (boot_cmdline && *boot_cmdline) {
        if (strstr(boot_cmdline, "novesa") || strstr(boot_cmdline, "vesa=off")) {
            disable_vesa = 1;
        }
        if (strstr(boot_cmdline, "quiet")) {
            g_console_quiet = 1;
        }
        //check for interrupt controller selection in cmdline
        if (strstr(boot_cmdline, "noapic") || strstr(boot_cmdline, "pic")) {
            apic_override = -1;  //force PIC
        } else if (strstr(boot_cmdline, "apic")) {
            apic_override = 1;   //force APIC
        }
    }

    kclear();
    print("Loading into FrostByte...", 0x0F);
    serial_init();
    speaker_init();
    DEBUG_PRINT("FrostByteOS kernel started");

    //initialize GDT and TSS
    gdt_init();
    DEBUG_PRINT("GDT initialized");
    tss_init();
    DEBUG_PRINT("TSS initialized");

    DEBUG_PRINT("Initializing memory management...");
    if (mbi) {
        pmm_init_multiboot((const struct multiboot_info*)mbi,
                           (uint32_t)&kernel_start,
                           (uint32_t)&kernel_end);
    } else {
        pmm_init(mem_lower, mem_upper);
    }
    DEBUG_PRINT("Physical memory manager initialized");

    vmm_init();
    DEBUG_PRINT("Virtual memory manager initialized - paging enabled!");

    heap_init();
    DEBUG_PRINT("Heap initialized");

    //initialize device manager
    device_manager_init();
    DEBUG_PRINT("Device manager initialized");

    //try to bring up /dev/fb0 using VBE (if bootloader provided it)
    if (!disable_vesa && mbi && (mbi->flags & MBI_FLAG_VBE) && mbi->vbe_mode_info) {
        vbe_mode_info_t* vm = (vbe_mode_info_t*)(uintptr_t)mbi->vbe_mode_info;
        if (vm) {
            uint32_t phys = vm->PhysBasePtr;
            uint32_t w = vm->XResolution;
            uint32_t h = vm->YResolution;
            uint32_t bpp = vm->BitsPerPixel;
            uint32_t pitch = vm->BytesPerScanLine;
            if (phys && w && h && (bpp == 32 || bpp == 24 || bpp == 16)) {
                if (fb_register_from_vbe(phys, w, h, bpp, pitch) == 0) {
                    extern int fbcon_init(void);
                    (void)fbcon_init();
                    #if DEBUG_ENABLED
                    serial_write_string("[VBE] fb0 registered ");
                    serial_printf("%dx%dx%d\n", (int)w, (int)h, (int)bpp);
                    #endif
                }
            }
        }
    }

    //initialize serial, RTC, and SB16 devices
    (void)serial_register_device();
    (void)rtc_register_device();
    (void)sb16_register_device();

    //initialize PCI bus
    pci_init();
    DEBUG_PRINT("PCI bus initialized");

    //initialize and register ATA driver
    ata_init();
    DEBUG_PRINT("ATA driver initialized");

    ata_probe_and_register();
    DEBUG_PRINT("ATA device probing complete");

    //initialize AHCI/SATA driver
    ahci_init();
    DEBUG_PRINT("AHCI driver initialized");

    ahci_probe_and_register();
    DEBUG_PRINT("AHCI device probing complete");

    //register keyboard device
    if (keyboard_register_device() == 0) {
        DEBUG_PRINT("Keyboard device registered with device manager");
    } else {
        DEBUG_PRINT("Failed to register keyboard device");
    }
    //register mouse device
    if (mouse_register_device() == 0) {
        DEBUG_PRINT("Mouse device registered with device manager");
    } else {
        DEBUG_PRINT("Failed to register mouse device");
    }

    //register TTY pseudo-device (text console)
    if (tty_register_device() == 0) {
        DEBUG_PRINT("TTY device registered as tty0");
    } else {
        DEBUG_PRINT("Failed to register TTY device");
    }



    //initialize interrupts
    pic_remap(0x20, 0x28);
    DEBUG_PRINT("PIC remapped");
    idt_install();
    DEBUG_PRINT("IDT installed");
    syscall_init();
    DEBUG_PRINT("Syscalls initialized");

    //initialize process manager before timer to avoid scheduling before ready
    process_init();
    DEBUG_PRINT("Process manager initialized");

    //initialize IPC subsystems
    shm_init();
    DEBUG_PRINT("Shared memory system initialized");
    socket_init();
    DEBUG_PRINT("Socket system initialized");

    //initialize interrupt controller (APIC or PIC)
    bool using_apic = false;
    if (apic_override == -1) {
        //user wants PIC
        DEBUG_PRINT("APIC disabled by kernel parameter (noapic/pic)");
    } else if (apic_override == 1) {
        //user wants APIC
        DEBUG_PRINT("Forcing APIC mode (kernel parameter)");
        using_apic = apic_init();
        if (!using_apic) {
            DEBUG_PRINT("APIC initialization failed, falling back to PIC");
        }
    } else {
        //auto-detect: try APIC if supported fallback to PIC
        if (apic_is_supported()) {
            DEBUG_PRINT("Auto-detected APIC support, initializing...");
            using_apic = apic_init();
            if (!using_apic) {
                DEBUG_PRINT("APIC init failed, using PIC");
            }
        } else {
            DEBUG_PRINT("APIC not supported by CPU, using PIC");
        }
    }
    
    keyboard_init(); //enable IRQ1 and setup keyboard handler
    mouse_init();    //enable IRQ12 and setup mouse handler
    
    //enable interrupts
    __asm__ volatile ("sti");
    DEBUG_PRINT("Interrupts enabled");
    
    //start timer
    if (using_apic) {
        DEBUG_PRINT("Using APIC timer");
        apic_timer_init(100); //100 Hz
    } else {
        DEBUG_PRINT("Using PIT timer");
        timer_init(100); //100 hz
    }
    
    DEBUG_PRINT("Timer initialized");
    //initialize VFS
    if (vfs_init() == 0) {
        DEBUG_PRINT("VFS initialized successfully");

        //register filesystems with VFS
        if (fs_vfs_init() == 0) {
            DEBUG_PRINT("Filesystems registered with VFS");
            //install initramfs as root
            initramfs_init();
            if (mbi && (mbi->flags & MBI_FLAG_MODS) && mbi->mods_count > 0) {
                multiboot_module_t* mods = (multiboot_module_t*)(uintptr_t)mbi->mods_addr;
                for (uint32_t i = 0; i < mbi->mods_count; i++) {
                    const char* mstr = (const char*)(uintptr_t)mods[i].string;
                    if (!mstr || strstr(mstr, "initramfs") || strstr(mstr, ".cpio")) {
                        const uint8_t* mstart = (const uint8_t*)(uintptr_t)mods[i].mod_start; //identity-mapped low memory
                        const uint8_t* mend   = (const uint8_t*)(uintptr_t)mods[i].mod_end;
                        if (initramfs_load_cpio(mstart, mend) == 0) {
                            #if DEBUG_ENABLED
                            serial_write_string("[init] loaded initramfs module: ");
                            if (mstr) serial_write_string(mstr);
                            serial_write_string("\n");
                            #endif
                            break;
                        }
                    }
                }
            }
            initramfs_install_as_root();
            //now that VFS/initramfs is mounted reload PSF font for fbcon if present
            (void)fbcon_reload_font();
        } else {
            DEBUG_PRINT("Failed to register filesystems with VFS");
            //install initramfs as root since no FS registered still try module
            initramfs_init();
            if (mbi && (mbi->flags & MBI_FLAG_MODS) && mbi->mods_count > 0) {
                multiboot_module_t* mods = (multiboot_module_t*)(uintptr_t)mbi->mods_addr;
                for (uint32_t i = 0; i < mbi->mods_count; i++) {
                    const char* mstr = (const char*)(uintptr_t)mods[i].string;
                    if (!mstr || strstr(mstr, "initramfs") || strstr(mstr, ".cpio")) {
                        const uint8_t* mstart = (const uint8_t*)(uintptr_t)mods[i].mod_start; //identity-mapped low memory
                        const uint8_t* mend   = (const uint8_t*)(uintptr_t)mods[i].mod_end;
                        if (initramfs_load_cpio(mstart, mend) == 0) {
                            break;
                        }
                    }
                }
            }
            initramfs_install_as_root();
            (void)fbcon_reload_font();
        }
    } else {
        DEBUG_PRINT("Failed to initialize VFS");
    }


    const char spinner_chars[] = { '|', '/', '-', '\\' };
    int spin_index = 0;

    int spinner_x = 25;
    int spinner_y = 0;
    DEBUG_PRINT("About to spin");
    SUCCESS_SOUND();
    //spin using actual interrupt-driven timer
    uint64_t start_tick = timer_get_ticks();
    uint64_t last_tick = start_tick;
    while (timer_get_ticks() - start_tick < 50) {
        uint64_t t = timer_get_ticks();
        if (t != last_tick) {
            last_tick = t;
            if ((t % 5) == 0) {
                put_char_at(spinner_chars[spin_index], 0x0F, spinner_x, spinner_y);
                spin_index = (spin_index + 1) % 4;
            }
        }
    }
    kclear();
    SUCCESS_SOUND();
    //try to spawn /bin/init from current root FS
    if (!spawn_user_from_vfs("/bin/init")) {
        //theres no fallback so panic
        kpanic_msg("/bin/init not found in initramfs");
    }
    //idle scheduler will run user processes
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
