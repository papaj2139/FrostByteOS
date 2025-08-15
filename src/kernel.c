#include "string.h"
#include <stdint.h>
#include "desktop.h"

/* TODO LIST: 
    - Fix memory showing as 0mb
    - Add MiniFS (see minifs.c)
    - Add more commands
    - Add the Watchdog timer

    Any community contribuations are welcomed and thanked upon :)
*/

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define VID_MEM ((unsigned char*)0xb8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define WATCHDOG_TIMEOUT 500

void kpanic(void);
void kshutdown(void);

volatile int currentTick = 0;
void watchdogTick(void){ currentTick++; }
void petWatchdog(void){ currentTick = 0; }
void watchdogCheck(void){ if(currentTick > WATCHDOG_TIMEOUT) kpanic(); }

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

static void update_cursor(void);
static void scroll_if_needed(void);

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


void print_at(const char* str, unsigned char attr, unsigned int x, unsigned int y) {
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

    print_at(" FrostByte ", 0x71, 35, 4); // Gray background, black text


    print_at("A fatal exception 0E has occurred at 0028:C0044526 in VXD VFAT(01) + 00002D6A.", 0x1F, 2, 6);
    print_at("The current application will be terminated.", 0x1F, 2, 7);
    print_at("* Press any key to terminate the current application.", 0x1F, 2, 8);
    print_at("* Press CTRL+ALT+DEL to restart your computer. You will", 0x1F, 2, 9);
    print_at("  lose any unsaved information in all applications.", 0x1F, 2, 10);

    print_at(" Press enter to shutdown. ", 0x1F, 25, 15);

    while (inb(0x64) & 1) {
        (void)inb(0x60);
    }

    // Wait for Enter key manually (polling, no echo)
    for (;;) {
        if (inb(0x64) & 1) { // If output buffer full
            uint8_t scancode = inb(0x60);
            if (scancode == 0x1C) { // Enter key make code
                kshutdown();
            }
        }
    }
}

static uint32_t total_memory_mb = 0;

static void detect_memory(void) {
    struct e820_entry {
        uint64_t base;
        uint64_t length;
        uint32_t type;
    } __attribute__((packed));

    struct e820_entry *entries = (struct e820_entry *)0x8000; 
    uint32_t num_entries = *((uint32_t*)0x7E00);

    total_memory_mb = 0;
    for (uint32_t i = 0; i < num_entries; i++) {
        if (entries[i].type == 1) { 
            total_memory_mb += (uint32_t)(entries[i].length / (1024 * 1024));
        }
    }
}

void cmd_meminfo(const char *args) {
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

#define RSDP_SIG "RSD PTR "
#define ACPI_SIG_FADT "FACP"
#define ACPI_SIG_RSDT "RSDT"
#define ACPI_SIG_XSDT "XSDT"
#define SLP_EN (1 << 13)

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

static int acpi_checksum(void *ptr, unsigned long len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (unsigned long i = 0; i < len; ++i) sum += p[i];
    return sum == 0;
}

static rsdp_descriptor_t* find_rsdp(void){
    uint16_t *ebda_ptr = (uint16_t*)0x40E;
    uint32_t ebda_kb = 0;
    if (ebda_ptr) ebda_kb = *ebda_ptr;
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

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static const char scancode_map[128] = {
 0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
 '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
 'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x',
 'c','v','b','n','m',',','.','/',0,'*',0,' '
};

static const char scancode_map_shift[128] = {
 0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
 '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
 'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X',
 'C','V','B','N','M','<','>','?',0,'*',0,' '
};

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

static int shift_pressed = 0;

static char sc_to_ascii(uint8_t sc){
    if(sc == 0x2A || sc == 0x36){ shift_pressed = 1; return 0; }
    if(sc == 0xAA || sc == 0xB6){ shift_pressed = 0; return 0; }
    if(sc > 0 && sc < 128) return shift_pressed ? scancode_map_shift[sc] : scancode_map[sc];
    return 0;
}

void input(char *buffer, size_t size){
    size_t len = 0;
    for(size_t i=0;i<size;i++) buffer[i]=0;
    uint16_t start_x = cursor_x;
    uint16_t start_y = cursor_y;
    size_t cursor_index = 0;
    int e0 = 0;
    enable_cursor(14,15);
    move_cursor(start_y,start_x);
    while(1){
        while((inb(KEYBOARD_STATUS_PORT) & 1) == 0);
        uint8_t sc = inb(KEYBOARD_DATA_PORT);
        if(sc == 0xE0){ e0 = 1; continue; }
        if(sc == 0x2A || sc == 0x36){ shift_pressed = 1; continue; }
        if(sc == 0xAA || sc == 0xB6){ shift_pressed = 0; continue; }
        if(sc & 0x80) { if(e0){ e0 = 0; } continue; }
        if(e0){
            e0 = 0;
            if(sc == 0x4B){
                if(cursor_index > 0) cursor_index--;
                uint16_t cx = start_x + cursor_index;
                uint16_t cy = start_y + cx / SCREEN_WIDTH;
                cx = cx % SCREEN_WIDTH;
                cursor_x = cx; cursor_y = cy;
                update_cursor();
                continue;
            } else if(sc == 0x4D){
                if(cursor_index < len) cursor_index++;
                uint16_t cx = start_x + cursor_index;
                uint16_t cy = start_y + cx / SCREEN_WIDTH;
                cx = cx % SCREEN_WIDTH;
                cursor_x = cx; cursor_y = cy;
                update_cursor();
                continue;
            } else continue;
        }
        char ch = sc_to_ascii(sc);
        if(!ch) continue;
        if(ch == '\n'){
            buffer[len] = 0;
            putchar_term('\n',0x0F);
            return;
        } else if(ch == '\b'){
            if(cursor_index > 0){
                size_t del_at = cursor_index - 1;
                for(size_t i = del_at; i < len; ++i) buffer[i] = buffer[i+1];
                if(len) len--;
                cursor_index--;
                for(size_t i = 0; i < len; ++i){
                    uint16_t px = start_x + i;
                    uint16_t py = start_y + px / SCREEN_WIDTH;
                    px = px % SCREEN_WIDTH;
                    write_char_at(py, px, buffer[i], 0x0F);
                }
                uint16_t cx = start_x + len;
                uint16_t cy = start_y + cx / SCREEN_WIDTH;
                cx = cx % SCREEN_WIDTH;
                write_char_at(cy, cx, ' ', 0x0F);
                uint16_t curx = start_x + cursor_index;
                uint16_t cury = start_y + curx / SCREEN_WIDTH;
                curx = curx % SCREEN_WIDTH;
                cursor_x = curx; cursor_y = cury;
                update_cursor();
            }
            continue;
        } else {
            if(len < size - 1){
                for(size_t i = len; i > cursor_index; --i) buffer[i] = buffer[i-1];
                buffer[cursor_index] = ch;
                len++;
                cursor_index++;
                for(size_t i = 0; i < len; ++i){
                    uint16_t px = start_x + i;
                    uint16_t py = start_y + px / SCREEN_WIDTH;
                    px = px % SCREEN_WIDTH;
                    write_char_at(py, px, buffer[i], 0x0F);
                }
                uint16_t curx = start_x + cursor_index;
                uint16_t cury = start_y + curx / SCREEN_WIDTH;
                curx = curx % SCREEN_WIDTH;
                cursor_x = curx; cursor_y = cury;
                update_cursor();
            }
            continue;
        }
    }
}

static char tolower_char(char c){
    if(c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int strncasecmp_custom(const char *a, const char *b, size_t n){
    size_t i = 0;
    while(i < n && a[i] && b[i]){
        char ca = tolower_char(a[i]);
        char cb = tolower_char(b[i]);
        if(ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        i++;
    }
    if(i == n) return 0;
    return (int)(unsigned char)tolower_char(a[i]) - (int)(unsigned char)tolower_char(b[i]);
}

static void cmd_shutdown(const char *args){ (void)args; kshutdown(); }
static void cmd_minifs(const char *args){ (void)args; print("\nNo drives attached\n",0x0F); }
static void cmd_induce(const char *args){ (void)args; kpanic(); }
static void cmd_clear(const char *args){ (void)args; kclear(); }
static void cmd_echo(const char *args){
    if(!args) return;
    while(*args == ' ') args++;
    print("\n",0x0F);
    print((char*)args,0x0F);
    print("\n",0x0F);
}

typedef void (*cmd_fn)(const char *args);

struct cmd_entry { const char *name; cmd_fn fn; };

static struct cmd_entry commands[] = {
    {"shutdown", cmd_shutdown},
    {"minifs", cmd_minifs},
    {"induce(kernel.panic())", cmd_induce},
    {"clear", cmd_clear},
    {"echo", cmd_echo},
    {"meminfo", cmd_meminfo},
    {"desktop", cmd_desktop},
    {0, 0}
};

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
        print("\nError: Invalid Command\n", 0x4F);
        cont: ;
    }
}

size_t kstrlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
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

    total_memory_mb = (mem_lower + mem_upper) / 1024;

    kclear();
    print("Loading into FrostByte...", 0x0F);

    const char spinner_chars[] = { '|', '/', '-', '\\' };
    int spin_index = 0;

    // Spinner always appears right after the loading text
    int spinner_x = 25; // "Loading into FrostByte..." is 24 chars
    int spinner_y = 0;

    for(int i = 0; i < 10; i++) {
        put_char_at(spinner_chars[spin_index], 0x0F, spinner_x, spinner_y);
        spin_index = (spin_index + 1) % 4;

        for (volatile unsigned long long i = 0; i < 10000000ULL; ++i);
    }

    detect_memory();
    kclear();
    commandLoop();
    kpanic(); //if we return here
}

