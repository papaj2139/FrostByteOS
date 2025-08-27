#include "device_manager.h"
#include <stdint.h>
#include <string.h>

//forward declaration
void print(char* msg, unsigned char colour);

//device manager state
static device_t* device_list_head = NULL;
static uint32_t next_device_id = 1;

void device_manager_init(void) {
    device_list_head = NULL;
    next_device_id = 1;
}

int device_register(device_t* device) {
    if (!device || !device->ops) {
        return -1; //invalid device
    }
    
    //assign unique device ID
    device->device_id = next_device_id++;
    device->status = DEVICE_STATUS_UNINITIALIZED;
    
    //add to linked list
    device->next = device_list_head;
    device_list_head = device;
    
    return 0;
}

int device_unregister(uint32_t device_id) {
    device_t** current = &device_list_head;
    
    while (*current) {
        if ((*current)->device_id == device_id) {
            device_t* to_remove = *current;
            *current = (*current)->next;
            
            //cleanup if device has cleanup function
            if (to_remove->ops->cleanup) {
                to_remove->ops->cleanup(to_remove);
            }
            
            return 0;
        }
        current = &(*current)->next;
    }
    
    return -1; //device not found
}

device_t* device_find_by_id(uint32_t device_id) {
    device_t* current = device_list_head;
    
    while (current) {
        if (current->device_id == device_id) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

device_t* device_find_by_name(const char* name) {
    device_t* current = device_list_head;
    
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

device_t* device_find_by_type(device_type_t type) {
    device_t* current = device_list_head;
    
    while (current) {
        if (current->type == type) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

void device_list_all(void) {
    device_t* current = device_list_head;
    
    if (!current) {
        print("  No devices registered\n", 0x0C);
        return;
    }
    
    while (current) {
        print("  ID: ", 0x0F);
        //ID printing (just show last digit for now)
        char id_str[2] = {'0' + (current->device_id % 10), '\0'};
        print(id_str, 0x0F);
        print(" Name: ", 0x0F);
        print(current->name, 0x0A);
        print(" Type: ", 0x0F);
        
        switch (current->type) {
            case DEVICE_TYPE_STORAGE:
                print("Storage", 0x0F);
                break;
            case DEVICE_TYPE_INPUT:
                print("Input", 0x0F);
                break;
            case DEVICE_TYPE_OUTPUT:
                print("Output", 0x0F);
                break;
            case DEVICE_TYPE_NETWORK:
                print("Network", 0x0F);
                break;
            case DEVICE_TYPE_TIMER:
                print("Timer", 0x0F);
                break;
            default:
                print("Unknown", 0x0F);
                break;
        }
        
        print(" Subtype: ", 0x0F);
        switch (current->subtype) {
            case DEVICE_SUBTYPE_GENERIC:
                print("Generic", 0x0F);
                break;
            case DEVICE_SUBTYPE_AUDIO:
                print("Audio", 0x0F);
                break;
            case DEVICE_SUBTYPE_DISPLAY:
                print("Display", 0x0F);
                break;
            case DEVICE_SUBTYPE_KEYBOARD:
                print("Keyboard", 0x0F);
                break;
            case DEVICE_SUBTYPE_MOUSE:
                print("Mouse", 0x0F);
                break;
            case DEVICE_SUBTYPE_STORAGE_ATA:
                print("ATA Storage", 0x0F);
                break;
            case DEVICE_SUBTYPE_STORAGE_USB:
                print("USB Storage", 0x0F);
                break;
            case DEVICE_SUBTYPE_NETWORK_ETH:
                print("Ethernet", 0x0F);
                break;
            case DEVICE_SUBTYPE_NETWORK_WIFI:
                print("WiFi", 0x0F);
                break;
            default:
                print("Unknown", 0x0F);
                break;
        }
        
        print(" Status: ", 0x0F);
        switch (current->status) {
            case DEVICE_STATUS_READY:
                print("Ready", 0x0A);
                break;
            case DEVICE_STATUS_ERROR:
                print("Error", 0x0C);
                break;
            case DEVICE_STATUS_INITIALIZING:
                print("Initializing", 0x0E);
                break;
            case DEVICE_STATUS_DISABLED:
                print("Disabled", 0x08);
                break;
            default:
                print("Uninitialized", 0x07);
                break;
        }
        print("\n", 0x0F);
        
        current = current->next;
    }
}

device_t* device_find_by_subtype(device_subtype_t subtype) {
    device_t* current = device_list_head;
    
    while (current) {
        if (current->subtype == subtype) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

device_t* device_find_by_type_and_subtype(device_type_t type, device_subtype_t subtype) {
    device_t* current = device_list_head;
    
    while (current) {
        if (current->type == type && current->subtype == subtype) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}



int device_init(device_t* device) {
    if (!device || !device->ops || !device->ops->init) {
        return -1;
    }
    
    device->status = DEVICE_STATUS_INITIALIZING;
    int result = device->ops->init(device);
    
    if (result == 0) {
        device->status = DEVICE_STATUS_READY;
    } else {
        device->status = DEVICE_STATUS_ERROR;
    }
    
    return result;
}

int device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    if (!device || !device->ops || !device->ops->read) {
        return -1;
    }
    
    if (device->status != DEVICE_STATUS_READY) {
        return -2; //device not ready
    }
    
    return device->ops->read(device, offset, buffer, size);
}

int device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size) {
    if (!device || !device->ops || !device->ops->write) {
        return -1;
    }
    
    if (device->status != DEVICE_STATUS_READY) {
        return -2; //device not ready
    }
    
    return device->ops->write(device, offset, buffer, size);
}

int device_ioctl(device_t* device, uint32_t cmd, void* arg) {
    if (!device || !device->ops || !device->ops->ioctl) {
        return -1;
    }
    
    if (device->status != DEVICE_STATUS_READY) {
        return -2; //device not ready
    }
    
    return device->ops->ioctl(device, cmd, arg);
}
