/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../refind/so_elf.h"

/* Check exactly the ELF profile the firmware loader accepts, without executing
 * provider code. Firmware still checks the relocated Common V2 tables. */
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 2;
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || info.st_size > BOOT_SO_MAX_FILE) {
        close(fd);
        return 2;
    }
    FILE *input = fdopen(fd, "rb");
    if (!input) {
        close(fd);
        return 2;
    }
    size_t size = (size_t)info.st_size;
    void *bytes = malloc(size);
    int result = 2;
    if (bytes && fread(bytes, 1, size, input) == size &&
        fgetc(input) == EOF && !ferror(input))
        result = BootSoValidate(bytes, size) != 0;
    free(bytes);
    fclose(input);
    return result;
}
