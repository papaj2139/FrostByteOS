#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct block {
    size_t size;
    int free;
    struct block* next;
} block_t;

#define BLOCK_SIZE sizeof(block_t)

static block_t* head = NULL;

static unsigned long rand_next = 1;


//find a free block that fits
static block_t* find_free_block(block_t** last, size_t size) {
    block_t* current = head;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

//request more memory from kernel
static block_t* request_space(block_t* last, size_t size) {
    block_t* block = sbrk(0);
    void* request = sbrk(size + BLOCK_SIZE);
    if (request == (void*)-1) {
        return NULL;
    }

    if (last) {
        last->next = block;
    }

    block->size = size;
    block->free = 0;
    block->next = NULL;
    return block;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    block_t* block;

    if (!head) {
        block = request_space(NULL, size);
        if (!block) {
            return NULL;
        }
        head = block;
    } else {
        block_t* last = head;
        block = find_free_block(&last, size);
        if (!block) {
            block = request_space(last, size);
            if (!block) {
                return NULL;
            }
        } else {
            block->free = 0;
        }
    }

    return (void*)(block + 1);
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }

    block_t* block = (block_t*)ptr - 1;
    block->free = 1;

    //simple asf coalescing: merge with next block if its free
    if (block->next && block->next->free) {
        block->size += BLOCK_SIZE + block->next->size;
        block->next = block->next->next;
    }
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_t* block = (block_t*)ptr - 1;
    if (block->size >= size) {
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}


int atoi(const char* str) {
    return (int)strtol(str, NULL, 10);
}

long atol(const char* str) {
    return strtol(str, NULL, 10);
}

long strtol(const char* str, char** endptr, int base) {
    const char* s = str;
    long result = 0;
    int negative = 0;

    //skip whitespace
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }

    //handle sign
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    //auto-detect base
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    //convert digits
    while (*s) {
        int digit = -1;

        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            break;
        }

        result = result * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char*)s;
    }

    return negative ? -result : result;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    const char* s = str;
    unsigned long result = 0;

    //skip whitespace
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }

    //skip + sign
    if (*s == '+') {
        s++;
    }

    //auto-detect base
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    //convert digits
    while (*s) {
        int digit = -1;

        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            break;
        }

        result = result * base + (unsigned long)digit;
        s++;
    }

    if (endptr) {
        *endptr = (char*)s;
    }

    return result;
}


void exit(int status) {
    _exit(status);
    while(1); //should never reach
}

void abort(void) {
    _exit(1);
    while(1); //should never reach
}

int rand(void) {
    rand_next = rand_next * 1103515245 + 12345;
    return (unsigned int)(rand_next / 65536) % 32768;
}

void srand(unsigned int seed) {
    rand_next = seed;
}

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}
