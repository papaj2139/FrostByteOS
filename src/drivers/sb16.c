#include "sb16.h"
#include "../io.h"
#include "../device_manager.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include "serial.h"
#include <string.h>

//default base
#define SB16_BASE_DEFAULT 0x220

//DSP register offsets from base
#define DSP_RESET_OFF   0x6   //base+6
#define DSP_READ_OFF    0xA   //base+0xA
#define DSP_WRITE_OFF   0xC   //base+0xC (also status for write buffer)
#define DSP_RSTAT_OFF   0xE   //base+0xE read buffer status

//mixer ports
#define MIXER_ADDR_OFF  0x4   //base+0x224
#define MIXER_DATA_OFF  0x5   //base+0x225

//ISA DMA (8237) controller 1 (8-bit channels 0 to 3)
#define DMA1_CH1_ADDR   0x02
#define DMA1_CH1_COUNT  0x03
#define DMA1_MASK_REG   0x0A
#define DMA1_MODE_REG   0x0B
#define DMA1_CLEAR_FF   0x0C
#define DMA1_MASTER_CLR 0x0D
#define DMA1_PAGE_CH1   0x83

//device state
static uint16_t g_sb_base = SB16_BASE_DEFAULT;
static uint8_t  g_sb_irq  = 5;   //default common SB16 IRQ
static uint8_t  g_dma8_ch = 1;   //default 8-bit DMA channel
static uint16_t g_rate    = 22050; //default sample rate
static volatile int g_irq_block_done = 0;
static int g_speaker_enabled = 0;

//software mixer controls
static int g_volume = 100; //0..100
static int g_muted = 0;

//streaming ring buffer
#define SB16_RING_CAP (64*1024)
static uint8_t*  g_ring = NULL;
static uint32_t  g_ring_cap = 0;
static volatile uint32_t g_ring_head = 0; //write pos
static volatile uint32_t g_ring_tail = 0; //read pos
static volatile uint32_t g_ring_fill = 0;
static volatile int g_playing = 0;
static volatile int g_paused = 0;
static volatile uint32_t g_underruns = 0;

static device_t g_sb_dev;

//delay for restt timing
static inline void io_delay(void) {
    //port I/O delay: read from an unused port
    (void)inb(0x80);
}

static inline int dsp_write_wait(uint8_t v) {
    //wait for write buffer to be ready (bit7 of base+0xC == 0)
    for (int i = 0; i < 65536; ++i) {
        if ((inb(g_sb_base + DSP_WRITE_OFF) & 0x80) == 0) {
            outb(g_sb_base + DSP_WRITE_OFF, v);
            return 0;
        }
    }
    return -1;
}

static inline int dsp_read_wait(uint8_t* out) {
    if (!out) return -1;
    for (int i = 0; i < 65536; ++i) {
        if (inb(g_sb_base + DSP_RSTAT_OFF) & 0x80) {
            *out = inb(g_sb_base + DSP_READ_OFF);
            return 0;
        }
    }
    return -1;
}

static int dsp_reset(void) {
    outb(g_sb_base + DSP_RESET_OFF, 1);
    io_delay(); io_delay(); io_delay();
    outb(g_sb_base + DSP_RESET_OFF, 0);
    //expect 0xAA from DSP
    uint8_t v = 0;
    if (dsp_read_wait(&v) != 0) return -1;
    return (v == 0xAA) ? 0 : -1;
}

static uint8_t mixer_read(uint8_t reg) {
    outb(g_sb_base + MIXER_ADDR_OFF, reg);
    return inb(g_sb_base + MIXER_DATA_OFF);
}

static void dsp_set_rate(uint16_t rate) {
    //SB16: 0x41 Set output sample rate, high then low
    (void)dsp_write_wait(0x41);
    (void)dsp_write_wait((uint8_t)(rate >> 8));
    (void)dsp_write_wait((uint8_t)(rate & 0xFF));
}

static void dsp_speaker_on(void) {
    (void)dsp_write_wait(0xD1);
}

static void dsp_speaker_off(void) {
    (void)dsp_write_wait(0xD3);
}

//detect configured IRQ and DMA from mixer
static void detect_irq_dma(void) {
    //IRQ select at mixer 0x80: typical bits (1<<1)=IRQ5, (1<<2)=IRQ7, (1<<3)=IRQ10, (1<<0)=IRQ2
    uint8_t irq_sel = mixer_read(0x80);
    if      (irq_sel & 0x02) g_sb_irq = 5;
    else if (irq_sel & 0x04) g_sb_irq = 7;
    else if (irq_sel & 0x08) g_sb_irq = 10;
    else if (irq_sel & 0x01) g_sb_irq = 2; //unlikely as shit
    else g_sb_irq = 5; //fallback

    //DMA select at mixer 0x81: bits for 8-bit ch {0,1,3}
    uint8_t dma_sel = mixer_read(0x81);
    if      (dma_sel & 0x02) g_dma8_ch = 1;
    else if (dma_sel & 0x08) g_dma8_ch = 3;
    else if (dma_sel & 0x01) g_dma8_ch = 0;
    else g_dma8_ch = 1; //fallback
}

//forward
static void sb16_kick_locked(void);

//IRQ handler acknowledge SB16 8-bit DMA interrupt and signal completion
static void sb16_irq_handler(void) {
    //acknowledge read status then data to clear 8-bit IRQ
    (void)inb(g_sb_base + DSP_RSTAT_OFF);
    (void)inb(g_sb_base + DSP_READ_OFF);
    g_irq_block_done = 1;
    //continue streaming
    g_playing = 0;
    if (!g_paused && g_ring_fill > 0) {
        sb16_kick_locked();
    }
}

//program DMA channel 1 for 8-bit playback from physical address
static void dma8_program_ch1(uint32_t phys, uint16_t len) {
    //mask channel 1
    outb(DMA1_MASK_REG, (uint8_t)(0x04 | 1));

    //reset flip-flop
    outb(DMA1_CLEAR_FF, 0x00);

    //address (low, high) for ch1
    outb(DMA1_CH1_ADDR, (uint8_t)(phys & 0xFF));
    outb(DMA1_CH1_ADDR, (uint8_t)((phys >> 8) & 0xFF));
    //page
    outb(DMA1_PAGE_CH1, (uint8_t)((phys >> 16) & 0xFF));

    //count = len-1 (low, high)
    uint16_t cnt = (uint16_t)(len - 1);
    outb(DMA1_CLEAR_FF, 0x00);
    outb(DMA1_CH1_COUNT, (uint8_t)(cnt & 0xFF));
    outb(DMA1_CH1_COUNT, (uint8_t)((cnt >> 8) & 0xFF));

    //mode: channel 1 | single-cycle | increment | read (mem->device)
    //transfer type read=10b -> 0x80, single=01b -> 0x04
    outb(DMA1_MODE_REG, (uint8_t)(0x80 | 0x04 | 1)); //0x85

    //unmask channel 1
    outb(DMA1_MASK_REG, 0x01);
}

//start one 8-bit block via single-cycle DMA (non-blocking)
static int sb16_start_block(uint32_t phys, uint16_t len) {
    if (len == 0) return 0;
    if (g_dma8_ch != 1) {
        g_dma8_ch = 1;
    }
    dma8_program_ch1(phys, len);
    dsp_set_rate(g_rate);
    (void)dsp_write_wait(0x14);
    uint16_t cnt = (uint16_t)(len - 1);
    (void)dsp_write_wait((uint8_t)(cnt & 0xFF));
    (void)dsp_write_wait((uint8_t)((cnt >> 8) & 0xFF));
    g_playing = 1;
    return 0;
}

uint16_t sb16_get_rate(void) {
    return g_rate;
}

int sb16_set_rate(uint16_t rate) {
    if (rate < 4000) rate = 4000;
    if (rate > 48000) rate = 48000;
    g_rate = rate;
    dsp_set_rate(g_rate);
    return 0;
}

void sb16_speaker_on(void) {
    dsp_speaker_on();
    g_speaker_enabled = 1;
}

void sb16_speaker_off(void) {
    dsp_speaker_off();
    g_speaker_enabled = 0;
}

int sb16_is_speaker_on(void) {
    return g_speaker_enabled;
}

uint8_t sb16_get_irq(void) {
    return g_sb_irq;
}

uint8_t sb16_get_dma8(void) {
    return g_dma8_ch;
}

int sb16_set_volume(int vol01_100) {
    if (vol01_100 < 0) vol01_100 = 0;
    if (vol01_100 > 100) vol01_100 = 100;
    g_volume = vol01_100;
    return 0;
}

int sb16_get_volume(void) {
    return g_volume;
}

void sb16_set_mute(int on) {
    g_muted = on ? 1 : 0;
}

int sb16_get_mute(void) {
    return g_muted;
}

void sb16_pause(void) {
    g_paused = 1;
}

void sb16_resume(void) {
    g_paused = 0;
    if (!g_playing && g_ring_fill > 0) sb16_kick_locked();
}

void sb16_stop(void) {
    g_paused = 1;
    g_playing = 0;
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags)); g_ring_head = g_ring_tail = g_ring_fill = 0;
    if (flags & 0x200) __asm__ volatile ("sti");
    sb16_speaker_off();
}

int sb16_is_playing(void) {
    return g_playing;
}

int sb16_is_paused(void) {
    return g_paused;
}

uint32_t sb16_get_queued(void) {
    return g_ring_fill;
}

uint32_t sb16_get_underruns(void) {
    return g_underruns;
}

//device ops
static int sb16_dev_init(struct device* d) {
    (void)d;
    //allocate ring buffer once
    if (!g_ring) {
        g_ring = (uint8_t*)kmalloc(SB16_RING_CAP);
        if (!g_ring) return -1;
        g_ring_cap = SB16_RING_CAP;
        g_ring_head = g_ring_tail = g_ring_fill = 0;
        g_playing = 0; g_paused = 0; g_underruns = 0;
    }
    //reset DSP and verify
    if (dsp_reset() != 0) {
        serial_write_string("[SB16] DSP reset failed\n");
        return -1;
    }

    //detect IRQ and DMA settings
    detect_irq_dma();

    //install IRQ handler and unmask
    irq_install_handler(g_sb_irq, sb16_irq_handler);
    pic_clear_mask(g_sb_irq);

    //default: speaker off set default rate
    sb16_speaker_off();
    dsp_set_rate(g_rate);
    return 0;
}

static int sb16_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz) { (void)d; (void)off; (void)buf; (void)sz; return -1; }

//start next DMA block from ring (assumes interrupts disabled or IRQ context)
static void sb16_kick_locked(void) {
    if (g_paused || g_playing) return;
    if (g_ring_fill == 0) {
        g_underruns++;
        return;
    }
    uint32_t tail = g_ring_tail;
    uint32_t to_play = g_ring_fill;
    if (to_play > 4096u) to_play = 4096u;
    uint32_t vaddr = (uint32_t)(g_ring + tail);
    uint32_t phys = vmm_get_physical_addr(vaddr);
    if (!phys) {
        g_underruns++;
        return;
    }
    uint32_t next64k = (phys & 0xFFFF0000u) + 0x10000u;
    uint32_t remain64k = (next64k > phys) ? (next64k - phys) : 0x10000u;
    if (to_play > remain64k) to_play = remain64k;
    uint32_t ring_tail_to_end = g_ring_cap - tail;
    if (to_play > ring_tail_to_end) to_play = ring_tail_to_end;
    if (sb16_start_block(phys, (uint16_t)to_play) == 0) {
        g_ring_tail = (g_ring_tail + to_play) % g_ring_cap;
        g_ring_fill -= to_play;
    }
}

static int sb16_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz) {
    (void)d;
    (void)off;
    if (!buf || sz == 0) return 0;

    const uint8_t* p = (const uint8_t*)buf;
    uint32_t written = 0;
    while (written < sz) {
        while (g_ring_fill == g_ring_cap) {
            __asm__ volatile ("hlt");
        }
        uint32_t space = g_ring_cap - g_ring_fill;
        uint32_t head = g_ring_head;
        uint32_t to_end = g_ring_cap - head;
        uint32_t chunk = sz - written;
        if (chunk > space) chunk = space;
        if (chunk > to_end) chunk = to_end;
        for (uint32_t i = 0; i < chunk; i++) {
            uint8_t s = p[written + i];
            if (g_muted || g_volume == 0) {
                g_ring[head + i] = 128;
            } else if (g_volume >= 100) {
                g_ring[head + i] = s;
            } else {
                int centered = (int)s - 128;
                int scaled = (centered * g_volume) / 100;
                int out = 128 + scaled;
                if (out < 0) out = 0;
                if (out > 255) out = 255;
                g_ring[head + i] = (uint8_t)out;
            }
        }
        uint32_t flags;
        __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
        g_ring_head = (g_ring_head + chunk) % g_ring_cap;
        g_ring_fill += chunk;
        if (!g_paused && !g_playing) {
            sb16_kick_locked();
        }
        if (flags & 0x200) __asm__ volatile ("sti");
        written += chunk;
    }
    return (int)written;
}

static int sb16_dev_ioctl(struct device* d, uint32_t cmd, void* arg) {
    (void)d;
    (void)cmd;
    (void)arg;
    return -1;
}

static void sb16_dev_cleanup(struct device* d) {
    (void)d;
    sb16_speaker_off();
    irq_uninstall_handler(g_sb_irq);
}

static const device_ops_t sb16_ops = {
    .init = sb16_dev_init,
    .read = sb16_dev_read,
    .write = sb16_dev_write,
    .ioctl = sb16_dev_ioctl,
    .cleanup = sb16_dev_cleanup,
};

int sb16_register_device(void) {
    memset(&g_sb_dev, 0, sizeof(g_sb_dev));
    strcpy(g_sb_dev.name, "sb16");
    g_sb_dev.type = DEVICE_TYPE_OUTPUT;
    g_sb_dev.subtype = DEVICE_SUBTYPE_AUDIO;
    g_sb_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_sb_dev.ops = &sb16_ops;
    if (device_register(&g_sb_dev) != 0) return -1;
    if (device_init(&g_sb_dev) != 0) {
        device_unregister(g_sb_dev.device_id);
        return -1;
    }
    g_sb_dev.status = DEVICE_STATUS_READY;
    return 0;
}
