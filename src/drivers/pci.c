#include "pci.h"
#include "../io.h"
#include "serial.h"
#include "../debug.h"

//read a dword from PCI configuration space
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

//read a word from PCI configuration space
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

//read a byte from PCI configuration space
uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

//write a dword to PCI configuration space
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

//write a word to PCI configuration space
void pci_config_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    dword = (dword & ~(0xFFFF << shift)) | ((uint32_t)value << shift);
    pci_config_write_dword(bus, slot, func, offset & 0xFC, dword);
}

//check if a device exists at a given bus/slot/func
static int pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_config_read_word(bus, slot, func, PCI_VENDOR_ID);
    return (vendor_id != 0xFFFF);
}

//find a PCI device by vendor and device ID
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                if (!pci_device_exists(bus, slot, func)) continue;
                
                uint16_t vid = pci_config_read_word(bus, slot, func, PCI_VENDOR_ID);
                uint16_t did = pci_config_read_word(bus, slot, func, PCI_DEVICE_ID);
                
                if (vid == vendor_id && did == device_id) {
                    if (out) {
                        out->bus = bus;
                        out->slot = slot;
                        out->func = func;
                        out->vendor_id = vid;
                        out->device_id = did;
                        out->class_code = pci_config_read_byte(bus, slot, func, PCI_CLASS);
                        out->subclass = pci_config_read_byte(bus, slot, func, PCI_SUBCLASS);
                        out->prog_if = pci_config_read_byte(bus, slot, func, PCI_PROG_IF);
                        for (int i = 0; i < 6; i++) {
                            out->bar[i] = pci_config_read_dword(bus, slot, func, PCI_BAR0 + i * 4);
                        }
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}

//find a PCI device by class code
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                if (!pci_device_exists(bus, slot, func)) continue;
                
                uint8_t cc = pci_config_read_byte(bus, slot, func, PCI_CLASS);
                uint8_t sc = pci_config_read_byte(bus, slot, func, PCI_SUBCLASS);
                uint8_t pi = pci_config_read_byte(bus, slot, func, PCI_PROG_IF);
                
                if (cc == class_code && sc == subclass && pi == prog_if) {
                    if (out) {
                        out->bus = bus;
                        out->slot = slot;
                        out->func = func;
                        out->vendor_id = pci_config_read_word(bus, slot, func, PCI_VENDOR_ID);
                        out->device_id = pci_config_read_word(bus, slot, func, PCI_DEVICE_ID);
                        out->class_code = cc;
                        out->subclass = sc;
                        out->prog_if = pi;
                        for (int i = 0; i < 6; i++) {
                            out->bar[i] = pci_config_read_dword(bus, slot, func, PCI_BAR0 + i * 4);
                        }
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}

//enable bus mastering for DMA
void pci_enable_bus_mastering(pci_device_t* dev) {
    if (!dev) return;
    uint16_t command = pci_config_read_word(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    command |= PCI_COMMAND_MASTER;
    pci_config_write_word(dev->bus, dev->slot, dev->func, PCI_COMMAND, command);
}

//enable memory space access
void pci_enable_memory_space(pci_device_t* dev) {
    if (!dev) return;
    uint16_t command = pci_config_read_word(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY;
    pci_config_write_word(dev->bus, dev->slot, dev->func, PCI_COMMAND, command);
}

void pci_init(void) {
    #if DEBUG_ENABLED
    serial_write_string("[PCI] Initializing PCI bus enumeration\n");
    #endif
}