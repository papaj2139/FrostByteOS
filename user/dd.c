#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_BLOCK_SIZE 512

static void print_usage(void) {
    fprintf(2, "Usage: dd if=<input> of=<output> [bs=<block_size>] [count=<num_blocks>] [skip=<blocks>] [seek=<blocks>]\n");
    fprintf(2, "  if=FILE         read from FILE instead of stdin\n");
    fprintf(2, "  of=FILE         write to FILE instead of stdout\n");
    fprintf(2, "  bs=BYTES        read and write BYTES bytes at a time (default: 512)\n");
    fprintf(2, "  count=N         copy only N input blocks\n");
    fprintf(2, "  skip=N          skip N input blocks at start\n");
    fprintf(2, "  seek=N          skip N output blocks at start\n");
}

static int parse_number(const char* str) {
    int result = 0;
    int len = strlen(str);
    int multiplier = 1;
    
    //check for suffixes (k, M, G)
    if (len > 0) {
        char last = str[len - 1];
        if (last == 'k' || last == 'K') {
            multiplier = 1024;
            len--;
        } else if (last == 'M') {
            multiplier = 1024 * 1024;
            len--;
        } else if (last == 'G') {
            multiplier = 1024 * 1024 * 1024;
            len--;
        }
    }
    
    for (int i = 0; i < len; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return -1;
        }
        result = result * 10 + (str[i] - '0');
    }
    
    return result * multiplier;
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    
    const char* input_file = NULL;
    const char* output_file = NULL;
    int block_size = DEFAULT_BLOCK_SIZE;
    int count = -1;  //-1 means copy everything
    int skip = 0;
    int seek = 0;
    
    //parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "if=", 3) == 0) {
            input_file = argv[i] + 3;
        } else if (strncmp(argv[i], "of=", 3) == 0) {
            output_file = argv[i] + 3;
        } else if (strncmp(argv[i], "bs=", 3) == 0) {
            block_size = parse_number(argv[i] + 3);
            if (block_size <= 0) {
                fprintf(2, "dd: invalid block size\n");
                return 1;
            }
        } else if (strncmp(argv[i], "count=", 6) == 0) {
            count = parse_number(argv[i] + 6);
            if (count < 0) {
                fprintf(2, "dd: invalid count\n");
                return 1;
            }
        } else if (strncmp(argv[i], "skip=", 5) == 0) {
            skip = parse_number(argv[i] + 5);
            if (skip < 0) {
                fprintf(2, "dd: invalid skip value\n");
                return 1;
            }
        } else if (strncmp(argv[i], "seek=", 5) == 0) {
            seek = parse_number(argv[i] + 5);
            if (seek < 0) {
                fprintf(2, "dd: invalid seek value\n");
                return 1;
            }
        } else {
            fprintf(2, "dd: unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }
    
    //validate input/output
    if (!input_file || !output_file) {
        fprintf(2, "dd: both if= and of= must be specified\n");
        print_usage();
        return 1;
    }
    
    //open input file
    int in_fd = open(input_file, 0);
    if (in_fd < 0) {
        fprintf(2, "dd: cannot open '%s' for reading\n", input_file);
        return 1;
    }
    
    //open output file (O_WRONLY | O_CREAT | O_TRUNC = 0x241)
    int out_fd = open(output_file, 0x241);
    if (out_fd < 0) {
        fprintf(2, "dd: cannot open '%s' for writing\n", output_file);
        close(in_fd);
        return 1;
    }
    
    //allocate buffer
    char* buffer = malloc(block_size);
    if (!buffer) {
        fprintf(2, "dd: cannot allocate buffer\n");
        close(in_fd);
        close(out_fd);
        return 1;
    }
    
    //skip input blocks if requested
    if (skip > 0) {
        for (int i = 0; i < skip; i++) {
            int r = read(in_fd, buffer, block_size);
            if (r < 0) {
                fprintf(2, "dd: error reading input\n");
                free(buffer);
                close(in_fd);
                close(out_fd);
                return 1;
            }
            if (r == 0) {
                fprintf(2, "dd: reached end of input while skipping\n");
                free(buffer);
                close(in_fd);
                close(out_fd);
                return 1;
            }
        }
    }
    
    //seek output blocks if requested
    if (seek > 0) {
        //write zeros for seek blocks
        memset(buffer, 0, block_size);
        for (int i = 0; i < seek; i++) {
            int w = write(out_fd, buffer, block_size);
            if (w < block_size) {
                fprintf(2, "dd: error seeking output\n");
                free(buffer);
                close(in_fd);
                close(out_fd);
                return 1;
            }
        }
    }
    
    //copy data
    int blocks_copied = 0;
    int blocks_partial = 0;
    int bytes_total = 0;
    
    while (count < 0 || blocks_copied < count) {
        int r = read(in_fd, buffer, block_size);
        if (r < 0) {
            fprintf(2, "dd: error reading input\n");
            free(buffer);
            close(in_fd);
            close(out_fd);
            return 1;
        }
        
        if (r == 0) break;  //EOF
        
        int w = write(out_fd, buffer, r);
        if (w != r) {
            fprintf(2, "dd: error writing output\n");
            free(buffer);
            close(in_fd);
            close(out_fd);
            return 1;
        }
        
        bytes_total += r;
        
        if (r == block_size) {
            blocks_copied++;
        } else {
            blocks_partial++;
            break;  //partial block means EOF
        }
    }
    
    //cleanup
    free(buffer);
    close(in_fd);
    close(out_fd);
    
    //print statistics
    fprintf(2, "%d+%d blocks copied (%d bytes)\n", blocks_copied, blocks_partial, bytes_total);
    
    return 0;
}
