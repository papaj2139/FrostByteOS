#include <string.h>
#include <stdint.h>
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
#include <stdlib.h>

//forward declaration for direct syscall testing
extern int32_t syscall_dispatch(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
extern void enter_user_mode(void);

/* TODO LIST: 
    * = done
    
    * Fix memory showing as 0mb
    - Add MiniFS (see minifs.c)
    - Add more commands
    - Add the Watchdog timer

    Any community contribuations are welcomed and thanked upon :)
*/


#define VID_MEM ((unsigned char*)0xb8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define WATCHDOG_TIMEOUT 500
#define RSDP_SIG "RSD PTR "
#define ACPI_SIG_FADT "FACP"
#define ACPI_SIG_RSDT "RSDT"
#define ACPI_SIG_XSDT "XSDT"
#define SLP_EN (1 << 13)
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64


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
    print("\nAvailable commands (type 'help' to show this message):\n\n", 0x0F);
    
    print("System Information:\n", 0x0E);
    print("  meminfo     - Show memory information\n", 0x0F);
    print("  time        - Show current RTC time\n", 0x0F);
    print("  devices     - List all registered devices\n", 0x0F);
    print("  devtest     - Test device functionality\n", 0x0F);
    print("  minifs      - Minimal filesystem commands\n\n", 0x0F);
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
    print("\nLoading app from disk sector 50...\n", 0x0F);
    
    //allocate buffer for one sector
    static uint8_t buffer[512];
    
    print("Loading user application...\n", 0x0F);
    device_t* ata_dev = device_find_by_name("ata0");
    if (!ata_dev) {
        print("ATA device ata0 not found!\n", 0x0F);
        return;
    }

    int bytes_read = device_read(ata_dev, 50 * 512, buffer, 512); //read 1 sector from LBA 50

    if (bytes_read == 512) {
        memcpy((void*)0x1000000, buffer, 512);  //safe address
        print("User application loaded at 0x1000000\n", 0x0F);
        print("About to switch to user mode...\n", 0x0F);
        
        //check if we actually have valid code
        uint32_t* code = (uint32_t*)0x1000000;
        print("First 4 bytes of loaded code: ", 0x0F);
        char hex[12];
        for (int i = 0; i < 8; i++) {
            hex[i] = "0123456789ABCDEF"[((uint8_t*)code)[i/2] >> ((i%2) ? 0 : 4) & 0xF];
        }
        hex[8] = '\n';
        hex[9] = '\0';
        print(hex, 0x0F);
        
        //disable interrupts during user mode switch
        __asm__ volatile ("cli");
        
        print("Switching to user mode now...\n", 0x0F);
    } else {
        print("Failed to read from ATA drive\n", 0x0F);
        return;
    }

    //switch to user mode and execute the app
    __asm__ volatile (
        "cli\n"                  //disable interrupts
        "mov $0x23, %%ax\n"     //user data segment with RPL 3
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        
        //set up stack frame for iret
        "pushl $0x23\n"         //SS (user data segment with RPL 3)
        "pushl $0x2000000\n"    //ESP (user stack pointer)
        "pushf\n"               //EFLAGS
        "popl %%eax\n"          //get current flags
        "andl $~0x200, %%eax\n" //clear IF to disable interrupts in user mode
        "pushl %%eax\n"         //push modified flags
        "pushl $0x1B\n"         //CS (user code segment with RPL 3)
        "pushl $0x1000000\n"    //EIP (entry point)
        "iret\n"                //switch to user mode
        :
        :
        : "eax", "memory"
    );
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


static int acpi_checksum(void *ptr, unsigned long len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (unsigned long i = 0; i < len; ++i) sum += p[i];
    return sum == 0;
}

static rsdp_descriptor_t* find_rsdp(void){
    uint16_t *ebda_ptr = (uint16_t*)0x40E;
    uint32_t ebda_kb = 0;
    //suppress array bounds warning for low memory access
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
    if (ebda_ptr) ebda_kb = *ebda_ptr;
    #pragma GCC diagnostic pop
    if (ebda_kb) {
        uint32_t ebda = ((uint32_t)ebda_kb) << 4;
        for (uint32_t p = ebda; p < ebda + 1024; p += 16) {
            if (memcmp((void*)p, RSDP_SIG, 8) == 0) {
                rsdp_descriptor_t *r = (rsdp_descriptor_t*)p;
                if (acpi_checksum(r, 20)) return r;
            }
        }
    }
    for (uint32_t p = 0xE0000; p < 0x100000; p += 16) {
        if (memcmp((void*)p, RSDP_SIG, 8) == 0) {
            rsdp_descriptor_t *r = (rsdp_descriptor_t*)p;
            if (r->revision >= 2) {
                if (acpi_checksum(r, r->length)) return r;
            } else {
                if (acpi_checksum(r, 20)) return r;
            }
        }
    }
    return 0;
}

static acpi_table_header_t* get_rsdt_xsdt(rsdp_descriptor_t *rsdp){
    if (!rsdp) return 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        acpi_table_header_t *xsdt = (acpi_table_header_t*)(uintptr_t)rsdp->xsdt_address;
        if (memcmp(xsdt->signature, ACPI_SIG_XSDT, 4) == 0 && acpi_checksum(xsdt, xsdt->length)) return xsdt;
    }
    if (rsdp->rsdt_address) {
        acpi_table_header_t *rsdt = (acpi_table_header_t*)(uintptr_t)rsdp->rsdt_address;
        if (memcmp(rsdt->signature, ACPI_SIG_RSDT, 4) == 0 && acpi_checksum(rsdt, rsdt->length)) return rsdt;
    }
    return 0;
}

static acpi_table_header_t* find_table(acpi_table_header_t *rsdt_xsdt, const char *sig){
    if (!rsdt_xsdt) return 0;
    unsigned long entry_size = 0;
    if (memcmp(rsdt_xsdt->signature, ACPI_SIG_XSDT, 4) == 0) entry_size = 8;
    else entry_size = 4;
    uint8_t *base = (uint8_t*)rsdt_xsdt;
    uint32_t len = rsdt_xsdt->length;
    uint32_t hdr = sizeof(acpi_table_header_t);
    uint32_t count = (len - hdr) / entry_size;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t addr;
        if (entry_size == 8) {
            uint64_t *entries = (uint64_t*)(base + hdr);
            addr = (uintptr_t)entries[i];
        } else {
            uint32_t *entries = (uint32_t*)(base + hdr);
            addr = (uintptr_t)entries[i];
        }
        acpi_table_header_t *t = (acpi_table_header_t*)addr;
        if (t && memcmp(t->signature, sig, 4) == 0) {
            if (acpi_checksum(t, t->length)) return t;
        }
    }
    return 0;
}

static uint32_t read_pm1a_cnt_blk(acpi_table_header_t *fadt_hdr){
    if (!fadt_hdr) return 0;
    uint8_t *fadt = (uint8_t*)fadt_hdr;
    if (fadt_hdr->length >= 88) {
        uint32_t pm1a = *(uint32_t*)(fadt + 64);
        if (pm1a != 0 && pm1a != 0xFFFFFFFF) return pm1a;
    }
    if (fadt_hdr->length >= 68) {
        uint16_t pm1a16 = *(uint16_t*)(fadt + 64);
        if (pm1a16 != 0 && pm1a16 != 0xFFFF) return (uint32_t)pm1a16;
    }
    return 0;
}

static int find_s5_sleep_type(acpi_table_header_t *fadt_hdr, uint16_t *out_slp_typa){
    if (!fadt_hdr || !out_slp_typa) return 0;
    uint8_t *fadt = (uint8_t*)fadt_hdr;
    uint32_t dsdt_addr = 0;
    if (fadt_hdr->length >= 44) {
        dsdt_addr = *(uint32_t*)(fadt + 40);
    }
    if (!dsdt_addr) return 0;
    acpi_table_header_t *dsdt = (acpi_table_header_t*)(uintptr_t)dsdt_addr;
    if (!dsdt || !acpi_checksum(dsdt, dsdt->length)) return 0;
    uint8_t *data = (uint8_t*)dsdt;
    uint32_t len = dsdt->length;
    for (uint32_t i = 0; i + 4 < len; ++i) {
        if (data[i] == '_' && data[i+1] == 'S' && data[i+2] == '5') {
            uint32_t j = i + 3;
            if (data[j] == '_') j++;
            uint32_t limit = (j + 64 < len) ? (j + 64) : len;
            for (uint32_t k = j; k < limit; ++k) {
                uint8_t b = data[k];
                if (b == 0x12) {
                    uint32_t p = k + 1;
                    uint8_t pkg_len = data[p++];
                    if (pkg_len & 0x80) pkg_len = data[p++];
                    for (uint32_t e = p; e < p + pkg_len && e + 1 < len; ++e) {
                        if (data[e] == 0x0A) {
                            uint8_t val = data[e+1];
                            *out_slp_typa = (uint16_t)val;
                            return 1;
                        } else if (data[e] == 0x0C) {
                            uint16_t val = *(uint16_t*)&data[e+1];
                            *out_slp_typa = val;
                            return 1;
                        } else if ((data[e] & 0xF0) == 0x70) {
                            *out_slp_typa = (uint16_t)(data[e] & 0x0F);
                            return 1;
                        }
                    }
                    break;
                }
                if (b == 0x0A && k + 1 < limit) {
                    *out_slp_typa = data[k+1];
                    return 1;
                }
            }
        }
    }
    return 0;
}

void kshutdown(void){
    rsdp_descriptor_t *rsdp = find_rsdp();
    if (rsdp) {
        acpi_table_header_t *rsdt_xsdt = get_rsdt_xsdt(rsdp);
        if (rsdt_xsdt) {
            acpi_table_header_t *fadt = find_table(rsdt_xsdt, ACPI_SIG_FADT);
            if (fadt) {
                uint32_t pm1a = read_pm1a_cnt_blk(fadt);
                uint16_t slp_typ = 0;
                int got_s5 = find_s5_sleep_type(fadt, &slp_typ);
                if (pm1a) {
                    if (!got_s5) slp_typ = 5;
                    uint16_t slp = (slp_typ & 0x7) << 10;
                    if (pm1a <= 0xFFFF) {
                        outw((uint16_t)pm1a, slp | SLP_EN);
                        for (volatile unsigned long long i = 0; i < 1000000; i++);
                    }
                }
            }
        }
    }
    __asm__ volatile("mov $0x2000, %%eax\n\t"
                     "mov $0x604, %%dx\n\t"
                     "out %%ax, %%dx" : : : "eax", "edx");
    outw(0x604, 0x2000 | SLP_EN);
    kpanic();
}

//works in qemu idk how about real hardware 
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

    //initialize interrupts
    pic_remap(0x20, 0x28);
    DEBUG_PRINT("PIC remapped");
    idt_install();
    DEBUG_PRINT("IDT installed");
    syscall_init();
    DEBUG_PRINT("Syscalls initialized");
    timer_init(100); //100 hz
    keyboard_init(); //enable IRQ1 and setup keyboard handler
    DEBUG_PRINT("Timer initialized");
    __asm__ volatile ("sti");
    DEBUG_PRINT("Interrupts enabled");

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
    commandLoop();
    kpanic(); //if we return here
}
