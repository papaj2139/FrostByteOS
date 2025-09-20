#include <stdint.h>
#include <string.h>
#include "../drivers/serial.h"

//forward declared from kernel.c
void kpanic_msg(const char* reason);

static const char* exception_names[32] = {
    "Divide-by-zero Error",            //0
    "Debug",                           //1
    "Non-maskable Interrupt",          //2
    "Breakpoint",                      //3
    "Overflow",                        //4
    "Bound Range Exceeded",            //5
    "Invalid Opcode",                  //6
    "Device Not Available",            //7
    "Double Fault",                    //8
    "Coprocessor Segment Overrun",     //9 (reserved)
    "Invalid TSS",                     //10
    "Segment Not Present",             //11
    "Stack-Segment Fault",             //12
    "General Protection Fault",        //13
    "Page Fault",                      //14
    "Reserved",                        //15
    "x87 Floating-Point Exception",    //16
    "Alignment Check",                 //17
    "Machine Check",                   //18
    "SIMD Floating-Point Exception",   //19
    "Virtualization Exception",        //20
    "Control Protection Exception",    //21
    "Reserved",                        //22
    "Reserved",                        //23
    "Reserved",                        //24
    "Reserved",                        //25
    "Reserved",                        //26
    "Reserved",                        //27
    "Hypervisor Injection Exception",  //28
    "VMM Communication Exception",     //29
    "Security Exception",              //30
    "Reserved"                         //31
};

static char g_panic_buf[256];

void isr_exception_dispatch(int vector, unsigned int errcode) {
    const char* name = (vector >= 0 && vector < 32) ? exception_names[vector] : "Unknown Exception";

    if (vector == 14) {
        //page fault read CR2 for faulting linear address
        uint32_t cr2 = 0;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        //decode common bits of the error code
        unsigned p = (errcode & 1);
        unsigned wr = (errcode >> 1) & 1;
        unsigned us = (errcode >> 2) & 1;
        unsigned rsvd = (errcode >> 3) & 1;
        unsigned id = (errcode >> 4) & 1;
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s CR2=0x%x EC=0x%x P=%u W/R=%u U/S=%u RSVD=%u I/D=%u",
                  (unsigned)vector, (char*)name, cr2, errcode, p, wr, us, rsvd, id);
    } else if (errcode) {
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s EC=0x%x", (unsigned)vector, (char*)name, errcode);
    } else {
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s", (unsigned)vector, (char*)name);
    }
    serial_write_string("[EXC] ");
    serial_write_string(g_panic_buf);
    serial_write_string("\n");
    kpanic_msg(g_panic_buf);
}

//extended exception dispatcher that also receives EIP/CS/EFLAGS/USERESP/SS
void isr_exception_dispatch_ext(int vector, unsigned int errcode,
                                uint32_t eip, uint32_t cs,
                                uint32_t eflags, uint32_t useresp, uint32_t ss) {
    const char* name = (vector >= 0 && vector < 32) ? exception_names[vector] : "Unknown Exception";

    if (vector == 14) {
        uint32_t cr2 = 0;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        unsigned p = (errcode & 1);
        unsigned wr = (errcode >> 1) & 1;
        unsigned us = (errcode >> 2) & 1;
        unsigned rsvd = (errcode >> 3) & 1;
        unsigned id = (errcode >> 4) & 1;
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s CR2=0x%x EC=0x%x P=%u W/R=%u U/S=%u RSVD=%u I/D=%u EIP=0x%x CS=0x%x EFLAGS=0x%x ESP=0x%x SS=0x%x",
                  (unsigned)vector, (char*)name, cr2, errcode, p, wr, us, rsvd, id,
                  eip, cs, eflags, useresp, ss);
    } else if (errcode) {
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s EC=0x%x EIP=0x%x CS=0x%x EFLAGS=0x%x ESP=0x%x SS=0x%x",
                  (unsigned)vector, (char*)name, errcode, eip, cs, eflags, useresp, ss);
    } else {
        ksnprintf(g_panic_buf, sizeof(g_panic_buf),
                  "#%u %s EIP=0x%x CS=0x%x EFLAGS=0x%x ESP=0x%x SS=0x%x",
                  (unsigned)vector, (char*)name, eip, cs, eflags, useresp, ss);
    }

    serial_write_string("[EXC] ");
    serial_write_string(g_panic_buf);
    serial_write_string("\n");
    kpanic_msg(g_panic_buf);
}

