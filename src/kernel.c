#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "desktop.h"
#include "io.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "device_manager.h"
#include "drivers/pc_speaker.h"
#include "interrupts/idt.h"
#include "interrupts/pic.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "drivers/ata.h"
#include "syscall.h"
#include "interrupts/gdt.h"
#include "interrupts/tss.h"
#include "drivers/mouse.h"
#include "drivers/tty.h"
#include "fs/fs.h"
#include "fs/fat16.h"
#include "fs/vfs.h"
#include "fs/initramfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "process.h"
#include <stdbool.h>


/* TODO LIST:
    * = done

    - Add MiniFS (see minifs.c)
    - Add more commands
    - Add the Watchdog timer

    Any community contribuations are welcomed and thanked upon :)
*/


#define VID_MEM ((unsigned char*)0xb8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define WATCHDOG_TIMEOUT 500
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

//ACPI variables
#define RSDP_SIG "RSD PTR "
#define ACPI_SIG_RSDT "RSDT"
#define ACPI_SIG_XSDT "XSDT"
#define ACPI_SIG_FADT "FACP"
#define ACPI_SIG_DSDT "DSDT"
#define SLP_EN (1 << 13)
#define SCI_EN (1 << 0)


//global filesystem instance for shell commands
static fat16_fs_t g_fat16_fs;
static bool g_fs_initialized = false;


//structs and typedefs

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} rsdp_descriptor_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oemtableid[8];
    uint32_t oemrevision;
    uint32_t creatorid;
    uint32_t creatorrev;
} acpi_table_header_t;

typedef struct __attribute__((packed)) {
    acpi_table_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
} fadt_t;

typedef void (*cmd_fn)(const char *args);
struct cmd_entry { const char *name; cmd_fn fn; };

//global variables
volatile int currentTick = 0;
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;
static uint32_t total_memory_mb = 0;
static const char* g_panic_reason = 0; //optional reason to display on panic
char *bsodVer = "classic";



//function declarations
void kpanic(void);
void kshutdown(void);
void kreboot(void);
void move_cursor(uint16_t row, uint16_t col);
void kclear(void);
void print(char* msg, unsigned char colour);
void cmd_meminfo(const char *args);
void cmd_console_colour(const char* args);
static void update_cursor(void);
static void scroll_if_needed(void);
static int putchar_term(char c, unsigned char colour);
void enable_cursor(uint8_t start, uint8_t end);
static void hex_dump(const uint8_t* data, size_t size, unsigned char colour);


void watchdogTick(void) {
    currentTick++;
}

//spawn a user program from VFS path by loading it to 0x01000000 and creating a user process
//returns 1 on success 0 on failure
static int spawn_user_from_vfs(const char* path) {
    if (!path || !*path) return 0;
    serial_write_string("\nLoading app from VFS: ");
    serial_write_string(path);
    serial_write_string("\n");

    vfs_node_t* node = vfs_open(path, VFS_FLAG_READ);
    if (!node) {
        serial_write_string("[VFS] File not found\n");
        return 0;
    }

    int fsize = vfs_get_size(node);
    if (fsize <= 0) {
        vfs_close(node);
        serial_write_string("[VFS] Invalid file size\n");
        return 0;
    }

    if (fsize > 4096) fsize = 4096; //ssingle page loader

    //allocate a physical page and map it at a kernel temp address to copy
    const uint32_t entry_va = 0x01000000;
    const uint32_t temp_kmap = 0x00800000;
    uint32_t code_phys = pmm_alloc_page();
    if (!code_phys) {
        vfs_close(node);
        serial_write_string("[VFS] pmm_alloc_page failed\n");
        return 0;
    }
    if (vmm_map_page(temp_kmap, code_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        vfs_close(node);
        pmm_free_page(code_phys);
        serial_write_string("[VFS] map temp page failed\n");
        return 0;
    }

    memset((void*)temp_kmap, 0, 4096);
    //read file into the mapped page
    uint32_t offset = 0;
    while (offset < (uint32_t)fsize) {
        int r = vfs_read(node, offset, (uint32_t)(fsize - offset), (char*)((uint8_t*)temp_kmap + offset));
        if (r <= 0) break;
        offset += (uint32_t)r;
    }
    vfs_close(node);
    vmm_unmap_page_nofree(temp_kmap);

    //create the process
    process_t* proc = process_create("/bin/sh", (void*)entry_va, true);
    if (!proc) {
        pmm_free_page(code_phys);
        serial_write_string("[VFS] process_create failed\n");
        return 0;
    }

    //map code into the process address space only
    if (vmm_map_page_in_directory(proc->page_directory, entry_va, code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        process_destroy(proc);
        pmm_free_page(code_phys);
        serial_write_string("[VFS] map in proc failed\n");
        return 0;
    }

    //create and map a user stack at 0x02000000
    uint32_t ustack_phys = pmm_alloc_page();
    if (!ustack_phys) {
        serial_write_string("[VFS] alloc user stack failed\n");
        process_destroy(proc);
        return 0;
    }
    if (vmm_map_page_in_directory(proc->page_directory, 0x02000000 - 0x1000, ustack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        serial_write_string("[VFS] map user stack in proc failed\n");
        pmm_free_page(ustack_phys);
        process_destroy(proc);
        return 0;
    }
    proc->context.esp = 0x02000000 - 16;

    //debugdump context and mappings (use known physical pages)
    {
        char out[192];
        ksnprintf(out, sizeof(out),
                  "Boot: Spawned %s PID %u entry=0x%08x code_phys=0x%08x stack_top=0x%08x stack_phys=0x%08x\n",
                  path, proc->pid, entry_va, (uint32_t)code_phys, (uint32_t)(0x02000000 - 16), (uint32_t)ustack_phys);
        serial_write_string(out);
        ksnprintf(out, sizeof(out),
                  "Ctx: CS=0x%04x SS=0x%04x DS=0x%04x EFLAGS=0x%08x EIP=0x%08x ESP=0x%08x\n",
                  (unsigned)proc->context.cs, (unsigned)proc->context.ss, (unsigned)proc->context.ds,
                  (unsigned)proc->context.eflags, (unsigned)proc->context.eip, (unsigned)proc->context.esp);
        serial_write_string(out);
    }

    process_yield();
    return 1;
}


//panic with a custom message stores reason and invokes kpanic screen
void kpanic_msg(const char* reason){
    g_panic_reason = reason;
    kpanic();
}

void petWatchdog(void) {
    currentTick = 0;
}

void watchdogCheck(void) {
    if(currentTick > WATCHDOG_TIMEOUT) kpanic();
}


static void cmd_shutdown(const char *args) {
    (void)args;
    kshutdown();
}

static void cmd_reboot(const char *args) {
    (void)args;
    kreboot();
}

static void cmd_minifs(const char *args) {
    (void)args;
    print("\nNo drives attached\n",0x0F);
}

static void cmd_induce(const char *args) {
    (void)args;
    kpanic();
}

static void cmd_clear(const char *args) {
    (void)args;
    kclear();
}

static void cmd_echo(const char *args) {
    if(!args) return;
    while(*args == ' ') args++;
    print("\n",0x0F);
    print((char*)args,0x0F);
    print("\n",0x0F);
}

static void cmd_time(const char *args) {
    (void)args;
    rtc_time_t t;
    if (!rtc_read(&t)) {
        print("\nRTC read failed\n", 0x4F);
        return;
    }
    char buf[64];
    size_t p = 0;
    p += ksnprintf(buf + p, sizeof(buf) - p, "\n%u-", t.year);
    if (t.month < 10 && p + 1 < sizeof(buf)) buf[p++] = '0';
    p += ksnprintf(buf + p, sizeof(buf) - p, "%u-", t.month);
    if (t.day < 10 && p + 1 < sizeof(buf)) buf[p++] = '0';
    p += ksnprintf(buf + p, sizeof(buf) - p, "%u ", t.day);
    if (t.hour < 10 && p + 1 < sizeof(buf)) buf[p++] = '0';
    p += ksnprintf(buf + p, sizeof(buf) - p, "%u:", t.hour);
    if (t.minute < 10 && p + 1 < sizeof(buf)) buf[p++] = '0';
    p += ksnprintf(buf + p, sizeof(buf) - p, "%u:", t.minute);
    if (t.second < 10 && p + 1 < sizeof(buf)) buf[p++] = '0';
    p += ksnprintf(buf + p, sizeof(buf) - p, "%u\n", t.second);
    buf[(p < sizeof(buf)) ? p : (sizeof(buf) - 1)] = '\0';
    print(buf, 0x0F);
}

static void cmd_help(const char *args) {
    (void)args;
    print("\nAvailable commands (type 'help' to show this message):\n", 0x0F);
    print("  meminfo     - Show memory information\n", 0x0F);
    print("  time        - Show current RTC time\n", 0x0F);
    print("  devices     - List all registered devices\n", 0x0F);
    print("  devtest     - Test device functionality\n", 0x0F);
    print("  memtest     - Test memory management\n", 0x0F);
    print("  vmmap <addr> - Show virtual to physical address mapping\n", 0x0F);
    print("  heapinfo    - Show heap information\n", 0x0F);
    print("  ls          - List directory contents\n", 0x0F);
    print("  cat <file>  - Display file contents\n", 0x0F);
    print("  touch <file> - Create a new file\n", 0x0F);
    print("  vfs_test    - Test VFS functionality\n", 0x0F);
    print("  minifs      - Minimal filesystem commands\n", 0x0F);
    print("  clear       - Clear the screen\n", 0x0F);
    print("  colour <c>  - Change console color attribute\n", 0x0F);
    print("  echo <text> - Display text\n", 0x0F);
    print("  desktop     - Start the desktop environment\n", 0x0F);
    print("  iceedit     - ICE (Interpreted Compiled Executable) Editor\n", 0x0F);
    print("  loadapp     - Load and execute application from disk (sector 50)\n", 0x0F);
    print("  readsector <dev> <sector> - Read a sector from a device\n", 0x0F);
    print("  reboot      - Reboot the system\n", 0x0F);
    print("  shutdown    - Shut down the system\n", 0x0F);
    print("  bsodVer <classic|modern> - Set BSOD style (modern=emoticon, classic=fatal exception)\n", 0x0F);
    print("  induce(kernel.panic()) - Trigger kernel panic (debug)\n", 0x0F);
    print("\n", 0x0F);
}

static uint16_t get_line_length(uint16_t row){
    if(row >= SCREEN_HEIGHT) return 0;
    uint16_t len = SCREEN_WIDTH;
    while(len > 0){
        char c = VID_MEM[(row * SCREEN_WIDTH + (len - 1)) * 2];
        if(c != ' ') break;
        len--;
    }
    return len;
}

void cmd_iceedit(const char *args){
    (void)args;

    DEBUG_PRINT("ICE Editor Started");

    kclear();

    print("ICE Editor\n", 0x0F);
    print("F5 - Execute\n", 0x0F);
    print("Use arrow keys to move the cursor.\n", 0x0F);
    print("Esc - Exit to shell\n", 0x0F);
    print("\n", 0x0F);

    //place cursor below header
    cursor_x = 0;
    cursor_y = 4;
    update_cursor();

    //clear any pending keyboard input from the shell before starting editor
    kbd_flush();

    enable_cursor(14, 15);
    uint16_t desired_col = cursor_x;

    for(;;){
        unsigned short ev = kbd_getevent();
        if(!ev) continue;

        //esc keyreturns to shell
        if (ev == 27) {
            print("\n", 0x0F);
            kbd_flush();
            kclear();
            return;
        }

        //arrow keys
        if (ev >= 0xE000) {
            if (ev == K_ARROW_LEFT) {
                if (cursor_x > 0) cursor_x--;
                desired_col = cursor_x;
            } else if (ev == K_ARROW_RIGHT) {
                if (cursor_x < SCREEN_WIDTH - 1) cursor_x++;
                desired_col = cursor_x;
            } else if (ev == K_ARROW_UP) {
                if (cursor_y > 4) {
                    uint16_t target = cursor_y - 1;
                    uint16_t ll = get_line_length(target);
                    uint16_t nx = desired_col;
                    if (nx > ll) nx = ll;
                    if (nx >= SCREEN_WIDTH) nx = SCREEN_WIDTH - 1;
                    cursor_y = target;
                    cursor_x = nx;
                }
            } else if (ev == K_ARROW_DOWN) {
                if (cursor_y < SCREEN_HEIGHT - 1) {
                    uint16_t target = cursor_y + 1;
                    uint16_t ll = get_line_length(target);
                    uint16_t nx = desired_col;
                    if (nx > ll) nx = ll;
                    if (nx >= SCREEN_WIDTH) nx = SCREEN_WIDTH - 1;
                    cursor_y = target;
                    cursor_x = nx;
                }
            }
            update_cursor();
            continue;
        }

        //ASCII input
        char ch = (char)(ev & 0xFF);
        if(!ch) continue;
        if (ch == '\n') {
            putchar_term('\n', 0x0F);
            desired_col = cursor_x;
        } else if (ch == '\b') {
            //prevent deleting header text
            if (!(cursor_y == 4 && cursor_x == 0)) {
                putchar_term('\b', 0x0F);
                desired_col = cursor_x;
            }
        } else {
            putchar_term(ch, 0x0F);
            desired_col = cursor_x;
        }
    }
}

void cmd_kpset(const char *args){
    if(!args) return;

    if(strcmp(args, "classic") != 0 && strcmp(args, "modern") != 0){
        print("Invalid theme\n", 0x4F);
        return;
    }

    strncpy(bsodVer, args, strlen(args));
    bsodVer[strlen(args)] = '\0';
}

static void cmd_loadapp(const char *args) {
    (void)args;
    serial_write_string("\nLoading app from disk sector 50...\n");

    //allocate buffer for one page (8 sectors)
    static uint8_t buffer[4096];

    serial_write_string("Loading user application...\n");
    device_t* ata_dev = device_find_by_name("ata0");
    if (!ata_dev) {
        serial_write_string("No ATA device found!\n");
        return;
    }

    int bytes_read = device_read(ata_dev, 50 * 512, buffer, 4096); //read 8 sectors (4KB) from LBA 50

    if (bytes_read <= 0) {
        serial_write_string("Failed to read from ATA drive\n");
        return;
    }

    //prepare a user-space entry address and a backing physical page for the program
    const uint32_t entry_va = 0x01000000; //16MB in user space
    const uint32_t temp_kmap = 0x00800000; //temporary kernel mapping address for copying (8MB)

    //allocate a physical page to hold the program code
    uint32_t code_phys = pmm_alloc_page();
    if (!code_phys) {
        serial_write_string("Failed to allocate physical page for program code\n");
        return;
    }

    //temporarily map the physical page into the kernel so we can copy the program into it
    if (vmm_map_page(temp_kmap, code_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        serial_write_string("Failed to temporarily map program page into kernel\n");
        pmm_free_page(code_phys);
        return;
    }

    //clear the page and copy the loaded image (up to 4KB)
    memset((void*)temp_kmap, 0, 4096);
    memcpy((void*)temp_kmap, buffer, (bytes_read > 4096) ? 4096 : (size_t)bytes_read);

    //unmap the temporary kernel mapping (the data remains in the physical page)
    vmm_unmap_page_nofree(temp_kmap);

    //create a new user process with the chosen entry point
    process_t* proc = process_create("userapp", (void*)entry_va, true);
    if (!proc) {
        serial_write_string("Failed to create process\n");
        pmm_free_page(code_phys);
        return;
    }

    //map the program page into the new process address space at entry_va (writable for .data/.bss)
    if (vmm_map_page_in_directory(proc->page_directory, entry_va, code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        serial_write_string("Failed to map program into process address space\n");
        process_destroy(proc);
        pmm_free_page(code_phys);
        return;
    }

    //map the program page into the current (kernel) directory
    if (vmm_map_page(entry_va, code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        serial_write_string("Failed to map program into kernel address space\n");
        process_destroy(proc);
        pmm_free_page(code_phys);
        return;
    }

    //create and map a user stack at 0x02000000
    uint32_t ustack_phys = pmm_alloc_page();
    if (!ustack_phys) {
        serial_write_string("Failed to allocate user stack page\n");
        process_destroy(proc);
        return;
    }
    //map into kernel directory (we run with kernel CR3 for now)
    if (vmm_map_page(0x02000000 - 0x1000, ustack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        serial_write_string("Failed to map user stack in kernel directory\n");
        pmm_free_page(ustack_phys);
        process_destroy(proc);
        return;
    }
    //map into the process directory for future per-process CR3 switching
    if (vmm_map_page_in_directory(proc->page_directory, 0x02000000 - 0x1000, ustack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        serial_write_string("Failed to map user stack in process directory\n");
        pmm_free_page(ustack_phys);
        process_destroy(proc);
        return;
    }
    //set the initial user ESP near the top of the stack
    proc->context.esp = 0x02000000 - 16;

    char out[64];
    ksnprintf(out, sizeof(out), "Boot: Spawned user shell PID %u entry=0x%08x\n", proc->pid, entry_va);
    serial_write_string(out);

    //yield to scheduler so the shell runs now
    process_yield();
}

static void cmd_devices(const char *args) {
    (void)args;
    print("\nRegistered devices:\n", 0x0F);
    device_list_all();
}

static void cmd_devtest(const char *args) {
    (void)args;
    print("\nTesting device operations...\n", 0x0F);

    //find ATA device
    device_t* ata_dev = device_find_by_type(DEVICE_TYPE_STORAGE);
    if (!ata_dev) {
        print("No storage device found!\n", 0x0C);
        return;
    }

    //test reading sector 50 via device manager
    static uint8_t buffer[512];
    int result = device_read(ata_dev, 50 * 512, buffer, 512);

    if (result == 512) {
        print("Device read successful! First 16 bytes:\n", 0x0A);
        for (int i = 0; i < 16; i++) {
            char hex[4];
            hex[0] = "0123456789ABCDEF"[buffer[i] >> 4];
            hex[1] = "0123456789ABCDEF"[buffer[i] & 0xF];
            hex[2] = ' ';
            hex[3] = '\0';
            print(hex, 0x0F);
        }
        print("\n", 0x0F);
    } else {
        print("Device read failed!\n", 0x0C);
    }
}

static void cmd_readsector(const char *args) {
    if (!args || !*args) {
        print("\nUsage: readsector <device> <sector>  e.g., readsector ata0 50 or readsector ata0 0x32\n", 0x0F);
        return;
    }

    //skip leading spaces
    while (*args == ' ') args++;

    //parse device name
    const char* dev_start = args;
    while (*args && *args != ' ') args++;
    size_t dev_len = args - dev_start;
    if (dev_len == 0) {
        print("\nError: Missing device name\n", 0x4F);
        return;
    }

    char dev_name[32];
    if (dev_len >= sizeof(dev_name)) {
        print("\nError: Device name too long\n", 0x4F);
        return;
    }
    memcpy(dev_name, dev_start, dev_len);
    dev_name[dev_len] = '\0';

    //skip spaces to sector str
    while (*args == ' ') args++;
    const char* sector_str = args;
    if (!*sector_str) {
        print("\nError: Missing sector number\n", 0x4F);
        return;
    }

    //find device
    device_t* dev = device_find_by_name(dev_name);
    if (!dev) {
        print("\nError: Device not found\n", 0x4F);
        return;
    }

    //parse sector
    uint32_t sector;
    if (!parse_u32(sector_str, &sector)) {
        print("\nError: Invalid sector number\n", 0x4F);
        return;
    }

    //read sector
    uint8_t buffer[512] = {0};  //initialize buffer with zeros
    int bytes_read = device_read(dev, sector * 512UL, buffer, 512);
    if (bytes_read != 512) {
        print("\nError: Failed to read sector\n", 0x4F);
        return;
    }

    print("\nSector contents (hex dump):\n", 0x0F);
    hex_dump(buffer, 512, 0x0F);
}

static void cmd_ls(const char *args) {
    (void)args;

    //open the root directory using VFS
    vfs_node_t* root_dir = vfs_open("/", VFS_FLAG_READ);
    if (!root_dir) {
        print("\nFailed to open root directory\n", 0x0F);
        return;
    }

    //check if it's actually a directory
    if (root_dir->type != VFS_FILE_TYPE_DIRECTORY) {
        vfs_close(root_dir);
        print("\nRoot is not a directory\n", 0x0F);
        return;
    }

    print("\nDirectory listing:\n", 0x0F);

    //read directory entries
    vfs_node_t* entry;
    for (uint32_t i = 0; ; i++) {
        int result = vfs_readdir(root_dir, i, &entry);
        if (result != 0) {
            break; //no more entries
        }

        //print entry name and type
        if (entry->type == VFS_FILE_TYPE_DIRECTORY) {
            char buf[80];
            ksnprintf(buf, sizeof(buf), "  %s <DIR>\n", entry->name);
            print(buf, 0x0F);
        } else {
            char buf[80];
            ksnprintf(buf, sizeof(buf), "  %s (%u bytes)\n", entry->name, vfs_get_size(entry));
            print(buf, 0x0F);
        }

        //close the entry
        vfs_close(entry);
    }

    //close the root directory
    vfs_close(root_dir);
}

static void cmd_cat(const char *args) {
    if (!args || !*args) {
        print("\nUsage: cat <filename>\n", 0x0F);
        return;
    }

    //open the file using VFS
    vfs_node_t* file = vfs_open(args, VFS_FLAG_READ);
    if (!file) {
        print("\nFile not found\n", 0x0F);
        return;
    }

    //check if it's actually a file
    if (file->type != VFS_FILE_TYPE_FILE) {
        vfs_close(file);
        print("\nNot a regular file\n", 0x0F);
        return;
    }

    uint8_t buffer[512];
    int bytes_read;
    uint32_t offset = 0;
    print("\n", 0x0F);

    while ((bytes_read = vfs_read(file, offset, sizeof(buffer), (char*)buffer)) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            uint8_t c = buffer[i];

            //handle common whitespace characters
            if (c == '\n') {
                print("\n", 0x0F);
            } else if (c == '\r') {
                //skip carriage returns (windows line endings)
                continue;
            } else if (c == '\t') {
                print("    ", 0x0F);  //convert tabs to spaces
            } else if (c >= 32 && c <= 126) {
                //only printable ASCII characters
                char str[2] = {c, '\0'};
                print(str, 0x0F);
            }
            //skip all other characters like control chars and extended ascii
        }
        offset += bytes_read;
    }

    vfs_close(file);
    print("\n", 0x0F);
}

static void cmd_memtest(const char *args) {
    (void)args;
    print("\nMemory Management Test\n", 0x0F);

    //show physical memory stats
    char buf[80];
    ksnprintf(buf, sizeof(buf), "Total pages: %u\n", pmm_get_total_pages());
    print(buf, 0x0F);
    ksnprintf(buf, sizeof(buf), "Free pages: %u\n", pmm_get_free_pages());
    print(buf, 0x0F);
    ksnprintf(buf, sizeof(buf), "Used pages: %u\n", pmm_get_used_pages());
    print(buf, 0x0F);

    //test heap allocation
    print("\nTesting heap allocation:\n", 0x0E);

    void* ptr1 = kmalloc(1024);
    ksnprintf(buf, sizeof(buf), "Allocated 1KB at: 0x%x\n", (uint32_t)ptr1);
    print(buf, 0x0F);

    void* ptr2 = kmalloc(2048);
    ksnprintf(buf, sizeof(buf), "Allocated 2KB at: 0x%x\n", (uint32_t)ptr2);
    print(buf, 0x0F);

    void* ptr3 = kmalloc(512);
    ksnprintf(buf, sizeof(buf), "Allocated 512B at: 0x%x\n", (uint32_t)ptr3);
    print(buf, 0x0F);

    //test writing to allocated memory
    if (ptr1) {
        strcpy((char*)ptr1, "Hello from heap memory!");
        ksnprintf(buf, sizeof(buf), "ptr1 contains: %s\n", (char*)ptr1);
        print(buf, 0x0A);
    }

    //free memory
    print("Freeing memory...\n", 0x0E);
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    print("Memory freed successfully!\n", 0x0A);
}

static void cmd_vmmap(const char *args) {
    if (!args || !*args) {
        print("\nUsage: vmmap <virtual_address>\n", 0x0F);
        print("Example: vmmap 0xC0000000\n", 0x0F);
        return;
    }

    uint32_t vaddr;
    if (!parse_u32(args, &vaddr)) {
        print("\nError: Invalid virtual address\n", 0x4F);
        return;
    }

    uint32_t paddr = vmm_get_physical_addr(vaddr);

    char buf[80];
    print("\n", 0x0F);
    ksnprintf(buf, sizeof(buf), "Virtual:  0x%08x\n", vaddr);
    print(buf, 0x0F);

    if (paddr) {
        ksnprintf(buf, sizeof(buf), "Physical: 0x%08x\n", paddr);
        print(buf, 0x0A);
        print("Status: MAPPED\n", 0x0A);
    } else {
        print("Physical: NOT MAPPED\n", 0x0C);
        print("Status: NOT MAPPED\n", 0x0C);
    }
}

static void cmd_heapinfo(const char *args) {
    (void)args;
    print("\nHeap Information\n", 0x0F);

    heap_stats_t stats;
    heap_get_stats(&stats);

    char buf[80];
    ksnprintf(buf, sizeof(buf), "Total heap size: %u bytes\n", stats.total_size);
    print(buf, 0x0F);
    ksnprintf(buf, sizeof(buf), "Used memory: %u bytes\n", stats.used_size);
    print(buf, 0x0F);
    ksnprintf(buf, sizeof(buf), "Free memory: %u bytes\n", stats.free_size);
    print(buf, 0x0F);
    ksnprintf(buf, sizeof(buf), "Number of blocks: %u\n", stats.num_blocks);
    print(buf, 0x0F);
}

static void cmd_vfs_test(const char *args) {
    (void)args;
    print("\n=== VFS Test ===\n", 0x0F);

    //try to resolve the root path
    vfs_node_t* node = vfs_resolve_path("/");
    if (node) {
        print("Root node resolved successfully\n", 0x0A);
        print("Node name: ", 0x0F);
        print(node->name, 0x0F);
        print("\n", 0x0F);
        vfs_close(node);
    } else {
        print("Failed to resolve root path\n", 0x0C);
        return;
    }

    //try to open root directory
    vfs_node_t* root_dir = vfs_open("/", VFS_FLAG_READ);
    if (root_dir) {
        print("Root directory opened successfully\n", 0x0A);

        //try to read directory entries
        vfs_node_t* entry;
        for (uint32_t i = 0; i < 5; i++) { //read first 5 entries
            int result = vfs_readdir(root_dir, i, &entry);
            if (result == 0) {
                char buf[100];
                if (entry->type == VFS_FILE_TYPE_DIRECTORY) {
                    ksnprintf(buf, sizeof(buf), "  Entry %u: %s (DIR)\n", i, entry->name);
                } else {
                    ksnprintf(buf, sizeof(buf), "  Entry %u: %s (%u bytes)\n", i, entry->name, vfs_get_size(entry));
                }
                print(buf, 0x0F);
                vfs_close(entry);
            } else {
                char buf[50];
                ksnprintf(buf, sizeof(buf), "  No entry at index %u\n", i);
                print(buf, 0x0F);
                break;
            }
        }

        vfs_close(root_dir);
    } else {
        print("Failed to open root directory\n", 0x0C);
    }
}

static void cmd_touch(const char *args) {
    if (!g_fs_initialized) {
        print("\nFilesystem not initialized.\n", 0x0C);
        return;
    }
    if (!args || !*args) {
        print("\nUsage: touch <filename>\n", 0x0F);
        return;
    }

    //to avoid issues with leading spaces from the command parser
    while (*args == ' ') args++;

    print("\nCreating file: ", 0x0F);
    print((char*)args, 0x0F);
    print("...\n", 0x0F);

    if (fat16_create_file(&g_fat16_fs, args) == 0) {
        print("File created successfully.\n", 0x0A);
    } else {
        print("Failed to create file. It may already exist or the disk is full.\n", 0x0C);
    }
}


static struct cmd_entry commands[] = {
    {"help", cmd_help},
    {"clear", cmd_clear},
    {"echo", cmd_echo},
    {"meminfo", cmd_meminfo},
    {"time", cmd_time},
    {"colour", cmd_console_colour},
    {"desktop", cmd_desktop},
    {"minifs", cmd_minifs},
    {"shutdown", cmd_shutdown},
    {"loadapp", cmd_loadapp},
    {"devices", cmd_devices},
    {"devtest", cmd_devtest},
    {"reboot", cmd_reboot},
    {"induce(kernel.panic())", cmd_induce},
    {"iceedit", cmd_iceedit},
    {"bsodVer", cmd_kpset},
    {"readsector", cmd_readsector},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"touch", cmd_touch},
    {"memtest", cmd_memtest},
    {"vmmap", cmd_vmmap},
    {"heapinfo", cmd_heapinfo},
    {"vfs_test", cmd_vfs_test},
    {NULL, NULL}
};

void kclear(void){
    unsigned int j = 0;

    while(j < SCREEN_WIDTH * SCREEN_HEIGHT){
        VID_MEM[j * 2] = ' ';
        VID_MEM[j * 2 + 1] = 0x0F;
        j++;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void print(char* msg, unsigned char colour){
    int i = 0;
    while(msg[i] != '\0'){
        char c = msg[i++];
        if (c == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
                VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
            } else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = SCREEN_WIDTH - 1;
                VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
                VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
            }
        } else {
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = c;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
            cursor_x++;
            if (cursor_x >= SCREEN_WIDTH) {
                cursor_x = 0;
                cursor_y++;
            }
        }
        scroll_if_needed();
    }
    update_cursor();
}


static void print_at(const char* str, unsigned char attr, unsigned int x, unsigned int y) {
    unsigned int i = 0;
    unsigned int pos = (y * SCREEN_WIDTH + x) * 2;
    while (str[i]) {
        VID_MEM[pos] = str[i];
        VID_MEM[pos + 1] = attr;
        i++;
        pos += 2;
    }
}

void kpanic(void) {

    __asm__ volatile ("cli");

    // Fill background with blue, white text
    for (unsigned int j = 0; j < SCREEN_WIDTH * SCREEN_HEIGHT; j++) {
        VID_MEM[j * 2] = ' ';
        VID_MEM[j * 2 + 1] = 0x1F; // White on blue
    }

    if(strcmp(bsodVer, "modern") == 0){
        print_at(":(", 0x1F, 0, 0);
        print_at("Your pc ran into a problem and needs to restart.", 0x1F, 0, 1);
        print_at("Please wait while we gather information about this (0%)", 0x1F, 0, 2);
        if (g_panic_reason && g_panic_reason[0]) {
            print_at("Reason:", 0x1F, 0, 3);
            print_at((char*)g_panic_reason, 0x1F, 8, 3);
        } else {
            print_at("Reason: (unspecified)", 0x1F, 0, 3);
        }
    } else{
        print_at(" FrostByte ", 0x71, 35, 4); // Gray background, black text

        if (g_panic_reason && g_panic_reason[0]) {
            print_at("A fatal error has occurred:", 0x1F, 2, 6);
            print_at((char*)g_panic_reason, 0x1F, 2, 7);
        } else {
            print_at("A fatal exception has occurred.", 0x1F, 2, 6);
            print_at("The current application will be terminated.", 0x1F, 2, 7);
        }
        print_at("* Press any key to terminate the current application.", 0x1F, 2, 8);
        print_at("* Press CTRL+ALT+DEL to restart your computer. You will", 0x1F, 2, 9);
        print_at("  lose any unsaved information in all applications.", 0x1F, 2, 10);

        print_at("  Press enter to reboot. ", 0x1F, 25, 15);
        move_cursor(26, 15);
    }

    while (inb(0x64) & 1) {
        (void)inb(0x60);
    }

    ERROR_SOUND();

    // Wait for Enter key manually (polling, no echo)
    for (;;) {
        if (inb(0x64) & 1) { // If output buffer full
            uint8_t scancode = inb(0x60);
            if (scancode == 0x1C) { // Enter key make code
                kreboot();
            }
        }
    }
}


void cmd_meminfo(const char *args) {
    (void)args; //suppress unused parameter warning
    char buf[64];
    ksnprintf(buf, sizeof(buf), "Total memory: %u MB\n", total_memory_mb);
    print(buf, 0x0F);
}



static int putchar_term(char c, unsigned char colour) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = SCREEN_WIDTH - 1;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else {
            return 0;
        }
    } else {
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = c;
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        cursor_x++;
        if (cursor_x >= SCREEN_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll_if_needed();
    update_cursor();
    return 1;
}

static void update_cursor(void){
    unsigned short pos = cursor_y * SCREEN_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll_if_needed(void){
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int y = 1; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                VID_MEM[((y - 1) * SCREEN_WIDTH + x) * 2] = VID_MEM[(y * SCREEN_WIDTH + x) * 2];
                VID_MEM[((y - 1) * SCREEN_WIDTH + x) * 2 + 1] = VID_MEM[(y * SCREEN_WIDTH + x) * 2 + 1];
            }
        }
        int last = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            VID_MEM[(last + x) * 2] = ' ';
            VID_MEM[(last + x) * 2 + 1] = 0x0F;
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
}

static int acpi_checksum(void *ptr, size_t len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (size_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum == 0;
}

static rsdp_descriptor_t* find_rsdp(void) {
    //check EBDA first and suppress warning for low memory access
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
    uint16_t ebda_seg = *(uint16_t*)0x40E;
    #pragma GCC diagnostic pop
    
    uint32_t ebda = ((uint32_t)ebda_seg) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
            if (memcmp((void*)addr, RSDP_SIG, 8) == 0) {
                rsdp_descriptor_t *rsdp = (rsdp_descriptor_t*)addr;
                if (acpi_checksum(rsdp, 20)) {
                    return rsdp;
                }
            }
        }
    }
    
    //check BIOS area
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((void*)addr, RSDP_SIG, 8) == 0) {
            rsdp_descriptor_t *rsdp = (rsdp_descriptor_t*)addr;
            if (acpi_checksum(rsdp, 20)) {
                return rsdp;
            }
        }
    }
    
    return NULL;
}

static acpi_table_header_t* find_acpi_table(acpi_table_header_t *rsdt, const char *signature) {
    if (!rsdt) return NULL;
    
    bool is_xsdt = (memcmp(rsdt->signature, ACPI_SIG_XSDT, 4) == 0);
    uint32_t entry_size = is_xsdt ? 8 : 4;
    uint32_t entries = (rsdt->length - sizeof(acpi_table_header_t)) / entry_size;
    
    uint8_t *table_data = (uint8_t*)rsdt + sizeof(acpi_table_header_t);
    
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t table_addr;
        if (is_xsdt) {
            uint64_t addr64 = *(uint64_t*)(table_data + i * 8);
            if (addr64 > 0xFFFFFFFF) continue; //skip 64-bit addresses
            table_addr = (uint32_t)addr64;
        } else {
            table_addr = *(uint32_t*)(table_data + i * 4);
        }
        
        //map the table header to check signature
        uint32_t page_addr = table_addr & ~(PAGE_SIZE - 1);
        uint32_t offset = table_addr & (PAGE_SIZE - 1);
        uint32_t temp_virt = 0x400000; //use a temporary virtual address
        
        if (vmm_map_page(temp_virt, page_addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            continue; //skip if mapping fails
        }
        
        acpi_table_header_t *table = (acpi_table_header_t*)(temp_virt + offset);
        bool match = (memcmp(table->signature, signature, 4) == 0);
        
        vmm_unmap_page_nofree(temp_virt);
        
        if (match) {
            return (acpi_table_header_t*)table_addr; //return physical address
        }
    }
    
    return NULL;
}

static uint16_t find_s5_sleep_type(acpi_table_header_t *dsdt) {
    if (!dsdt) return 5; //default fallback
    
    uint8_t *data = (uint8_t*)dsdt;
    uint32_t length = dsdt->length;
    
    //search for "_S5_" followed by a package
    for (uint32_t i = 0; i < length - 10; i++) {
        if (data[i] == '_' && data[i+1] == 'S' && data[i+2] == '5' && data[i+3] == '_') {
            //look for package op (0x12) nearby
            for (uint32_t j = i + 4; j < i + 20 && j < length - 5; j++) {
                if (data[j] == 0x12) { //package op
                    //skip package length encoding
                    uint32_t k = j + 1;
                    if (data[k] & 0xC0) k += (data[k] >> 6) & 3; //multi-byte length
                    k++; //skip element count
                    
                    //look for first integer value
                    if (k < length) {
                        if (data[k] == 0x0A && k + 1 < length) { //byte const
                            uint8_t val = data[k + 1];
                            if (val > 0 && val < 8) return val;
                        } else if (data[k] == 0x01) { //one
                            return 1;
                        }
                    }
                    break;
                }
            }
        }
    }
    
    return 5; //default S5 sleep type
}

void kshutdown(void) {
    DEBUG_PRINT("Initiating shutdown...");
    
    //find RSDP
    rsdp_descriptor_t *rsdp = find_rsdp();
    if (!rsdp) {
        DEBUG_PRINT("RSDP not found, using fallback");
        goto fallback_shutdown;
    }
    DEBUG_PRINT("RSDP found");
    
    //get RSDT/XSDT
    uint32_t rsdt_phys = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address && (rsdp->xsdt_address >> 32) == 0) {
        rsdt_phys = (uint32_t)rsdp->xsdt_address;
    } else if (rsdp->rsdt_address) {
        rsdt_phys = rsdp->rsdt_address;
    }
    
    if (!rsdt_phys) {
        DEBUG_PRINT("RSDT/XSDT not found");
        goto fallback_shutdown;
    }
    DEBUG_PRINT("RSDT/XSDT address found");
    
    //map RSDT/XSDT
    uint32_t rsdt_page = rsdt_phys & ~(PAGE_SIZE - 1);
    uint32_t rsdt_offset = rsdt_phys & (PAGE_SIZE - 1);
    uint32_t rsdt_virt = 0x500000;
    
    if (vmm_map_page(rsdt_virt, rsdt_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        DEBUG_PRINT("Failed to map RSDT/XSDT");
        goto fallback_shutdown;
    }
    DEBUG_PRINT("RSDT/XSDT mapped");
    
    acpi_table_header_t *rsdt = (acpi_table_header_t*)(rsdt_virt + rsdt_offset);
    
    //find FADT
    uint32_t fadt_phys = (uint32_t)find_acpi_table(rsdt, ACPI_SIG_FADT);
    if (!fadt_phys) {
        DEBUG_PRINT("FADT not found");
        vmm_unmap_page_nofree(rsdt_virt);
        goto fallback_shutdown;
    }
    DEBUG_PRINT("FADT found");
    
    //map FADT
    uint32_t fadt_page = fadt_phys & ~(PAGE_SIZE - 1);
    uint32_t fadt_offset = fadt_phys & (PAGE_SIZE - 1);
    uint32_t fadt_virt = 0x600000;
    
    if (vmm_map_page(fadt_virt, fadt_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        DEBUG_PRINT("Failed to map FADT");
        vmm_unmap_page_nofree(rsdt_virt);
        goto fallback_shutdown;
    }
    
    fadt_t *fadt = (fadt_t*)(fadt_virt + fadt_offset);
    
    //get PM1a control register
    uint32_t pm1a_cnt = fadt->pm1a_cnt_blk;
    if (!pm1a_cnt) {
        DEBUG_PRINT("PM1a control register not found");
        vmm_unmap_page_nofree(fadt_virt);
        vmm_unmap_page_nofree(rsdt_virt);
        goto fallback_shutdown;
    }
    serial_printf("PM1a control register: 0x%x\n", pm1a_cnt);
    
    //map and parse DSDT for S5 sleep type
    uint32_t dsdt_phys = fadt->dsdt;
    uint16_t slp_typ = 5; //default
    
    if (dsdt_phys) {
        uint32_t dsdt_page = dsdt_phys & ~(PAGE_SIZE - 1);
        uint32_t dsdt_offset = dsdt_phys & (PAGE_SIZE - 1);
        uint32_t dsdt_virt = 0x700000;
        
        if (vmm_map_page(dsdt_virt, dsdt_page, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
            acpi_table_header_t *dsdt = (acpi_table_header_t*)(dsdt_virt + dsdt_offset);
            slp_typ = find_s5_sleep_type(dsdt);
            vmm_unmap_page_nofree(dsdt_virt);
        }
    }
    serial_printf("S5 sleep type: %d\n", slp_typ);
    
    //enable ACPI if needed
    if (fadt->smi_cmd && fadt->acpi_enable) {
        serial_printf("Enabling ACPI via SMI_CMD=0x%x\n", fadt->smi_cmd);
        uint16_t pm1a_sts = inw(pm1a_cnt);
        if (!(pm1a_sts & SCI_EN)) {
            outb(fadt->smi_cmd, fadt->acpi_enable);
            //wait for ACPI to be enabled
            for (int i = 0; i < 100; i++) {
                if (inw(pm1a_cnt) & SCI_EN) break;
                for (volatile int j = 0; j < 10000; j++);
            }
        }
    }
    
    //perform shutdown
    uint16_t pm1a_val = inw(pm1a_cnt);
    serial_printf("PM1a original value: 0x%x\n", pm1a_val);
    pm1a_val &= ~(7 << 10); //clear SLP_TYP
    pm1a_val |= (slp_typ << 10) | SLP_EN;
    serial_printf("PM1a shutdown value: 0x%x\n", pm1a_val);
    
    //disable interrupts for atomic shutdown
    asm volatile("cli");
    
    //write PM1b first if it exists
    if (fadt->pm1b_cnt_blk) {
        uint16_t pm1b_val = inw(fadt->pm1b_cnt_blk);
        pm1b_val &= ~(7 << 10);
        pm1b_val |= (slp_typ << 10) | SLP_EN;
        outw(fadt->pm1b_cnt_blk, pm1b_val);
    }
    
    //write PM1a last to trigger shutdown
    outw(pm1a_cnt, pm1a_val);
    
    //try alternative QEMU ACPI approach if PM1a is at 0x604
    if (pm1a_cnt == 0x604) {
        serial_printf("Trying QEMU ACPI shutdown with 0x2000\n");
        outw(0x604, 0x2000);  // QEMU's expected shutdown value
    }
    
    //give it a moment
    for (volatile int i = 0; i < 1000; i++);
    
    //cleanup mappings
    vmm_unmap_page_nofree(fadt_virt);
    vmm_unmap_page_nofree(rsdt_virt);
    
    serial_printf("ACPI shutdown initiated\n");
    
    //if we reach here ACPI shutdown didn't work so halt forever
    for (;;) {
        asm volatile("hlt");
    }
    
fallback_shutdown:
    DEBUG_PRINT("ACPI shutdown failed, trying fallback methods");
    //try QEMU/Bochs specific ports
    outw(0x604, 0x2000);  //QEMU
    outw(0xB004, 0x2000); //bochs
    outb(0xF4, 0x00);     //QEMU isa-debug-exit
    
    //if all else fails just halt
    for (;;) {
        asm volatile("hlt");
    }
}

//works in qemu idk how about real hardware
//TODO: should change this to reboot using ACPI
void kreboot(void){
    __asm__ volatile("cli");
    //port 0xCF9 (reset control register)
    outb(0xCF9, 0x02); //set reset bit
    for (volatile unsigned int i = 0; i < 100000; ++i) { }
    outb(0xCF9, 0x06); //full reset
}

static inline void write_char_at(uint16_t row, uint16_t col, char c, uint8_t attr){
    if (row >= SCREEN_HEIGHT) return;
    if (col >= SCREEN_WIDTH) {
        row += col / SCREEN_WIDTH;
        col = col % SCREEN_WIDTH;
        if (row >= SCREEN_HEIGHT) return;
    }
    volatile uint16_t *vm = (volatile uint16_t*)0xB8000;
    vm[row * SCREEN_WIDTH + col] = ((uint16_t)attr << 8) | (uint8_t)c;
}

void enable_cursor(uint8_t start, uint8_t end){
    outb(0x3D4,0x0A);
    outb(0x3D5,(inb(0x3D5)&0xC0)|start);
    outb(0x3D4,0x0B);
    outb(0x3D5,(inb(0x3D5)&0xE0)|end);
}

void move_cursor(uint16_t row, uint16_t col){
    uint16_t pos = row * SCREEN_WIDTH + col;
    outb(0x3D4,0x0F);
    outb(0x3D5,(uint8_t)(pos & 0xFF));
    outb(0x3D4,0x0E);
    outb(0x3D5,(uint8_t)((pos >> 8) & 0xFF));
}


//get keyboard input through device manager
char getkey_device_manager(void) {
    //find keyboard device hardcoded to PS/2 for now
    device_t* kbd_device = device_find_by_name("ps2kbd0");
    if (!kbd_device) {
        return 0; //no keyboard device found
    }

    //poll for input through device manager
    char buffer[1];
    int bytes_read = device_read(kbd_device, 0, buffer, 1);
    if (bytes_read > 0) {
        return buffer[0];
    }
    return 0; //no input available
}

//blocking version that waits for input
char getkey_blocking_device_manager(void) {
    for(;;) {
        char ch = getkey_device_manager();
        if (ch != 0) {
            return ch;
        }
        //small delay to avoid busy waiting
        for(volatile int i = 0; i < 1000; i++);
    }
}

//now uses device manager for keyboard input
void input(char *buffer, size_t size){
    size_t len = 0;
    for(size_t i = 0; i < size; ++i) buffer[i] = 0;
    enable_cursor(14, 15);
    for(;;){
        char ch = getkey_blocking_device_manager(); //blocks until a key is available through device manager
        if(!ch) continue;
        if(ch == '\n'){
            buffer[len] = 0;
            putchar_term('\n', 0x0F);
            return;
        } else if(ch == '\b'){
            if(len > 0){
                len--;
                buffer[len] = 0;
                putchar_term('\b', 0x0F);
            }
            continue;
        } else if((unsigned char)ch >= 32 && (unsigned char)ch < 127){
            if(len < size - 1){
                buffer[len++] = ch;
                buffer[len] = 0;
                putchar_term(ch, 0x0F);
            }
        }
    }
}

static void hex_dump(const uint8_t* data, size_t size, unsigned char colour) {
    char buf[80];
    for (size_t i = 0; i < size; i += 16) {
        size_t len = 0;
        len += ksnprintf(buf + len, sizeof(buf) - len, "%08x: ", (uint32_t)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                len += ksnprintf(buf + len, sizeof(buf) - len, "%02x ", data[i + j]);
            } else {
                len += ksnprintf(buf + len, sizeof(buf) - len, "   ");
            }
        }
        len += ksnprintf(buf + len, sizeof(buf) - len, " ");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                char c = data[i + j];
                len += ksnprintf(buf + len, sizeof(buf) - len, "%c", (c >= 32 && c <= 126) ? c : '.');
            } else {
                len += ksnprintf(buf + len, sizeof(buf) - len, " ");
            }
        }
        buf[len] = '\0';
        print(buf, colour);
        print("\n", colour);
    }
}

void cmd_console_colour(const char* args){
    unsigned char attr = 0x0F;
    if(!parse_u8(args, &attr)){
        print("\nUsage: colour <attr>  e.g., colour 15 or colour 0x1F\n", 0x0F);
        return;
    }
    kclear();
    unsigned int j = 0;
    while(j < SCREEN_WIDTH * SCREEN_HEIGHT){
        VID_MEM[j * 2] = ' ';
        VID_MEM[j * 2 + 1] = attr;
        j++;
    }
}




void commandLoop(void){
    char buffer[128];
    while(1){
        print("root@frostbyteos > ", 0x0F);
        input(buffer, sizeof(buffer));
        size_t i = 0;
        while(buffer[i] && buffer[i] == ' ') i++;
        size_t start = i;
        while(buffer[i] && buffer[i] != ' ') i++;
        size_t cmdlen = i - start;

        //handle empty command (just pressed enter)
        if(cmdlen == 0) {
            print("\n", 0x0F);
            continue;
        }

        for(struct cmd_entry *ce = commands; ce->name; ++ce){
            size_t n = 0;
            while(ce->name[n]) n++;
            if(n == cmdlen && strncasecmp_custom(ce->name, &buffer[start], cmdlen) == 0){
                const char *args = buffer + i;
                if(*args == ' ') args++;
                ce->fn(args);
                goto cont;
            }
        }
        print("\nError: Invalid command. Type 'help' for available commands.\n", 0x4F);
        cont: ;
    }
}


void put_char_at(char c, uint8_t attr, int x, int y) {
    int offset = (y * SCREEN_WIDTH + x) * 2;
    VID_MEM[offset] = c;
    VID_MEM[offset + 1] = attr;
}

void kmain(uint32_t magic, uint32_t addr) {
    (void)magic; // unused
    uint32_t mem_lower = *((uint32_t*)(addr + 4));
    uint32_t mem_upper = *((uint32_t*)(addr + 8));

    total_memory_mb = (mem_lower + mem_upper) / 1024 + 1;

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
    pmm_init(mem_lower, mem_upper);
    DEBUG_PRINT("Physical memory manager initialized");

    vmm_init();
    DEBUG_PRINT("Virtual memory manager initialized - paging enabled!");

    heap_init();
    DEBUG_PRINT("Heap initialized");

    //initialize device manager
    device_manager_init();
    DEBUG_PRINT("Device manager initialized");

    //initialize and register ATA driver
    ata_init();
    DEBUG_PRINT("ATA driver initialized");

    ata_probe_and_register();
    DEBUG_PRINT("ATA device probing complete");

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
    
    timer_init(100); //100 hz
    keyboard_init(); //enable IRQ1 and setup keyboard handler
    DEBUG_PRINT("Timer initialized");
    __asm__ volatile ("sti");
    DEBUG_PRINT("Interrupts enabled");
    //initialize VFS
    if (vfs_init() == 0) {
        DEBUG_PRINT("VFS initialized successfully");

        //fegister filesystems with VFS
        if (fs_vfs_init() == 0) {
            DEBUG_PRINT("Filesystems registered with VFS");
            //rn assume that you want to mount the root filesystem on the first ATA device later parse params from multiboot
            //find ATA device and mount the root filesystem
            device_t* ata_dev = device_find_by_type(DEVICE_TYPE_STORAGE);
            if (ata_dev) {
                //mount the root filesystem with FAT16
                if (vfs_mount("ata0", "/", "fat16") == 0) {
                    DEBUG_PRINT("Root filesystem mounted successfully");
                    //manually initialize global fs object
                    if (fat16_init(&g_fat16_fs, ata_dev) == 0) {
                        g_fs_initialized = true;
                        DEBUG_PRINT("Global FAT16 FS object initialized for commands");
                    } else {
                        DEBUG_PRINT("Failed to init global FAT16 FS object");
                    }
                } else {
                    DEBUG_PRINT("Failed to mount root filesystem");
                    //install initramfs as root
                    initramfs_init();
                    initramfs_populate_builtin();
                    initramfs_install_as_root();
                }
            } else {
                DEBUG_PRINT("No storage device found");
                //install initramfs as root (no disk available)
                initramfs_init();
                initramfs_populate_builtin();
                initramfs_install_as_root();
            }
        } else {
            DEBUG_PRINT("Failed to register filesystems with VFS");
            //install initramfs as root since no FS registered
            initramfs_init();
            initramfs_populate_builtin();
            initramfs_install_as_root();
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
    //try to spawn /bin/init from current root FS (FAT16 or initramfs)
    if (!spawn_user_from_vfs("/bin/init")) {
        //ff not found on current root (e.g., FAT16 without /bin/sh) switch to initramfs
        vfs_unmount("/");
        initramfs_init();
        initramfs_populate_builtin();
        initramfs_install_as_root();
        if (!spawn_user_from_vfs("/bin/init")) {
            //try /bin/sh next  in cas /bin/init missing
            if (!spawn_user_from_vfs("/bin/sh")) {
            //final fallbackboot into user-space shell loaded from disk sector 50
            cmd_loadapp(NULL);
            }
        }
    }
    //idle scheduler will run user processes
    for (;;) { __asm__ volatile ("hlt"); }
}
