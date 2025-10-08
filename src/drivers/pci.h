#ifndef PCI_H
#define PCI_H

#include <stdint.h>

//PCI configuration space registers
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

//PCI configuration space offsets
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_PROG_IF        0x09
#define PCI_SUBCLASS       0x0A
#define PCI_CLASS          0x0B
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_INTERRUPT_LINE 0x3C

//PCI command register bits
#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_MASTER      0x0004
#define PCI_COMMAND_INTERRUPT   0x0400

//PCI class codes
#define PCI_CLASS_STORAGE       0x01
#define PCI_SUBCLASS_SATA       0x06
#define PCI_PROG_IF_AHCI        0x01

//PCI device structure
typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar[6];
} pci_device_t;

//function declarations
void pci_init(void);
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out);
void pci_enable_bus_mastering(pci_device_t* dev);
void pci_enable_memory_space(pci_device_t* dev);

#endif