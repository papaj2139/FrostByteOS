#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <stdint.h>

//device types
typedef enum {
    DEVICE_TYPE_STORAGE,
    DEVICE_TYPE_INPUT,
    DEVICE_TYPE_OUTPUT,
    DEVICE_TYPE_NETWORK,
    DEVICE_TYPE_TIMER,
    DEVICE_TYPE_UNKNOWN
} device_type_t;

//device status
typedef enum {
    DEVICE_STATUS_UNINITIALIZED,
    DEVICE_STATUS_INITIALIZING,
    DEVICE_STATUS_READY,
    DEVICE_STATUS_ERROR,
    DEVICE_STATUS_DISABLED
} device_status_t;

//forward declaration
struct device;

//device operations structure
typedef struct {
    int (*init)(struct device* dev);
    int (*read)(struct device* dev, uint32_t offset, void* buffer, uint32_t size);
    int (*write)(struct device* dev, uint32_t offset, const void* buffer, uint32_t size);
    int (*ioctl)(struct device* dev, uint32_t cmd, void* arg);
    void (*cleanup)(struct device* dev);
} device_ops_t;

//generic device structure
typedef struct device {
    char name[32];
    device_type_t type;
    device_status_t status;
    uint32_t device_id;
    void* private_data;
    const device_ops_t* ops;
    struct device* next;
} device_t;

//device manager functions
void device_manager_init(void);
int device_register(device_t* device);
int device_unregister(uint32_t device_id);
device_t* device_find_by_id(uint32_t device_id);
device_t* device_find_by_name(const char* name);
device_t* device_find_by_type(device_type_t type);
void device_list_all(void);
int device_init(device_t* device);
int device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int device_ioctl(device_t* device, uint32_t cmd, void* arg);

#endif
