#include "initramfs_cpio.h"
#include "initramfs.h"
#include "vfs.h"
#include "../libc/string.h"

//minimal newc (SVR4) CPIO parser
//based on linux docs
//reference: https://www.kernel.org/doc/Documentation/early-userspace/buffer-format.txt

static inline uint32_t hex8(const char* s) {
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
    }
    return v;
}

static inline const uint8_t* align4(const uint8_t* p, const uint8_t* base) {
    uintptr_t off = (uintptr_t)(p - base);
    uintptr_t a = (off + 3) & ~((uintptr_t)3);
    return base + a;
}

int initramfs_load_cpio(const uint8_t* start, const uint8_t* end) {
    if (!start || !end || end <= start) return -1;
    const uint8_t* base = start;
    const uint8_t* p = start;
    while (p + 110 <= end) {
        const char* hdr = (const char*)p;
        if (memcmp(hdr, "070701", 6) != 0) {
            //not newc
            return -1;
        }
        //fields are 8-hex chars each (except magic)
        uint32_t ino       = hex8(hdr + 6);
        uint32_t mode      = hex8(hdr + 14);
        uint32_t uid       = hex8(hdr + 22);
        uint32_t gid       = hex8(hdr + 30);
        uint32_t nlink     = hex8(hdr + 38);
        uint32_t mtime     = hex8(hdr + 46);
        uint32_t filesize  = hex8(hdr + 54);
        uint32_t devmajor  = hex8(hdr + 62);
        uint32_t devminor  = hex8(hdr + 70);
        uint32_t rdevmajor = hex8(hdr + 78);
        uint32_t rdevminor = hex8(hdr + 86);
        uint32_t namesize  = hex8(hdr + 94);
        uint32_t check     = hex8(hdr + 102);
        (void)ino; (void)uid; (void)gid; (void)nlink; (void)mtime;
        (void)devmajor; (void)devminor; (void)rdevmajor; (void)rdevminor; (void)check;

        p += 110;
        if (p + namesize > end) return -1;
        const char* name = (const char*)p;
        //name includes NUL terminator
        p += namesize;
        p = align4(p, base);
        if (strcmp(name, "TRAILER!!!") == 0) {
            break;
        }

        const uint8_t* filedata = p;
        if (p + filesize > end) return -1;
        const uint8_t* fileend = p + filesize;
        p = align4(fileend, base);

        //construct absolute path
        char path[512];
        if (name[0] == '/') {
            strncpy(path, name, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        } else {
            path[0] = '/';
            strncpy(path + 1, name, sizeof(path) - 2);
            path[sizeof(path) - 1] = '\0';
        }

        //filter out "." entries
        if (strcmp(path, "/.") == 0) continue;

        //determine type from mode
        const uint32_t S_IFMT  = 0170000;
        const uint32_t S_IFDIR = 0040000;
        const uint32_t S_IFREG = 0100000;
        const uint32_t S_IFLNK = 0120000;
        uint32_t ftype = mode & S_IFMT;

        //persist permissions and ownership via VFS overlay so later opens see correct exec bits
        vfs_set_metadata_override(path, 1, (mode & 07777), 1, uid, 1, gid);

        if (ftype == S_IFDIR) {
            //ensure dir exists
            initramfs_add_dir(path);
        } else if (ftype == S_IFREG) {
            //ensure parent exists, then add file
            //initramfs_add_file handles creating parents via ensure_dir_path
            initramfs_add_file(path, filedata, filesize);
        } else if (ftype == S_IFLNK) {
            //symlink: filedata contains target path (not necessarily NUL-terminated)
            char target[512];
            uint32_t tlen = filesize;
            if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
            memcpy(target, filedata, tlen);
            target[tlen] = '\0';
            initramfs_add_symlink(path, target);
        } else {
            //ignore other types (devices, fifos, etc.)
        }
    }
    return 0;
}
