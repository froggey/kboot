/*
 * Copyright (C) 2015 Alex Smith
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file
 * @brief               KBoot installation utility.
 */

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support.h"

/** Directory containing target binaries. */
static const char *target_bin_dir;

/** Parsed arguments. */
static const char *arg_device;
static const char *arg_dir;
static int arg_fallback;
static const char *arg_image;
static uint64_t arg_offset;
static const char *arg_path;
static const char *arg_target;
static const char *arg_vendor_id = "kboot";
static int arg_verbose;

/** Details of the installation device. */
static const char *device_path;
static const char *device_root;
static int device_fd;

/**
 * Helper functions.
 */

/** Print an error message and exit.
 * @param str           Format for message to print.
 * @param ...           Arguments to substitute into format. */
static void error(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

/** Print an verbose debug message.
 * @param str           Format for message to print.
 * @param ...           Arguments to substitute into format. */
static void verbose(const char *fmt, ...) {
    if (arg_verbose) {
        va_list args;

        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
}

/** Open a file in the target binary directory.
 * @param name          Name of file to open.
 * @return              File descriptor to file. */
static int open_target_bin(const char *name) {
    char buf[PATH_MAX];
    int fd;

    snprintf(buf, sizeof(buf), "%s/%s", target_bin_dir, name);

    fd = open(buf, O_RDONLY);
    if (fd < 0)
        error("Error opening '%s': %s\n", name, strerror(errno));

    return fd;
}

/** Read a file from the target binary directory.
 * @param name          Name of file to read.
 * @param _buf          Where to store pointer to buffer containing file data.
 * @param _size         Where to store file size. */
static void read_target_bin(const char *name, void **_buf, size_t *_size) {
    int fd;
    struct stat st;
    void *buf;

    fd = open_target_bin(name);

    if (fstat(fd, &st) != 0) {
        goto err;
    } else if ((uint64_t)st.st_size > SSIZE_MAX) {
        errno = EFBIG;
        goto err;
    }

    buf = malloc(st.st_size);
    if (!buf)
        goto err;

    if (read(fd, buf, st.st_size) != st.st_size)
        goto err;

    close(fd);

    *_buf = buf;
    *_size = st.st_size;
    return;

err:
    close(fd);
    error("Error reading '%s': %s\n", name, strerror(errno));
}

/** Copy a target binary.
 * @param name          Name of file to copy.
 * @param path          Path to copy to. */
static void copy_target_bin(const char *name, const char *path) {
    void *buf;
    size_t size;
    int fd;

    read_target_bin(name, &buf, &size);

    fd = open(path, O_CREAT | O_TRUNC | O_NOFOLLOW | O_WRONLY, 0644);
    if (fd < 0)
        error("Error creating '%s': %s\n", path, strerror(errno));

    if (write(fd, buf, size) != (ssize_t)size)
        error("Error writing '%s': %s\n", path, strerror(errno));

    close(fd);
    free(buf);
}

/**
 * Device functions.
 */

/** Read from the device.
 * @param buf           Buffer to read into.
 * @param count         Number of bytes to read.
 * @param offset        Offset to write at.
 * @return              Number of bytes read on success, -1 on failure. */
static ssize_t read_device(void *buf, size_t count, uint64_t offset) {
    return pread(device_fd, buf, count, arg_offset + offset);
}

/** Write to the device.
 * @param buf           Buffer to write from.
 * @param count         Number of bytes to write.
 * @param offset        Offset to write at. */
static ssize_t write_device(const void *buf, size_t count, uint64_t offset) {
    return pwrite(device_fd, buf, count, arg_offset + offset);
}

/** Open the device according to the options. */
static void open_device(void) {
    if (arg_dir) {
        char *dev, *root;

        verbose("Installing to directory '%s'\n", arg_dir);

        /* Get the device containing this path. */
        if (os_device_from_path(arg_dir, &dev, &root) != 0)
            error("Failed to determine device containing '%s': %s\n", arg_dir, strerror(errno));

        verbose("Resolved '%s' to device '%s' (root: '%s')\n", arg_dir, dev, root);
        device_path = dev;
        device_root = root;
    } else if (arg_device) {
        verbose("Installing to device '%s'\n", arg_device);
        device_path = arg_device;
    } else {
        verbose("Installing to image '%s' at offset %llu\n", arg_image, arg_offset);
        device_path = arg_image;
    }

    device_fd = open(device_path, O_RDWR);
    if (device_fd < 0)
        error("Error opening '%s': %s\n", device_path, strerror(errno));
}

/**
 * Filesystem support.
 */

/** ext2 filesystem magic number. */
#define EXT2_MAGIC_OFFSET   1080
#define EXT2_MAGIC_0        0x53
#define EXT2_MAGIC_1        0xef

/** ext2 boot sector structure. */
typedef struct ext2_boot_sector {
    uint8_t code[992];
    char path[32];
} ext2_boot_sector_t;

/** Check whether the device contains an ext* filesystem.
 * @return              1 if filesystem found, 0 if not, -1 on error. */
static int ext2_identify(void) {
    uint8_t buf[2];
    ssize_t ret;

    /* Read in the magic number. */
    ret = read_device(buf, sizeof(buf), EXT2_MAGIC_OFFSET);
    if (ret < 0) {
        return -1;
    } else if ((size_t)ret < sizeof(buf)) {
        return 0;
    }

    return (buf[0] == EXT2_MAGIC_0 && buf[1] == EXT2_MAGIC_1);
}

/** Install an ext* boot sector.
 * @param buf           Buffer containing boot sector to write.
 * @param size          Size of the boot sector.
 * @param path          Normalized path to kboot.bin.
 * @return              0 on success, -1 on error. */
static int ext2_install(void *buf, size_t size, const char *path) {
    ext2_boot_sector_t *bs = buf;
    ssize_t ret;

    if (size != sizeof(*bs))
        error("Boot sector is incorrect size (got %zu, expected %zu)\n", size, sizeof(*bs));

    /* Copy in the path string. */
    strncpy(bs->path, path, sizeof(bs->path));

    /* Write the boot sector. */
    ret = write_device(bs, sizeof(*bs), 0);
    if (ret < 0) {
        return ret;
    } else if (ret != sizeof(*bs)) {
        errno = EIO;
        return -1;
    }

    return 0;
}

/** Supported filesystem types. */
static struct {
    const char *name;
    int (*identify)(void);
    int (*install)(void *buf, size_t size, const char *path);
} fs_types[] = {
    { "ext2", ext2_identify, ext2_install },
};

/**
 * BIOS target specifics.
 */

/** Size of the path buffer in a boot sector. */
#define BOOT_SECTOR_PATH_SIZE 32

/** Copy the boot loader to the installation directory. */
static void copy_boot_loader(void) {
    char dest[PATH_MAX];
    char *abs;
    size_t root_len;

    assert(!arg_path);
    assert(device_root);

    /* Copy the boot loader. */
    snprintf(dest, sizeof(dest), "%s/kboot.bin", arg_dir);
    copy_target_bin("kboot.bin", dest);

    /* Determine the boot loader path relative to the root of the installation
     * device for the boot sector. */
    abs = realpath(dest, NULL);
    if (!abs)
        error("Error getting absolute path for '%s': %s\n", dest, strerror(errno));

    root_len = strlen(device_root);
    if (device_root[root_len - 1] == '/')
        root_len--;
    if (strncmp(abs, device_root, root_len) != 0 || abs[root_len] != '/')
        error("Root is not a prefix of installation directory, something went wrong\n");

    arg_path = strdup(abs + root_len + 1);
    free(abs);
    verbose("Boot loader relative path is '%s'\n", arg_path);
}

/**
 * Normalize a path string.
 *
 * The boot sectors require a path string that is relative (does not start with
 * a '/'), and has no duplicate '/' characters. This reduces the complexity of
 * their path parsing code, and also helps paths to fit in their limited size
 * path buffers (32 bytes). This function takes a path string and returns a new
 * one which conforms to these restrictions.
 *
 * @param path          Path to kboot.bin.
 * @param buf           32 byte buffer to write into. An error will be raised if
 *                      the string is too long.
 */
static void normalize_path(const char *path, char *buf) {
    const char *orig = path;
    size_t len = 0;

    while (*path == '/')
        path++;

    while (*path) {
        if (len == BOOT_SECTOR_PATH_SIZE - 1)
            error("Loader path '%s' is too long to fit in boot sector\n", orig);

        buf[len++] = *path;

        if (*path++ == '/') {
            while (*path == '/')
                path++;
        }
    }

    buf[len] = 0;
    verbose("Normalized loader path is '%s'\n", buf);
}

/** Install the boot sector. */
static void install_boot_sector(void) {
    char path[BOOT_SECTOR_PATH_SIZE];
    char name[32];
    void *bs;
    size_t i, size;

    /* Normalize the path to the boot loader binary. */
    normalize_path(arg_path, path);

    /* Try to determine the filesystem type to install the boot sector to. */
    i = 0;
    while (true) {
        int ret;

        if (i == array_size(fs_types))
            error("Could not identify filesystem type on '%s'\n", device_path);

        ret = fs_types[i].identify();
        if (ret < 0) {
            error("Error reading '%s': %s\n", path, strerror(errno));
        } else if (ret) {
            break;
        }

        i++;
    }

    /* Get the boot sector for this filesystem type. */
    snprintf(name, sizeof(name), "%sboot.bin", fs_types[i].name);
    read_target_bin(name, &bs, &size);

    verbose(
        "Installing boot sector to '%s' at offset %llu, filesystem type '%s'\n",
        device_path, arg_offset, fs_types[i].name);

    /* Install it. */
    if (fs_types[i].install(bs, size, path) != 0)
        error("Error writing to '%s': %s\n", device_path, strerror(errno));
}

/** Install KBoot for BIOS-based systems.
 * @param arg           Unused. */
static void bios_install(const char *arg) {
    /* Open the installation device. */
    open_device();

    /* If installing to a directory, copy the boot loader binary. */
    if (arg_dir)
        copy_boot_loader();

    /* Install the boot sector. */
    install_boot_sector();
}

/**
 * EFI target specifics.
 */

/** Install KBoot for EFI-based systems.
 * @param arch          EFI architecture name. */
static void efi_install(const char *arch) {
    error("EFI installation not yet implemented\n");
}

/**
 * Main functions.
 */

/** Array of targets with specialised installation helper functions. */
static struct {
    const char *name;
    void (*func)(const char *arg);
    const char *arg;
} target_helpers[] = {
    { "bios",      bios_install, NULL },
    { "efi-amd64", efi_install,  "x64" },
};

/** Print usage information.
 * @param argv0         Program name.
 * @param stream        Stream to print to. */
static void usage(const char *argv0, FILE *stream) {
    fprintf(
        stream,
        "Usage: %s OPTIONS...\n"
        "\n"
        "Installs KBoot to a disk or disk image. A target system type must be specified,\n"
        "along with an installation location. The installation location can either be a\n"
        "directory, device, or disk image, depending on the target system type.\n"
        "\n"
        "On BIOS systems, when a directory is specified, the loader binary will be copied\n"
        "to that directory, and the appropriate boot sector will be installed to the\n"
        "partition containing the directory. When a device or image is specified, it is\n"
        "assumed that kboot.bin has already been copied to the file system, and the path\n"
        "to it must be specified. For a device, the boot sector will be installed at the\n"
        "beginning of the device. For an image, the boot sector will be installed at the\n"
        "specified offset.\n"
        "\n"
        "On EFI systems, only installation to a directory is supported. This directory\n"
        "must be the root of an EFI System Partition. The loader binary will be copied\n"
        "to either /EFI/<vendor ID>/kboot<arch>.efi, or /EFI/BOOT/boot<arch>.efi if\n"
        "installation to the fallback directory is requested. If not installing to the\n"
        "fallback directory, an EFI boot entry will be added at the top of the boot\n"
        "list.\n"
        "\n"
        "Generic options:\n"
        "  --help, -h        Show this help\n"
        "  --target=TARGET   Specify target system type\n"
        "\n"
        "Installation location options:\n"
        "  --device=DEVICE   Install to a device\n"
        "  --dir=DIR         Install to a directory\n"
        "  --image=FILE      Install to a disk image\n"
        "  --offset=OFFSET   With --image, byte offset of boot partition\n"
        "  --path=PATH       With --device and --image, path to kboot.bin on the device\n"
        "                    or image\n"
        "\n"
        "EFI-specific options:\n"
        "  --fallback        Install to the fallback boot directory\n"
        "  --vendor-id=NAME  Vendor directory name (default: %s)\n"
        "\n",
        argv0, arg_vendor_id);
}

/** Find the target binary directory.
 * @param argv0         Program name. */
static void find_target_bin_dir(const char *argv0) {
    char *program_dir;
    char buf[PATH_MAX];
    struct stat st;

    /* Get directory containing program. */
    program_dir = os_get_program_dir(argv0);
    if (!program_dir)
        error("Failed to get program path\n");

    /* Try to locate the data directory. TODO: Installed location. */
    snprintf(buf, sizeof(buf), "%s/../../../build/%s/bin", program_dir, arg_target);
    if (stat(buf, &st) == 0 && st.st_mode & S_IFDIR) {
        target_bin_dir = realpath(buf, NULL);
    } else {
        error("Target '%s' could not be found\n", arg_target);
    }

    free(program_dir);
}

/** Option identifiers. */
enum {
    OPT_DEVICE = 0x100,
    OPT_DIR,
    OPT_IMAGE,
    OPT_OFFSET,
    OPT_PATH,
    OPT_TARGET,
    OPT_VENDOR_ID,
};

/** Option descriptions. */
static const struct option options[] = {
    { "device",    required_argument, NULL,          OPT_DEVICE    },
    { "dir",       required_argument, NULL,          OPT_DIR       },
    { "fallback",  no_argument,       &arg_fallback, 1             },
    { "help",      no_argument,       NULL,          'h'           },
    { "image",     required_argument, NULL,          OPT_IMAGE     },
    { "offset",    required_argument, NULL,          OPT_OFFSET    },
    { "path",      required_argument, NULL,          OPT_PATH      },
    { "target",    required_argument, NULL,          OPT_TARGET    },
    { "vendor-id", required_argument, NULL,          OPT_VENDOR_ID },
    { "verbose",   no_argument,       &arg_verbose,  1             },
    { NULL,        0,                 NULL,          0             }
};

/** Main function of the KBoot installer. */
int main(int argc, char **argv) {
    int opt;

    /* Parse arguments. */
    while ((opt = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        char *end;

        switch (opt) {
        case OPT_VENDOR_ID:
            arg_vendor_id = optarg;
            break;
        case OPT_DEVICE:
            arg_device = optarg;
            break;
        case OPT_DIR:
            arg_dir = optarg;
            break;
        case 'h':
            usage(argv[0], stdout);
            return EXIT_SUCCESS;
        case OPT_IMAGE:
            arg_image = optarg;
            break;
        case OPT_OFFSET:
            arg_offset = strtoull(optarg, &end, 0);

            if (!end || end == optarg || *end)
                error("Offset must be a 64-bit integer\n");

            break;
        case OPT_PATH:
            arg_path = optarg;
            break;
        case OPT_TARGET:
            arg_target = optarg;
            break;
        case 0:
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    /* Validate options. */
    if (!arg_target)
        error("No target specified\n");
    if (!arg_device && !arg_dir && !arg_image)
        error("No installation location specified\n");
    if ((arg_device && (arg_dir || arg_image)) || (arg_dir && arg_image))
        error("Options --device, --dir and --image are mutually exclusive\n");
    if (arg_offset && !arg_image)
        error("Option --offset is only valid with --image\n");
    if ((arg_device || arg_image) && (!arg_path || !arg_path[0]))
        error("Options --device and --image require --path\n");
    if (arg_dir && arg_path)
        error("Option --path is invalid with --dir\n");

    /* Try to locate target binary directory. */
    find_target_bin_dir(argv[0]);

    /* Check if we have an installation helper. */
    for (size_t i = 0; i < array_size(target_helpers); i++) {
        if (strcmp(target_helpers[i].name, arg_target) == 0) {
            target_helpers[i].func(target_helpers[i].arg);
            return EXIT_SUCCESS;
        }
    }

    // TODO: Do we even need this? Installation for most other targets will
    // amount to just copying the binary into a directory.
    error("TODO: Generic installation\n");
}