#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

//local APIC register offsets (from APIC base address)
#define APIC_ID                 0x0020  //local APIC ID
#define APIC_VERSION            0x0030  //local APIC version
#define APIC_TPR                0x0080  //task priority register
#define APIC_APR                0x0090  //arbitration priority register
#define APIC_PPR                0x00A0  //processor priority register
#define APIC_EOI                0x00B0  //end of interrupt
#define APIC_RRD                0x00C0  //remote read register
#define APIC_LDR                0x00D0  //logical destination register
#define APIC_DFR                0x00E0  //destination format register
#define APIC_SPURIOUS           0x00F0  //spurious interrupt vector register
#define APIC_ISR                0x0100  //in-service register (0x100-0x170)
#define APIC_TMR                0x0180  //trigger mode register (0x180-0x1F0)
#define APIC_IRR                0x0200  //interrupt request register (0x200-0x270)
#define APIC_ESR                0x0280  //error status register
#define APIC_ICR_LOW            0x0300  //interrupt command register (bits 0-31)
#define APIC_ICR_HIGH           0x0310  //interrupt command register (bits 32-63)
#define APIC_TIMER_LVT          0x0320  //LVT timer register
#define APIC_THERMAL_LVT        0x0330  //LVT thermal sensor register
#define APIC_PERF_LVT           0x0340  //LVT performance counter register
#define APIC_LINT0_LVT          0x0350  //LVT LINT0 register
#define APIC_LINT1_LVT          0x0360  //LVT LINT1 register
#define APIC_ERROR_LVT          0x0370  //LVT error register
#define APIC_TIMER_ICR          0x0380  //timer initial count register
#define APIC_TIMER_CCR          0x0390  //timer current count register
#define APIC_TIMER_DCR          0x03E0  //timer divide configuration register

//spurious interrupt vector register bits
#define APIC_SPURIOUS_ENABLE    (1 << 8)  //apic software enable/disable

//timer LVT bits
#define APIC_TIMER_PERIODIC     0x20000   //periodic mode
#define APIC_TIMER_MASKED       0x10000   //interrupt masked

//timer divide configuration values
#define APIC_TIMER_DIV_1        0xB
#define APIC_TIMER_DIV_2        0x0
#define APIC_TIMER_DIV_4        0x1
#define APIC_TIMER_DIV_8        0x2
#define APIC_TIMER_DIV_16       0x3
#define APIC_TIMER_DIV_32       0x8
#define APIC_TIMER_DIV_64       0x9
#define APIC_TIMER_DIV_128      0xA

//MSR addresses
#define MSR_APIC_BASE           0x1B

//APIC base MSR bits
#define APIC_BASE_ENABLE        (1 << 11)  //enable APIC globally
#define APIC_BASE_BSP           (1 << 8)   //bootstrap processor

//default spurious interrupt vector
#define APIC_SPURIOUS_VECTOR    0xFF

//default timer interrupt vector
#define APIC_TIMER_VECTOR       0x20

//function declarations
bool apic_is_supported(void);
bool apic_init(void);
void apic_send_eoi(void);
void apic_timer_init(uint32_t frequency_hz);
uint32_t apic_timer_get_ticks(void);
bool apic_is_enabled(void);
uint32_t apic_get_id(void);

#endif
