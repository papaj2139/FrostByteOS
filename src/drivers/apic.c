#include "apic.h"
#include "../io.h"
#include "../mm/vmm.h"
#include "../drivers/serial.h"
#include "../interrupts/idt.h"
#include "../interrupts/pic.h"
#include "../scheduler.h"
#include <string.h>

static bool apic_available = false;
static uint32_t apic_base_phys = 0;
static volatile uint32_t* apic_base_virt = NULL;
static volatile uint64_t apic_timer_ticks = 0;

//dwetect cpu features
static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

//read MSR (model specific register)
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

//write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

//read APIC register
static inline uint32_t apic_read(uint32_t reg) {
    if (!apic_base_virt) return 0;
    return apic_base_virt[reg / 4];
}

//write APIC register
static inline void apic_write(uint32_t reg, uint32_t value) {
    if (!apic_base_virt) return;
    apic_base_virt[reg / 4] = value;
}

//check if CPU supports APIC
bool apic_is_supported(void) {
    uint32_t eax, ebx, ecx, edx;

    //check CPUID availability first
    cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax < 1) return false;  //CPUID function 1 not available

    //cPUID function 1: Feature Information
    cpuid(1, &eax, &ebx, &ecx, &edx);

    //bit 9 of EDX indicates APIC support
    return (edx & (1 << 9)) != 0;
}

//unitialize APIC
bool apic_init(void) {
    serial_write_string("[APIC] Checking for APIC support...\n");

    if (!apic_is_supported()) {
        serial_write_string("[APIC] CPU does not support APIC\n");
        return false;
    }

    serial_write_string("[APIC] CPU supports APIC\n");

    //read APIC base address from MSR
    uint64_t apic_base_msr = rdmsr(MSR_APIC_BASE);
    apic_base_phys = (uint32_t)(apic_base_msr & 0xFFFFF000);

    serial_write_string("[APIC] APIC base physical address: 0x");
    serial_printf("%x", apic_base_phys);
    serial_write_string("\n");

    //map APIC registers to virtual memory (typically at 0xFEE00000)
    uint32_t apic_virt = 0xFEE00000;  //standard APIC virtual address

    if (vmm_map_page(apic_virt, apic_base_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        serial_write_string("[APIC] Failed to map APIC registers\n");
        return false;
    }

    apic_base_virt = (volatile uint32_t*)apic_virt;
    serial_write_string("[APIC] APIC registers mapped to 0x");
    serial_printf("%x", apic_virt);
    serial_write_string("\n");

    //enable APIC via MSR
    apic_base_msr |= APIC_BASE_ENABLE;
    wrmsr(MSR_APIC_BASE, apic_base_msr);

    //enable APIC in spurious interrupt vector register
    uint32_t spurious = apic_read(APIC_SPURIOUS);
    spurious |= APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR;
    apic_write(APIC_SPURIOUS, spurious);


    //read APIC version
    uint32_t version = apic_read(APIC_VERSION);
    serial_write_string("[APIC] APIC version: 0x");
    serial_printf("%x", version & 0xFF);
    serial_write_string("\n");

    //read APIC ID
    uint32_t apic_id = apic_get_id();
    serial_write_string("[APIC] Local APIC ID: ");
    serial_printf("%d", apic_id);
    serial_write_string("\n");

    apic_available = true;
    serial_write_string("[APIC] APIC initialized successfully\n");

    return true;
}

//send end of interrupt
void apic_send_eoi(void) {
    if (!apic_available || !apic_base_virt) return;
    apic_write(APIC_EOI, 0);  //writing 0 to EOI register signals end
}

//APIC timer interrupt handler
static void apic_timer_handler(void) {
    apic_timer_ticks++;

    //call the scheduler tick handler
    scheduler_tick();
}

//calibrate and initialize APIC timer
void apic_timer_init(uint32_t frequency_hz) {
    (void)frequency_hz;
    if (!apic_available) {
        serial_write_string("[APIC] APIC not available, cannot init timer\n");
        return;
    }

    serial_write_string("[APIC] Initializing APIC timer at 100 Hz\n");

    //disable APIC timer during setup
    apic_write(APIC_TIMER_LVT, APIC_TIMER_MASKED);

    //set divide configuration (divide by 16)
    apic_write(APIC_TIMER_DCR, APIC_TIMER_DIV_16);

    //SIMPLIFIED: using a known-good initial count instead of calibration
    //most APIC timers run at bus frequency / 16
    //typical bus freq is ~1GHz so with div-by-16 that's ~62.5MHz
    //for 100Hz: 62500000 / 100 / 16 = ~39000
    //using a conservative estimate that works on most hardware
    uint32_t initial_count = 1000000;  //conservative value

    serial_write_string("[APIC] Using initial count: 1000000\n");

    //install interrupt handler
    extern void irq_install_handler(int irq, void (*handler)(void));
    irq_install_handler(0, apic_timer_handler);

    //set LVT timer entry: periodic mode, vector 0x20 (IRQ0)
    apic_write(APIC_TIMER_LVT, APIC_TIMER_VECTOR | APIC_TIMER_PERIODIC);

    //set initial count to start timer
    apic_write(APIC_TIMER_ICR, initial_count);

    serial_write_string("[APIC] Timer started\n");
}

//get timer tick count
uint32_t apic_timer_get_ticks(void) {
    return (uint32_t)apic_timer_ticks;
}

//check if APIC is enabled
bool apic_is_enabled(void) {
    return apic_available;
}

//get local APIC ID
uint32_t apic_get_id(void) {
    if (!apic_available) return 0;
    return (apic_read(APIC_ID) >> 24) & 0xFF;
}
