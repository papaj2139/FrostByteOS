#include "ahci.h"
#include "pci.h"
#include "serial.h"
#include "../debug.h"
#include "../mm/heap.h"
#include "../mm/vmm.h"
#include <string.h>

//AHCI controller state
static ahci_hba_mem_t* abar = NULL;
static pci_device_t ahci_pci_dev;
static int ahci_initialized = 0;

//per-port data structures
typedef struct {
    ahci_hba_port_t* port;
    ahci_cmd_header_t* cmd_list;
    ahci_received_fis_t* fis;
    ahci_cmd_table_t* cmd_tables[32];
    uint32_t cmd_list_phys;
    uint32_t fis_phys;
    uint8_t port_num;
    uint8_t device_type;
    uint64_t total_sectors; //capacity in 512-byte sectors (from IDENTIFY)
    //DMA bounce buffer for non-physical buffers
    void* dma_buffer;
    uint32_t dma_buffer_phys;
} ahci_port_data_t;

static ahci_port_data_t port_data[32];

//partition support
#define MAX_AHCI_PARTITIONS 16
typedef struct {
    device_t* base;
    uint32_t start_lba;
    uint32_t sectors;
} ahci_part_priv_t;

static device_t ahci_part_devices[MAX_AHCI_PARTITIONS];
static ahci_part_priv_t ahci_part_privs[MAX_AHCI_PARTITIONS];
static int ahci_part_count = 0;
static int ahci_drive_count = 0;

//check device type from signature
static int ahci_check_type(ahci_hba_port_t* port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0xF;

    if (det != HBA_PxSSTS_DET_PRESENT) {
        return AHCI_DEV_NULL;
    }

    uint32_t sig = port->sig;
    switch (sig) {
        case SATA_SIG_ATA:
            return AHCI_DEV_SATA;
        case SATA_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case SATA_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case SATA_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_NULL;
    }
}

//stop command engine
static void ahci_stop_cmd(ahci_hba_port_t* port) {
    //clear ST (bit 0)
    port->cmd &= ~HBA_PxCMD_ST;

    //wait until CR (bit 15) is cleared
    while (port->cmd & HBA_PxCMD_CR) {
        //spin
    }

    //clear FRE (bit 4)
    port->cmd &= ~HBA_PxCMD_FRE;

    //wait until FR (bit 14) is cleared
    while (port->cmd & HBA_PxCMD_FR) {
        //spin
    }
}

//start command engine
static void ahci_start_cmd(ahci_hba_port_t* port) {
    //wait until CR (bit 15) is cleared
    while (port->cmd & HBA_PxCMD_CR) {
        //spin
    }

    //set FRE (bit 4) and ST (bit 0)
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

//allocate command list and FIS for a port
static int ahci_port_alloc(ahci_port_data_t* pd) {
    uint32_t cmd_list_phys, fis_phys;

    //allocate command list (1KB aligned) we need physical address for DMA
    pd->cmd_list = (ahci_cmd_header_t*)kmalloc_physical(sizeof(ahci_cmd_header_t) * 32, &cmd_list_phys);
    if (!pd->cmd_list) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] Failed to allocate command list\n");
        #endif
        return -1;
    }
    memset(pd->cmd_list, 0, sizeof(ahci_cmd_header_t) * 32);

    //allocate FIS (256 byte aligned) we need physical address for DMA
    pd->fis = (ahci_received_fis_t*)kmalloc_physical(sizeof(ahci_received_fis_t), &fis_phys);
    if (!pd->fis) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] Failed to allocate FIS\n");
        #endif
        kfree(pd->cmd_list);
        return -1;
    }
    memset(pd->fis, 0, sizeof(ahci_received_fis_t));

    //store physical addresses for setting up port registers
    pd->cmd_list_phys = cmd_list_phys;
    pd->fis_phys = fis_phys;

    //allocate command tables for each slot
    for (int i = 0; i < 32; i++) {
        uint32_t ctbl_phys;
        pd->cmd_tables[i] = (ahci_cmd_table_t*)kmalloc_physical(sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * 8, &ctbl_phys);
        if (!pd->cmd_tables[i]) {
            #if DEBUG_AHCI
            serial_write_string("[AHCI] Failed to allocate command table\n");
            #endif
            //cleanup
            for (int j = 0; j < i; j++) {
                kfree(pd->cmd_tables[j]);
            }
            kfree(pd->fis);
            kfree(pd->cmd_list);
            return -1;
        }
        memset(pd->cmd_tables[i], 0, sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * 8);

        //set command table address in command header (use physical address)
        pd->cmd_list[i].ctba = ctbl_phys;
        pd->cmd_list[i].ctbau = 0; //32-bit system
    }

    //allocate DMA bounce buffer (128KB should be enough for most operations)
    pd->dma_buffer = kmalloc_physical(128 * 1024, &pd->dma_buffer_phys);
    if (!pd->dma_buffer) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] Failed to allocate DMA bounce buffer\n");
        #endif
        //cleanup
        for (int i = 0; i < 32; i++) {
            kfree(pd->cmd_tables[i]);
        }
        kfree(pd->fis);
        kfree(pd->cmd_list);
        return -1;
    }

    return 0;
}

//initialize a port
static int ahci_port_rebase(ahci_hba_port_t* port, int portno) {
    ahci_stop_cmd(port);

    ahci_port_data_t* pd = &port_data[portno];
    pd->port = port;
    pd->port_num = portno;

    if (ahci_port_alloc(pd) < 0) {
        return -1;
    }

    //set command list base address (use physical address for DMA)
    port->clb = pd->cmd_list_phys;
    port->clbu = 0; //32-bit system

    //set FIS base address (use physical address for DMA)
    port->fb = pd->fis_phys;
    port->fbu = 0; //32-bit system

    //clear interrupt status
    port->is = 0xFFFFFFFF;

    //enable FIS receive
    port->cmd |= HBA_PxCMD_FRE;

    //start command processing
    ahci_start_cmd(port);

    return 0;
}

//find a free command slot
static int ahci_find_cmdslot(ahci_hba_port_t* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }
    return -1;
}

//read sectors from SATA drive
int ahci_read_sectors(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    if (!device || !device->private_data || !buffer) return -1;

    ahci_port_data_t* pd = (ahci_port_data_t*)device->private_data;
    ahci_hba_port_t* port = pd->port;

    //convert byte offset to sector (assuming 512 byte sectors)
    uint64_t lba = offset / 512;
    uint32_t count = (size + 511) / 512;

    #if DEBUG_AHCI
    serial_printf("[AHCI] Read: LBA=%d count=%d size=%d\n", (uint32_t)lba, count, size);
    #endif

    //find free command slot
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] No free command slots\n");
        #endif
        return -1;
    }

    ahci_cmd_header_t* cmdheader = &pd->cmd_list[slot];
    //reserve ctba! and clear the fields we need
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);  //should be 5
    cmdheader->w = 0; //read (device to host)
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    cmdheader->p = 0;
    cmdheader->c = 0;
    cmdheader->b = 0;
    cmdheader->r = 0;
    cmdheader->pmp = 0;
    //ctba and ctbau are already set during port allocation

    #if DEBUG_AHCI
    serial_printf("[AHCI] Read cmd header: cfl=%d w=%d prdtl=%d ctba=0x%x\n",
                  cmdheader->cfl, cmdheader->w, cmdheader->prdtl, cmdheader->ctba);
    #endif

    ahci_cmd_table_t* cmdtbl = pd->cmd_tables[slot];
    memset(cmdtbl, 0, sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * 8);

    //Use DMA bounce buffer
    void* dma_buf = pd->dma_buffer;
    uint32_t buffer_phys = pd->dma_buffer_phys;

    #if DEBUG_AHCI
    serial_printf("[AHCI] Buffer virt=0x%x dma_phys=0x%x\n", (uint32_t)buffer, buffer_phys);
    #endif

    cmdtbl->prdt_entry[0].dba = buffer_phys;
    cmdtbl->prdt_entry[0].dbau = 0; //32-bit system
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1; //0-based (byte count - 1)
    cmdtbl->prdt_entry[0].i = 1; //interrupt on completion

    #if DEBUG_AHCI
    uint32_t dbc_val = cmdtbl->prdt_entry[0].dbc;
    serial_printf("[AHCI] Read PRDT: dba=0x%x dbau=0x%x dbc=%d (size=%d bytes)\n",
                  cmdtbl->prdt_entry[0].dba,
                  cmdtbl->prdt_entry[0].dbau,
                  dbc_val,
                  count * 512);
    #endif

    //setup command FIS
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmdtbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));

    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; //command
    cmdfis->command = ATA_CMD_READ_DMA_EX;

    cmdfis->lba0 = (uint8_t)(lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    cmdfis->device = 1 << 6; //LBA mode

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFF);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Read FIS: cmd=0x%x type=%d c=%d lba=%d count=%d device=0x%x\n",
                  cmdfis->command, cmdfis->fis_type, cmdfis->c,
                  (uint32_t)lba, count, cmdfis->device);
    //dump raw FIS bytes
    serial_write_string("[AHCI] Raw FIS: ");
    uint8_t* fis_bytes = (uint8_t*)cmdfis;
    for (int i = 0; i < 20; i++) {
        serial_printf("%x ", fis_bytes[i]);
    }
    serial_write_string("\n");
    #endif

    //issue command
    port->ci = (1 << slot);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Read command issued on slot %d\n", slot);
    #endif

    //wait for completion
    int timeout = 1000000;
    while (timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { //task file error
            #if DEBUG_AHCI
            serial_printf("[AHCI] Read error: TFD=0x%x IS=0x%x SERR=0x%x\n",
                          port->tfd, port->is, port->serr);
            #endif
            return -1;
        }
    }

    #if DEBUG_AHCI
    //check for any SATA errors even if command completed
    if (port->serr) {
        serial_printf("[AHCI] SATA error after read: SERR=0x%x\n", port->serr);
        port->serr = port->serr; //clear errors
    }
    serial_printf("[AHCI] Read completion: TFD=0x%x IS=0x%x PRDBC=%d\n",
                  port->tfd, port->is, cmdheader->prdbc);
    #endif

    if (timeout <= 0) {
        #if DEBUG_AHCI
        serial_printf("[AHCI] Read timeout: CI=0x%x IS=0x%x\n", port->ci, port->is);
        #endif
        return -1;
    }

    //clear interrupt status
    port->is = port->is;

    //copy from DMA bounce buffer to user buffer
    memcpy(buffer, dma_buf, size);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Read completed, first 4 bytes: %x %x %x %x\n",
                  ((uint8_t*)buffer)[0], ((uint8_t*)buffer)[1],
                  ((uint8_t*)buffer)[2], ((uint8_t*)buffer)[3]);
    #endif

    return size;
}

//write sectors to SATA drive
int ahci_write_sectors(device_t* device, uint32_t offset, const void* buffer, uint32_t size) {
    if (!device || !device->private_data || !buffer) return -1;

    ahci_port_data_t* pd = (ahci_port_data_t*)device->private_data;
    ahci_hba_port_t* port = pd->port;

    //convert byte offset to sector (assuming 512 byte sectors)
    uint64_t lba = offset / 512;
    uint32_t count = (size + 511) / 512;

    #if DEBUG_AHCI
    serial_printf("[AHCI] Write: LBA=%d count=%d size=%d\n", (uint32_t)lba, count, size);
    #endif

    //find free command slot
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] No free command slots\n");
        #endif
        return -1;
    }

    ahci_cmd_header_t* cmdheader = &pd->cmd_list[slot];
    //preserve ctba! clear the fields we need
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);  //should be 5
    cmdheader->w = 1; //write (host to device)
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    cmdheader->p = 0;
    cmdheader->c = 0;
    cmdheader->b = 0;
    cmdheader->r = 0;
    cmdheader->pmp = 0;
    //ctba and ctbau are already set during port allocation

    #if DEBUG_AHCI
    serial_printf("[AHCI] Write cmd header: cfl=%d w=%d prdtl=%d ctba=0x%x\n",
                  cmdheader->cfl, cmdheader->w, cmdheader->prdtl, cmdheader->ctba);
    serial_printf("[AHCI] Write first 4 data bytes: %x %x %x %x\n",
                  ((uint8_t*)buffer)[0], ((uint8_t*)buffer)[1],
                  ((uint8_t*)buffer)[2], ((uint8_t*)buffer)[3]);
    #endif

    ahci_cmd_table_t* cmdtbl = pd->cmd_tables[slot];
    memset(cmdtbl, 0, sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * 8);

    //copy to DMA bounce buffer first
    void* dma_buf = pd->dma_buffer;
    uint32_t buffer_phys = pd->dma_buffer_phys;
    memcpy(dma_buf, buffer, size);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Write buffer virt=0x%x dma_phys=0x%x\n", (uint32_t)buffer, buffer_phys);
    #endif

    cmdtbl->prdt_entry[0].dba = buffer_phys;
    cmdtbl->prdt_entry[0].dbau = 0; //32-bit system
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1; //0-based (byte count - 1)
    cmdtbl->prdt_entry[0].i = 1; //interrupt on completion

    #if DEBUG_AHCI
    uint32_t dbc_val_w = cmdtbl->prdt_entry[0].dbc;
    serial_printf("[AHCI] Write PRDT: dba=0x%x dbau=0x%x dbc=%d (size=%d bytes)\n",
                  cmdtbl->prdt_entry[0].dba,
                  cmdtbl->prdt_entry[0].dbau,
                  dbc_val_w,
                  count * 512);
    #endif

    //setup command FIS
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmdtbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));

    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; //command
    cmdfis->command = ATA_CMD_WRITE_DMA_EX;

    cmdfis->lba0 = (uint8_t)(lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    cmdfis->device = 1 << 6; //LBA mode

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFF);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Write FIS: cmd=0x%x type=%d c=%d lba=%d count=%d device=0x%x\n",
                  cmdfis->command, cmdfis->fis_type, cmdfis->c,
                  (uint32_t)lba, count, cmdfis->device);
    //dump raw FIS bytes
    serial_write_string("[AHCI] Raw FIS: ");
    uint8_t* fis_bytes_w = (uint8_t*)cmdfis;
    for (int i = 0; i < 20; i++) {
        serial_printf("%x ", fis_bytes_w[i]);
    }
    serial_write_string("\n");
    #endif

    //issue command
    port->ci = (1 << slot);

    #if DEBUG_AHCI
    serial_printf("[AHCI] Command issued on slot %d\n", slot);
    #endif

    //wait for completion
    int timeout = 1000000;
    while (timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { //task file error
            #if DEBUG_AHCI
            serial_printf("[AHCI] Write error: TFD=0x%x IS=0x%x SERR=0x%x\n",
                          port->tfd, port->is, port->serr);
            #endif
            return -1;
        }
    }

    #if DEBUG_AHCI
    //check for any SATA errors even if command completed
    if (port->serr) {
        serial_printf("[AHCI] SATA error after write: SERR=0x%x\n", port->serr);
        port->serr = port->serr; //clear errors
    }
    serial_printf("[AHCI] Write completion: TFD=0x%x IS=0x%x PRDBC=%d\n",
                  port->tfd, port->is, cmdheader->prdbc);
    #endif

    if (timeout <= 0) {
        #if DEBUG_AHCI
        serial_printf("[AHCI] Write timeout: CI=0x%x IS=0x%x\n", port->ci, port->is);
        #endif
        return -1;
    }

    //clear interrupt status
    port->is = port->is;

    #if DEBUG_AHCI
    serial_printf("[AHCI] Write completed successfully\n");
    #endif

    //issue FLUSH CACHE command to ensure data is written to disk
    //this is needed for QEMU AHCI emulation which uses write caching
    slot = ahci_find_cmdslot(port);
    if (slot != -1) {
        cmdheader = &pd->cmd_list[slot];
        cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
        cmdheader->w = 0;
        cmdheader->prdtl = 0;  //no data transfer for flush
        cmdheader->prdbc = 0;
        cmdheader->p = 0;
        cmdheader->c = 0;
        cmdheader->b = 0;
        cmdheader->r = 0;
        cmdheader->pmp = 0;

        cmdtbl = pd->cmd_tables[slot];
        memset(cmdtbl, 0, sizeof(ahci_cmd_table_t));

        fis_reg_h2d_t* flush_fis = (fis_reg_h2d_t*)cmdtbl->cfis;
        memset(flush_fis, 0, sizeof(fis_reg_h2d_t));
        flush_fis->fis_type = FIS_TYPE_REG_H2D;
        flush_fis->c = 1;
        flush_fis->command = ATA_CMD_FLUSH_CACHE_EXT;
        flush_fis->device = 0;

        port->ci = (1 << slot);

        #if DEBUG_AHCI
        serial_printf("[AHCI] Issuing FLUSH CACHE command\n");
        #endif

        //wait for flush to complete
        timeout = 1000000;
        while (timeout-- > 0) {
            if ((port->ci & (1 << slot)) == 0) break;
        }

        port->is = port->is;  //clear interrupt status

        #if DEBUG_AHCI
        serial_printf("[AHCI] FLUSH CACHE completed\n");
        #endif
    }

    return size;
}

//send IDENTIFY DEVICE command to get disk info
static int ahci_identify(ahci_port_data_t* pd, uint16_t* id_buffer) {
    ahci_hba_port_t* port = pd->port;
    if (!port) return -1;

    //find free command slot
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return -1;

    ahci_cmd_header_t* cmdheader = &pd->cmd_list[slot];
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0; //device to host
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    cmdheader->p = 0;
    cmdheader->c = 0;
    cmdheader->b = 0;
    cmdheader->r = 0;
    cmdheader->pmp = 0;

    ahci_cmd_table_t* cmdtbl = pd->cmd_tables[slot];
    memset(cmdtbl, 0, sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * 8);

    //use DMA bounce buffer
    uint32_t buffer_phys = pd->dma_buffer_phys;

    cmdtbl->prdt_entry[0].dba = buffer_phys;
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = 512 - 1; //0-based
    cmdtbl->prdt_entry[0].i = 1;

    //setup command FIS
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmdtbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; //command
    cmdfis->command = ATA_CMD_IDENTIFY;
    cmdfis->device = 0;

    //issue command
    port->ci = (1 << slot);

    //wait for completion
    int timeout = 1000000;
    while (timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { //task file error
            return -1;
        }
    }

    if (timeout <= 0) return -1;

    //clear interrupt status
    port->is = port->is;

    //copy from DMA buffer to output
    memcpy(id_buffer, pd->dma_buffer, 512);

    return 0;
}

//partition device operations
static int ahci_part_read(device_t* d, uint32_t offset, void* buffer, uint32_t size) {
    ahci_part_priv_t* pp = (ahci_part_priv_t*)d->private_data;
    if (!pp || !pp->base) return -1;
    uint64_t part_bytes = (uint64_t)pp->sectors * 512ull;
    if ((uint64_t)offset + (uint64_t)size > part_bytes) return -1;
    uint32_t abs_off = (uint32_t)(pp->start_lba * 512ull + (uint64_t)offset);
    return device_read(pp->base, abs_off, buffer, size);
}

static int ahci_part_write(device_t* d, uint32_t offset, const void* buffer, uint32_t size) {
    ahci_part_priv_t* pp = (ahci_part_priv_t*)d->private_data;
    if (!pp || !pp->base) return -1;
    uint64_t part_bytes = (uint64_t)pp->sectors * 512ull;
    if ((uint64_t)offset + (uint64_t)size > part_bytes) return -1;
    uint32_t abs_off = (uint32_t)(pp->start_lba * 512ull + (uint64_t)offset);
    return device_write(pp->base, abs_off, buffer, size);
}

static int ahci_part_ioctl(device_t* d, uint32_t cmd, void* arg) {
    if (!d) return -1;
    if (cmd == IOCTL_BLK_GET_INFO) {
        typedef struct {
            uint32_t sector_size;
            uint32_t sector_count;
        } blkdev_info_t;
        if (!arg) return -1;
        ahci_part_priv_t* pp = (ahci_part_priv_t*)d->private_data;
        if (!pp) return -1;
        blkdev_info_t info;
        info.sector_size = 512;
        info.sector_count = pp->sectors;
        //copy to user
        extern int copy_to_user(void* dst, const void* src, uint32_t size);
        if (copy_to_user(arg, &info, sizeof(info)) != 0) return -1;
        return 0;
    }
    return -1;
}

static const device_ops_t ahci_part_ops = {
    .init = NULL,
    .read = ahci_part_read,
    .write = ahci_part_write,
    .ioctl = ahci_part_ioctl,
    .cleanup = NULL
};

//ioctl handler for AHCI drives
static int ahci_device_ioctl(device_t* device, uint32_t cmd, void* arg) {
    if (!device) return -1;
    if (cmd == IOCTL_BLK_GET_INFO) {
        typedef struct {
            uint32_t sector_size;
            uint32_t sector_count;
        } blkdev_info_t;
        if (!arg) return -1;
        ahci_port_data_t* pd = (ahci_port_data_t*)device->private_data;
        if (!pd) return -1;
        blkdev_info_t info;
        info.sector_size = 512;
        info.sector_count = (uint32_t)pd->total_sectors;
        //copy to user
        extern int copy_to_user(void* dst, const void* src, uint32_t size);
        if (copy_to_user(arg, &info, sizeof(info)) != 0) return -1;
        return 0;
    }
    return -1;
}

//device operations for AHCI drives
static device_ops_t ahci_device_ops = {
    .init = NULL,
    .read = ahci_read_sectors,
    .write = ahci_write_sectors,
    .ioctl = ahci_device_ioctl,
    .cleanup = NULL
};

//register partitions for a SATA drive
static void ahci_register_partitions(device_t* base_dev, int drive_no) {
    if (ahci_part_count >= MAX_AHCI_PARTITIONS) return;

    //read MBR sector
    uint32_t mbr_phys;
    uint8_t* mbr = (uint8_t*)kmalloc_physical(512, &mbr_phys);
    if (!mbr) return;

    if (device_read(base_dev, 0, mbr, 512) != 512) {
        kfree(mbr);
        return;
    }
    if (!(mbr[510] == 0x55 && mbr[511] == 0xAA)) return;

    //partition table at offset 446
    for (int i = 0; i < 4 && ahci_part_count < MAX_AHCI_PARTITIONS; i++) {
        uint8_t* e = &mbr[446 + i * 16];
        uint8_t ptype = e[4];
        uint32_t lba_start = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                             ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t sectors = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                           ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);

        if (ptype == 0 || sectors == 0) continue;

        //create partition device
        device_t* pd = &ahci_part_devices[ahci_part_count];
        ahci_part_priv_t* pp = &ahci_part_privs[ahci_part_count];
        memset(pd, 0, sizeof(*pd));
        memset(pp, 0, sizeof(*pp));

        pp->base = base_dev;
        pp->start_lba = lba_start;
        pp->sectors = sectors;
        pd->private_data = pp;

        //name: sata<drive_no>p<idx+1>
        ksnprintf(pd->name, sizeof(pd->name), "sata%dp%d", drive_no, i + 1);
        pd->type = DEVICE_TYPE_STORAGE;
        pd->subtype = DEVICE_SUBTYPE_STORAGE_ATA;
        pd->ops = &ahci_part_ops;

        if (device_register(pd) == 0) {
            pd->status = DEVICE_STATUS_READY;
            ahci_part_count++;
            #if DEBUG_AHCI
            serial_printf("[AHCI] Registered partition: %s (LBA=%d, sectors=%d)\n",
                          pd->name, lba_start, sectors);
            #endif
        }
    }

    kfree(mbr);
}

//rescan partitions on all AHCI drives
void ahci_rescan_partitions(void) {
    //unregister existing partition devices
    for (int i = 0; i < ahci_part_count; i++) {
        device_t* pd = &ahci_part_devices[i];
        if (pd->status == DEVICE_STATUS_READY) {
            device_unregister(pd->device_id);
            memset(pd, 0, sizeof(*pd));
            memset(&ahci_part_privs[i], 0, sizeof(ahci_part_priv_t));
        }
    }
    ahci_part_count = 0;

    #if DEBUG_AHCI
    serial_printf("[AHCI] Rescanning partitions for %d drives\n", ahci_drive_count);
    #endif

    //rescan known AHCI drives without device enumeration
    for (int i = 0; i < 32; i++) {
        ahci_port_data_t* pd = &port_data[i];
        if (pd->device_type == AHCI_DEV_SATA && pd->port) {
            //find the device registered for this port
            device_t* dev = device_find_by_name("sata0"); //for now assume sata0
            if (dev && dev->status == DEVICE_STATUS_READY) {
                #if DEBUG_AHCI
                serial_printf("[AHCI] Scanning partitions on %s\n", dev->name);
                #endif
                ahci_register_partitions(dev, i);
            }
            break; //for now we only handle one drive
        }
    }
}

//probe ports and register devices
int ahci_probe_and_register(void) {
    if (!ahci_initialized) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] Not initialized\n");
        #endif
        return -1;
    }

    uint32_t pi = abar->pi;
    int device_count = 0;

    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            ahci_hba_port_t* port = (ahci_hba_port_t*)((uintptr_t)abar + 0x100 + (i * 0x80));
            int dt = ahci_check_type(port);

            if (dt == AHCI_DEV_SATA) {
                #if DEBUG_AHCI
                serial_printf("[AHCI] SATA drive found on port %d\n", i);
                #endif

                //rebase port
                if (ahci_port_rebase(port, i) < 0) {
                    #if DEBUG_AHCI
                    serial_printf("[AHCI] Failed to rebase port %d\n", i);
                    #endif
                    continue;
                }

                port_data[i].device_type = dt;

                //run IDENTIFY DEVICE to get disk size
                uint16_t id_buffer[256];
                if (ahci_identify(&port_data[i], id_buffer) == 0) {
                    //parse sector count from IDENTIFY data
                    //words 100-103 contain 48-bit LBA capacity (if supported)
                    //words 60-61 contain 28-bit LBA capacity
                    uint64_t lba48 = ((uint64_t)id_buffer[103] << 48) |
                                     ((uint64_t)id_buffer[102] << 32) |
                                     ((uint64_t)id_buffer[101] << 16) |
                                     ((uint64_t)id_buffer[100]);
                    uint32_t lba28 = ((uint32_t)id_buffer[61] << 16) | (uint32_t)id_buffer[60];

                    //use LBA48 if available (non-zero), otherwise use LBA28
                    port_data[i].total_sectors = (lba48 != 0) ? lba48 : lba28;

                    #if DEBUG_AHCI
                    serial_printf("[AHCI] Drive size: %u sectors (LBA28=%u, LBA48=%llu)\n",
                                  (uint32_t)port_data[i].total_sectors, lba28, lba48);
                    #endif
                } else {
                    #if DEBUG_AHCI
                    serial_write_string("[AHCI] IDENTIFY command failed\n");
                    #endif
                    port_data[i].total_sectors = 0;
                }

                //create device structure
                device_t* dev = (device_t*)kmalloc(sizeof(device_t));
                if (!dev) {
                    #if DEBUG_AHCI
                    serial_write_string("[AHCI] Failed to allocate device structure\n");
                    #endif
                    continue;
                }

                memset(dev, 0, sizeof(device_t));
                ksnprintf(dev->name, sizeof(dev->name), "sata%d", i);
                dev->type = DEVICE_TYPE_STORAGE;
                dev->subtype = DEVICE_SUBTYPE_STORAGE_ATA;
                dev->device_id = 0x1000 + i; //arbitrary ID
                dev->private_data = &port_data[i];
                dev->ops = &ahci_device_ops;

                if (device_register(dev) == 0) {
                    //device manager sets status to UNINITIALIZED, need to set it to READY
                    dev->status = DEVICE_STATUS_READY;
                    #if DEBUG_AHCI
                    serial_printf("[AHCI] Registered device: %s (status=%d)\n",
                                  dev->name, dev->status);
                    #endif

                    //scan and register partitions
                    ahci_register_partitions(dev, i);
                    ahci_drive_count++;
                    device_count++;
                } else {
                    #if DEBUG_AHCI
                    serial_write_string("[AHCI] Failed to register device\n");
                    #endif
                    kfree(dev);
                }
            }
        }
    }

    #if DEBUG_AHCI
    serial_printf("[AHCI] Found and registered %d SATA device(s)\n", device_count);
    #endif

    return device_count;
}

//initialize AHCI controller
void ahci_init(void) {
    #if DEBUG_AHCI
    serial_write_string("[AHCI] Initializing AHCI driver\n");
    #endif

    //find AHCI controller via PCI
    if (pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, PCI_PROG_IF_AHCI, &ahci_pci_dev) < 0) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] No AHCI controller found\n");
        #endif
        return;
    }

    #if DEBUG_AHCI
    serial_printf("[AHCI] Found AHCI controller at %x:%x:%x\n",
                  ahci_pci_dev.bus, ahci_pci_dev.slot, ahci_pci_dev.func);
    #endif

    //enable bus mastering and memory space
    pci_enable_bus_mastering(&ahci_pci_dev);
    pci_enable_memory_space(&ahci_pci_dev);

    //get ABAR (AHCI Base Address Register) from BAR5
    uint32_t abar_addr = ahci_pci_dev.bar[5] & 0xFFFFFFF0;
    if (abar_addr == 0) {
        #if DEBUG_AHCI
        serial_write_string("[AHCI] Invalid ABAR address\n");
        #endif
        return;
    }

    //map ABAR into virtual memory (identity map it for now)
    //AHCI registers are typically 8KB in size
    #if DEBUG_AHCI
    serial_printf("[AHCI] Mapping ABAR 0x%x into virtual memory\n", abar_addr);
    #endif

    for (uint32_t offset = 0; offset < 0x2000; offset += 0x1000) {
        if (vmm_map_page(abar_addr + offset, abar_addr + offset, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            #if DEBUG_AHCI
            serial_printf("[AHCI] Failed to map ABAR page at 0x%x\n", abar_addr + offset);
            #endif
            return;
        }
    }

    abar = (ahci_hba_mem_t*)(uintptr_t)abar_addr;

    #if DEBUG_AHCI
    serial_printf("[AHCI] ABAR at 0x%x\n", (uint32_t)abar_addr);
    serial_printf("[AHCI] Host capabilities: 0x%x\n", abar->cap);
    serial_printf("[AHCI] Ports implemented: 0x%x\n", abar->pi);
    #endif

    //enable AHCI mode
    abar->ghc |= HBA_GHC_AHCI_ENABLE;

    ahci_initialized = 1;

    #if DEBUG_AHCI
    serial_write_string("[AHCI] AHCI controller initialized\n");
    #endif
}
