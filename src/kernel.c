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
#include "drivers/sb16.h"
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
#include "kernel/cga.h"
#include "kernel/panic.h"
#include "kernel/kreboot.h"
#include "kernel/kshutdown.h"
#include "kernel/elf.h"
#include "kernel/multiboot.h"
#include <stdbool.h>


/* TODO LIST:
    * = done

    - Add MiniFS (see minifs.c)
    - Add more commands
    - Add the Watchdog timer

    Any community contribuations are welcomed and thanked upon :)
*/


#define WATCHDOG_TIMEOUT 500


//global filesystem instance for shell commands
static fat16_fs_t g_fat16_fs;
static bool g_fs_initialized = false;

typedef void (*cmd_fn)(const char *args);
struct cmd_entry { const char *name; cmd_fn fn; };

//global variables
volatile int currentTick = 0;
static uint32_t total_memory_mb = 0;
char *bsodVer = "classic";

extern char kernel_start, kernel_end;

//function declarations
void cmd_meminfo(const char *args);
void cmd_console_colour(const char* args);
static void hex_dump(const uint8_t* data, size_t size, unsigned char colour);


void watchdogTick(void) {
    currentTick++;
}

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

    print("  readsector <dev> <sector> - Read a sector from a device\n", 0x0F);
    print("  reboot      - Reboot the system\n", 0x0F);
    print("  shutdown    - Shut down the system\n", 0x0F);
    print("  bsodVer <classic|modern> - Set BSOD style (modern=emoticon, classic=fatal exception)\n", 0x0F);
    print("  induce(kernel.panic()) - Trigger kernel panic (debug)\n", 0x0F);
    print("\n", 0x0F);
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
    move_cursor(cursor_y, cursor_x);

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
            move_cursor(cursor_y, cursor_x);
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


void cmd_meminfo(const char *args) {
    (void)args; //suppress unused parameter warning
    char buf[64];
    ksnprintf(buf, sizeof(buf), "Total memory: %u MB\n", total_memory_mb);
    print(buf, 0x0F);
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


void kmain(uint32_t magic, uint32_t addr) {
    (void)magic; // unused
    multiboot_info_t* mbi = (multiboot_info_t*)addr;
    uint32_t mem_lower = (mbi && (mbi->flags & MBI_FLAG_MEM)) ? mbi->mem_lower : 0;
    uint32_t mem_upper = (mbi && (mbi->flags & MBI_FLAG_MEM)) ? mbi->mem_upper : 0;

    total_memory_mb = (mem_lower + mem_upper) / 1024 + 1;

    //expose kernel cmdline via procfs for init to read
    const char* boot_cmdline = (mbi && mbi->cmdline) ? (const char*)mbi->cmdline : "";
    procfs_set_cmdline(boot_cmdline);

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

    //initialize serial RTC and SB16 devices
    (void)serial_register_device();
    (void)rtc_register_device();
    (void)sb16_register_device();

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
    commandLoop();
    //idle scheduler will run user processes
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
extern char kernel_start, kernel_end;
