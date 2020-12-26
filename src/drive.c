// A set of drivers that regulates access to external files.
// All disk image reads/writes go through this single function

#include "drive.h"
#include "platform.h"
#include "state.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#ifndef EMSCRIPTEN
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>
#else
#include <emscripten.h>
#endif

#ifdef EMSCRIPTEN
#define SIMULATE_ASYNC_ACCESS
#endif
#define SIMULATE_ASYNC_ACCESS
#if !defined(EMSCRIPTEN) && defined(SIMULATE_ASYNC_ACCESS)
static void (*global_cb)(void*, int);
static void* global_cb_arg1;
#endif
static int transfer_in_progress = 0;

void drive_cancel_transfers(void)
{
    transfer_in_progress = 0;
}

#define BLOCK_SHIFT 18
#define BLOCK_SIZE (256 * 1024)
#define BLOCK_MASK (BLOCK_SIZE - 1)

// ============================================================================
// Basic drive wrapper functions
// ============================================================================

int drive_read(struct drive_info* info, void* a, void* b, uint32_t c, drv_offset_t d, drive_cb e)
{
    return info->read(info->data, a, b, c, d, e);
}
int drive_prefetch(struct drive_info* info, void* a, uint32_t b, drv_offset_t c, drive_cb d)
{
    return info->prefetch(info->data, a, b, c, d);
}
int drive_write(struct drive_info* info, void* a, void* b, uint32_t c, drv_offset_t d, drive_cb e)
{
    return info->write(info->data, a, b, c, d, e);
}

// ============================================================================
// Struct definitions for block drivers.
// ============================================================================

struct block_info {
    uint32_t pathindex;
    uint32_t modified;
    uint8_t* data; // Note: on the Emscripten version, it will not be valid pointer. Instead, it will be an ID pointing to the this.blocks[] cache.
};

struct drive_internal_info {
#ifdef EMSCRIPTEN
    int drive_id;
#else
#endif
    // IDE callback
    void (*callback)(void*, int);
    void* ide_callback_arg1;
    // Argument 2 is supplied by the status parameter of drive_handler_callback

    // Destination
    void* argument_buffer;
    drv_offset_t argument_position, argument_length;

    drv_offset_t size;
    uint32_t block_count, block_size, path_count; // Size of image, number of blocks, size of block, and paths
    char** paths; // The number of paths present in our disk image.
    struct block_info* blocks;
};

// Contained in each file directory as info.dat
struct drive_info_file {
    uint32_t size;
    uint32_t block_size;
};

// ============================================================================
// Path utilities
// ============================================================================

#ifdef _WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

// Join two paths together
static void join_path(char* dest, int alen, char* a, char* b)
{
    strcpy(dest, a);
    if (dest[alen - 1] != PATHSEP) {
        dest[alen] = PATHSEP;
        alen++;
    }
    strcpy(dest + alen, b);

}

static void drive_get_path(char* dest, char* pathbase, uint32_t x)
{
    char temp[16];
    sprintf(temp, "blk%08x.bin", x);
    join_path(dest, strlen(pathbase), pathbase, temp);
}

// ============================================================================
// Read files
// ============================================================================

// Reads block information from a file.
// Called by drive_internal_read_check
static int drive_read_block_internal(struct drive_internal_info* this, struct block_info* info, void* buffer, uint32_t length, uint32_t position)
{
    uint32_t blockoffs = position % this->block_size;
#ifdef EMSCRIPTEN
    UNUSED(info);
    return EM_ASM_INT({
        /* id, buffer, offset, length */
        return window["drives"][$0]["readCache"]($1, $2, $3, $4) | 0;
    },
        this->drive_id, position / this->block_size, buffer, blockoffs, length);
#else
    memcpy(buffer, info->data + blockoffs, length);
    return 0;
#endif
}

#ifndef EMSCRIPTEN
// Read file from cache, uncompress, and return allocated value
static void* drive_read_file(struct drive_internal_info* this, char* fn)
{
    char temp[1024 + 8];
    int fd;
    unsigned int size;
    void *readbuf, *data;

    sprintf(temp, "%s.gz", fn);
    fd = open(temp, O_RDONLY | O_BINARY);
    if (fd < 0) {
        // Try reading from non-gzipped file instead
        fd = open(fn, O_RDONLY | O_BINARY);
        if (fd < 0) {
            perror("open: ");
            DRIVE_FATAL("Could not open file %s\n", fn);
        }
        data = malloc(this->block_size);
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        if (read(fd, data, size) != (ssize_t)size)
            DRIVE_FATAL("Could not read file %s\n", fn);
        close(fd);
        return data;
    }

    // Read the block into a chunk of temporary memory, and decompress
    data = malloc(this->block_size);
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    readbuf = malloc(size);
    if (read(fd, readbuf, size) != (ssize_t)size)
        DRIVE_FATAL("Could not read file %s\n", fn);

    z_stream inflate_stream = { 0 };
    inflate_stream.zalloc = Z_NULL;
    inflate_stream.zfree = Z_NULL;
    inflate_stream.opaque = Z_NULL;
    inflate_stream.avail_in = inflate_stream.total_in = size;
    inflate_stream.next_in = readbuf;
    inflate_stream.avail_out = inflate_stream.total_out = this->block_size;
    inflate_stream.next_out = data;
    int err = inflateInit(&inflate_stream);
    if (err == Z_OK) {
        err = inflate(&inflate_stream, Z_FINISH);
        if (err != Z_STREAM_END) {
            inflateEnd(&inflate_stream);
            DRIVE_FATAL("Unable to inflate %s\n", temp);
        }
    } else {
        inflateEnd(&inflate_stream);
        DRIVE_FATAL("Unable to inflate %s\n", temp);
    }
    free(readbuf);
    close(fd);
    return data;
}
#endif

// Read data from remote source (i.e. file, web server, etc.)
static int drive_internal_read_remote(struct drive_internal_info* this, struct block_info* blockinfo, uint8_t* buffer, uint32_t pos, uint32_t length)
{
    char temp[1024];
    uint32_t block = pos / this->block_size;
    drive_get_path(temp, this->paths[blockinfo->pathindex], block);
#ifdef EMSCRIPTEN
    // Mark the block cache entry as valid
    blockinfo->data = (uint8_t*)(1);
    UNUSED(buffer);
    UNUSED(length);
    return EM_ASM_INT({
        /* str, id */
        return window["drives"][$0]["readQueue"]($1, $2);
    },
        this->drive_id, temp, block);
#else
    // Open the file, allocate memory, read file, and close file.
    blockinfo->data = drive_read_file(this, temp);

// If we want to run in sync mode, then copy the data
#ifndef SIMULATE_ASYNC_ACCESS
    memcpy(buffer, blockinfo->data + (pos % this->block_size), length);
#else
    UNUSED(buffer);
    UNUSED(length);
#endif

    return 0;
#endif
}

// This function loads blocks from the cache. Returns 0 if all blocks were read from the cache.
static int drive_internal_read_check(struct drive_internal_info* this, void* buffer, uint32_t length, drv_offset_t position, int no_xhr)
{
    uint32_t readEnd = position + length,
             blocksToRead = ((((readEnd - 1) & ~BLOCK_MASK) - (position & ~BLOCK_MASK)) >> BLOCK_SHIFT) + 1;

    uint32_t currentFilePosition = position;

    int retval = 0;

    for (unsigned int i = 0; i < blocksToRead; i++) {
        struct block_info* blockInformation = &this->blocks[currentFilePosition >> BLOCK_SHIFT];
        uint32_t begin = 0,
                 end = BLOCK_SIZE,
                 len = 0;
        if (currentFilePosition & BLOCK_MASK) {
            begin = currentFilePosition & BLOCK_MASK;
        }
        if ((currentFilePosition ^ readEnd) < BLOCK_SIZE) { // Check if the end is on the same block
            end = readEnd & BLOCK_MASK;
            if (end == 0)
                end = BLOCK_SIZE;
        }
#if 0 && defined(EMSCRIPTEN)
        blockInformation->data = (void*)1;
#endif
        len = end - begin;
        //printf("BlockInformation: %p Data: %p cfp=%d\n", blockInformation, blockInformation->data, currentFilePosition / 512);
        if (blockInformation->data) {
            retval |= drive_read_block_internal(this, blockInformation, buffer, len, currentFilePosition);
        } else {
            if (!no_xhr)
                if (drive_internal_read_remote(this, blockInformation, buffer, currentFilePosition, len) < 0)
                    DRIVE_FATAL("Unable to load disk\n");
#ifdef SIMULATE_ASYNC_ACCESS // If we aren't simulating async, then don't set retval
            retval |= 1;
#endif
        }

        buffer += len;
        currentFilePosition += len;
    }
    return retval;
}

#ifdef SIMULATE_ASYNC_ACCESS
static void drive_internal_read_cb(void* this_ptr, int status)
{
    if (!transfer_in_progress)
        return;
    struct drive_internal_info* this = this_ptr;
    // Make sure everything is loaded
    if (drive_internal_read_check(this, this->argument_buffer, this->argument_length, this->argument_position, 1))
        DRIVE_FATAL("We haven't loaded everything..?\n");
    this->callback(this->ide_callback_arg1, status);
}
#endif

// Reads data from drives
static int drive_internal_read(void* this_ptr, void* cb_ptr, void* buffer, uint32_t length, drv_offset_t position, drive_cb cb)
{
    struct drive_internal_info* this = this_ptr;
    if (!drive_internal_read_check(this, buffer, length, position, 0))
        return DRIVE_RESULT_SYNC;

    // Store information
    this->argument_buffer = buffer;
    this->argument_length = length;
    this->argument_position = position;
    this->callback = cb;
    this->ide_callback_arg1 = cb_ptr;
    transfer_in_progress = 1;
#ifdef EMSCRIPTEN
    EM_ASM_({
        window["drives"][$0]["flushReadQueue"]($1, $2);
    },
        this->drive_id, drive_internal_read_cb, this);
#elif defined(SIMULATE_ASYNC_ACCESS)
    global_cb = drive_internal_read_cb;
    global_cb_arg1 = this;
#endif

    return DRIVE_RESULT_ASYNC;
}

// ============================================================================
// Write functions
// ============================================================================

// Read data from remote source (i.e. file, web server, etc.)
static int drive_internal_write_remote(struct drive_internal_info* this, struct block_info* blockinfo, uint8_t* buffer, uint32_t pos, drv_offset_t length)
{
    char temp[1024];
    uint32_t block = pos / this->block_size;
    drive_get_path(temp, this->paths[blockinfo->pathindex], block);
#ifdef EMSCRIPTEN
    // We have to read the block in order for it to be valid
    blockinfo->data = (uint8_t*)(1);
    UNUSED(buffer);
    UNUSED(length);
    return EM_ASM_INT({
        /* str, id */
        return window["drives"][$0]["readQueue"]($1, $2);
    },
        this->drive_id, temp, block);
#else
    // Open the file, allocate memory, read file, and close file.
    blockinfo->data = drive_read_file(this, temp);

// If we want to run in sync mode, then copy the data
#ifndef SIMULATE_ASYNC_ACCESS
    memcpy(blockinfo->data + (pos % this->block_size), buffer, length);
#else
    UNUSED(buffer);
    UNUSED(length);
#endif

    return 0;
#endif
}

// Reads block information from a file.
static int drive_write_block_internal(struct drive_internal_info* this, struct block_info* info, void* buffer, uint32_t length, drv_offset_t position)
{
    info->modified = 1;
    uint32_t blockoffs = position % this->block_size;
#ifdef EMSCRIPTEN
    UNUSED(info);
    return EM_ASM_INT({
        /* id, buffer, offset, length */
        return window["drives"][$0]["writeCache"]($1, $2, $3, $4);
    },
        this->drive_id, position / this->block_size, buffer, blockoffs, length);
#else
    memcpy(info->data + blockoffs, buffer, length);
    return 0;
#endif
}
// This function loads blocks from the cache. Returns 0 if all blocks were read from the cache.
static int drive_internal_write_check(struct drive_internal_info* this, void* buffer, uint32_t length, drv_offset_t position, int no_xhr)
{
    drv_offset_t writeEnd = position + length,
                 blocksToWrite = ((((writeEnd - 1) & ~BLOCK_MASK) - (position & ~BLOCK_MASK)) >> BLOCK_SHIFT) + 1;

    uint32_t currentFilePosition = position;

    int retval = 0;

    for (unsigned int i = 0; i < blocksToWrite; i++) {
        struct block_info* blockInformation = &this->blocks[currentFilePosition >> BLOCK_SHIFT];
        uint32_t begin = 0,
                 end = BLOCK_SIZE,
                 len = 0;
        if (currentFilePosition & BLOCK_MASK) {
            begin = currentFilePosition & BLOCK_MASK;
        }
        if ((currentFilePosition ^ writeEnd) < BLOCK_SIZE) { // Check if the end is on the same block
            end = writeEnd & BLOCK_MASK;
            if (end == 0)
                end = BLOCK_SIZE;
        }
        blockInformation->modified = 1;
        len = end - begin;
        //printf("BlockInformation: %p Data: %p cfp=%d\n", blockInformation, blockInformation->data, currentFilePosition / 512);
        if (blockInformation->data)
            retval |= drive_write_block_internal(this, blockInformation, buffer, len, currentFilePosition);
        else {
            if (!no_xhr)
                if (drive_internal_write_remote(this, blockInformation, buffer, currentFilePosition, len) < 0)
                    DRIVE_FATAL("Unable to load disk\n");
#ifdef SIMULATE_ASYNC_ACCESS // If we aren't simulating async, then don't set retval
            retval |= 1;
#endif
        }

        buffer += len;
        currentFilePosition += len;
    }
    return retval;
}

#ifdef SIMULATE_ASYNC_ACCESS
static void drive_internal_write_cb(void* this_ptr, int status)
{
    if (!transfer_in_progress)
        return;
    struct drive_internal_info* this = this_ptr;
    if (drive_internal_write_check(this, this->argument_buffer, this->argument_length, this->argument_position, 1))
        DRIVE_FATAL("We haven't loaded everything..?\n");
    this->callback(this->ide_callback_arg1, status);
}
#endif

// Reads data from drives
static int drive_internal_write(void* this_ptr, void* cb_ptr, void* buffer, uint32_t length, drv_offset_t position, drive_cb cb)
{
    struct drive_internal_info* this = this_ptr;
    if (!drive_internal_write_check(this, buffer, length, position, 0))
        return DRIVE_RESULT_SYNC;

    // Store information
    this->argument_buffer = buffer;
    this->argument_length = length;
    this->argument_position = position;
    this->callback = cb;
    this->ide_callback_arg1 = cb_ptr;
    transfer_in_progress = 1;
#ifdef EMSCRIPTEN
    EM_ASM_({
        window["drives"][$0]["flushReadQueue"]($1, $2);
    },
        this->drive_id, drive_internal_write_cb, this);
#elif defined(SIMULATE_ASYNC_ACCESS)
    global_cb = drive_internal_write_cb;
    global_cb_arg1 = this;
#endif

    return DRIVE_RESULT_ASYNC;
}

// ============================================================================
// Prefetch functions
// ============================================================================

#ifdef SIMULATE_ASYNC_ACCESS
static void drive_internal_prefetch_cb(void* this_ptr, int status)
{
    if (!transfer_in_progress)
        return;
    struct drive_internal_info* this = this_ptr;
    this->callback(this->ide_callback_arg1, status);
}
#endif

// Read data from remote source (i.e. file, web server, etc.)
static int drive_internal_prefetch_remote(struct drive_internal_info* this, struct block_info* blockinfo, uint32_t pos, drv_offset_t length)
{
    char temp[1024];
    uint32_t block = pos / this->block_size;
    drive_get_path(temp, this->paths[blockinfo->pathindex], block);
#ifdef EMSCRIPTEN
    // Mark the block cache entry as valid
    blockinfo->data = (uint8_t*)(1);
    UNUSED(length);
    return EM_ASM_INT({
        /* str, id */
        return window["drives"][$0]["readQueue"]($1, $2);
    },
        this->drive_id, temp, block);
#else
    // Open the file, allocate memory, read file, and close file.
    blockinfo->data = drive_read_file(this, temp);

    UNUSED(length);

    return 0;
#endif
}
// This function prefetches information from the buffer, but does not load anything.
// Returns 0 if all blocks were loaded from the cache
static int drive_internal_prefetch_check(struct drive_internal_info* this, uint32_t length, drv_offset_t position)
{
    drv_offset_t readEnd = position + length,
                 blocksToRead = ((((readEnd - 1) & ~BLOCK_MASK) - (position & ~BLOCK_MASK)) >> BLOCK_SHIFT) + 1;

    uint32_t currentFilePosition = position;

    int retval = 0;
    for (unsigned int i = 0; i < blocksToRead; i++) {
        struct block_info* blockInformation = &this->blocks[currentFilePosition >> BLOCK_SHIFT];
        uint32_t begin = 0,
                 end = BLOCK_SIZE,
                 len = 0;
        if (currentFilePosition & BLOCK_MASK) {
            begin = currentFilePosition & BLOCK_MASK;
        }
        if ((currentFilePosition ^ readEnd) < BLOCK_SIZE) { // Check if the end is on the same block
            end = readEnd & BLOCK_MASK;
            if (end == 0)
                end = BLOCK_SIZE;
        }
        len = end - begin;
        if (blockInformation->data) {
            // Nothing happens here -- we just pretend to read data
        } else {
            if (drive_internal_prefetch_remote(this, blockInformation, currentFilePosition, len) < 0)
                DRIVE_FATAL("Unable to load disk\n");
#ifdef SIMULATE_ASYNC_ACCESS // If we aren't simulating async, then don't set retval
            retval |= 1;
#endif
        }

        currentFilePosition += len;
    }
    return retval;
}
static int drive_internal_prefetch(void* this_ptr, void* cb_ptr, uint32_t length, drv_offset_t position, drive_cb cb)
{
    if (position > 0xFFFFFFFF)
        DRIVE_FATAL("TODO: big access\n");
    struct drive_internal_info* this = this_ptr;
    if (!drive_internal_prefetch_check(this, length, position))
        return DRIVE_RESULT_SYNC;

    // Store information
    this->argument_length = length;
    this->argument_position = position;
    this->callback = cb;
    this->ide_callback_arg1 = cb_ptr;
    transfer_in_progress = 1;
#ifdef EMSCRIPTEN
    EM_ASM_({
        window["drives"][$0]["flushReadQueue"]($1, $2);
    },
        this->drive_id, drive_internal_prefetch_cb, this);
#elif defined(SIMULATE_ASYNC_ACCESS)
    global_cb = drive_internal_prefetch_cb;
    global_cb_arg1 = this;
#endif

    return DRIVE_RESULT_ASYNC;
}

static void drive_internal_state(void* this_ptr, char* pn)
{
    char temp[100];
    struct drive_internal_info* this = this_ptr;
    struct bjson_object* obj = state_obj(pn, 4 + this->path_count + 1);

    state_field(obj, 4, "size", &this->size);
    state_field(obj, 4, "path_count", &this->path_count);
    state_field(obj, 4, "block_count", &this->block_count);
    uint32_t* block_infos = alloca(this->block_count * 4);

    if (state_is_reading()) {
        int old_path_counts = this->path_count; // The current state file may have paths not in our current
        state_field(obj, 4, "path_count", &this->path_count);
        // Destroy all paths and load in our new paths
        for (int i = 0; i < old_path_counts; i++)
            free(this->paths[i]);
        this->paths = realloc(this->paths, this->path_count * sizeof(char*));
        int paths0 = 0;
        for (unsigned int i = 0; i < this->path_count; i++) {
            sprintf(temp, "path%d", i);
            state_string(obj, temp, &this->paths[i]);
            printf("%s\n", this->paths[i]);
            paths0++;
        }

        // Destroy all blocks
        state_field(obj, this->block_count * 4, "block_array", block_infos);
        for (unsigned int i = 0; i < this->block_count; i++) {
            if (this->blocks[i].data)
                free(this->blocks[i].data);
            this->blocks[i].data = NULL;
            this->blocks[i].modified = 0;
            this->blocks[i].pathindex = block_infos[i];
        }
    } else {
        char pathname[1000];
        sprintf(pathname, "%s/%s", state_get_path_base(), pn);
        state_mkdir(pathname);

        uint32_t path_count = this->path_count;
        int existing_add = 1;
        for (unsigned int i = 0; i < this->path_count; i++) {
            printf("%s %s\n", pathname, this->paths[i]);
            if (!strcmp(pathname, this->paths[i])) {
                existing_add = 0;
                break;
            }
        }
        path_count += existing_add;
        state_field(obj, 4, "path_count", &path_count);

        int pwdindex = 0, pwdinc = 1;
        for (unsigned int i = 0; i < this->path_count; i++) {
            if (!strcmp(pathname, this->paths[i]))
                pwdinc = 0; // Stop counting
            pwdindex += pwdinc;
            sprintf(temp, "path%d", i);
            state_string(obj, temp, &this->paths[i]);
        }
        if (pwdinc) {
            // Add new path to list
            sprintf(temp, "path%d", pwdindex);
            char* foo_bad = pathname; // XXX hack
            state_string(obj, temp, &foo_bad);
        }

        for (unsigned int i = 0; i < this->block_count; i++) {
            if (this->blocks[i].modified) {
                sprintf(pathname, "%s/blk%08x.bin", pn, i);
                state_file(BLOCK_SIZE, pathname, this->blocks[i].data);
                this->blocks[i].pathindex = pwdindex;
            }
            block_infos[i] = this->blocks[i].pathindex;
        }
        state_field(obj, this->block_count * 4, "block_array", block_infos);
    }
}

// This is a drive type that can be used for both native and Emscripten builds.
#ifndef EMSCRIPTEN // Browser version will call from emscripten.c
static
#endif
    int
    drive_internal_init(struct drive_info* info, char* filename, void* info_dat, int drvid)
{
    struct drive_internal_info* drv = malloc(sizeof(struct drive_internal_info));

    int len = strlen(filename);
    void* pathbase = malloc(len + 1);
    strcpy(pathbase, filename);

    drv->path_count = 1;
    drv->paths = malloc(sizeof(char*));
    drv->paths[0] = pathbase;
#ifdef EMSCRIPTEN
    drv->drive_id = drvid;
#else
    UNUSED(drvid);
#endif

    // Parse
    struct drive_info_file* internal = info_dat;
    drv->block_size = internal->block_size;
    drv->size = internal->size;
    drv->block_count = (internal->block_size + internal->size - 1) / internal->block_size;
    drv->blocks = calloc(sizeof(struct block_info), drv->block_count);

    info->data = drv;
    info->read = drive_internal_read;
    info->write = drive_internal_write;
    info->state = drive_internal_state;
    info->prefetch = drive_internal_prefetch;

    // Now determine drive geometry
    info->sectors = internal->size / 512;
    info->sectors_per_cylinder = 63;
    info->heads = 16;
    info->cylinders_per_head = info->sectors / (info->sectors_per_cylinder * info->heads);

    return 0;
#ifdef EMSCRIPTEN
#else
#endif
}

void drive_check_complete(void)
{
#if !defined(EMSCRIPTEN) && defined(SIMULATE_ASYNC_ACCESS)
    if (transfer_in_progress) {
        global_cb(global_cb_arg1, 0);
        transfer_in_progress = 0;
    }
#endif
}

int drive_async_event_in_progress(void)
{
#ifdef SIMULATE_ASYNC_ACCESS
    return transfer_in_progress;
#endif
}

#ifndef EMSCRIPTEN
#if 0
function join_path(a, b) {
    if (b.charAt(0) !== "/") 
        b = "/" + b;
    if (a.charAt(a.length - 1) === "/") 
        a = a.substring(0, a.length - 1);
    return normalize_path(a + b);
}
#endif
int drive_init(struct drive_info* info, char* filename)
{
    char buf[1024];
    int filelen = strlen(filename);
    if (filelen > 1000)
        return -1;
    join_path(buf, filelen, filename, "info.dat");

    int fd = open(buf, O_RDONLY | O_BINARY);
    if (fd == -1)
        return -1;
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    void* data = malloc(size);
    if (read(fd, data, size) != size)
        return -1;
    close(fd);

    drive_internal_init(info, filename, data, -1);
    free(data);
    return 0;
}
#endif
void drive_state(struct drive_info* info, char* filename)
{
    info->state(info->data, filename);
}

#ifndef EMSCRIPTEN

// Simple driver

struct simple_driver {
    int fd; // File descriptor of image file

    // Size of image file (in bytes) and size of each block (in bytes)
    drv_offset_t image_size, block_size;

    // Size of block array, in terms of uint8_t*
    uint32_t block_array_size;

    // Set if disk is write protected
    int write_protected;

    // Should we modify the backing file?
    int raw_file_access;

    // Table of blocks
    uint8_t** blocks;
};

static void drive_simple_state(void* this, char* path)
{
    UNUSED(this);
    UNUSED(path);
    //DRIVE_FATAL("TODO: Sync driver state\n");
}

static int drive_simple_prefetch(void* this_ptr, void* cb_ptr, uint32_t length, drv_offset_t position, drive_cb cb)
{
    // Since we're always sync, no need to prefetch
    UNUSED(this_ptr);
    UNUSED(cb_ptr);
    UNUSED(length);
    UNUSED(position);
    UNUSED(cb);
    return DRIVE_RESULT_SYNC;
}

static inline int drive_simple_has_cache(struct simple_driver* info, drv_offset_t offset)
{
    return info->blocks[offset / info->block_size] != NULL;
}

// Reads 512 bytes of data from the cache, if possible.
static int drive_simple_fetch_cache(struct simple_driver* info, void* buffer, drv_offset_t offset)
{
    if (offset & 511)
        DRIVE_FATAL("Offset not aligned to 512 byte boundary");
    uint32_t blockid = offset / info->block_size;

    // Check if block cache is open
    if (info->blocks[blockid]) {
        // Get the offset inside the block, get the physical position of the block, and copy 512 bytes into the destination buffer
        uint32_t block_offset = offset % info->block_size;
        void* ptr = info->blocks[blockid] + block_offset;
        memcpy(buffer, ptr, 512); // Copy all the bytes from offset to the end of the file
        return 1;
    }

    return 0;
}

static int drive_simple_add_cache(struct simple_driver* info, drv_offset_t offset)
{
    void* dest = info->blocks[offset / info->block_size] = malloc(info->block_size);
    lseek(info->fd, offset & (drv_offset_t) ~(info->block_size - 1), SEEK_SET); // Seek to the beginning of the current block
    if ((uint32_t)read(info->fd, dest, info->block_size) != info->block_size)
        DRIVE_FATAL("Unable to read %d bytes from image file\n", (int)info->block_size);
    return 0;
}

static inline int drive_simple_write_cache(struct simple_driver* info, void* buffer, drv_offset_t offset)
{
    uint32_t block_offset = offset % info->block_size;
    void* ptr = info->blocks[offset / info->block_size] + block_offset;
    memcpy(ptr, buffer, 512); // Copy all the bytes from offset to the end of the file
    return 0;
}

//#define ALLOW_READWRITE 1

static int drive_simple_write(void* this, void* cb_ptr, void* buffer, uint32_t size, drv_offset_t offset, drive_cb cb)
{
    UNUSED(this);
    UNUSED(cb);
    UNUSED(cb_ptr);

    if ((size | offset) & 511)
        DRIVE_FATAL("Length/offset must be multiple of 512 bytes\n");

    struct simple_driver* info = this;

    drv_offset_t end = size + offset;
    while (offset != end) {
        if (!info->raw_file_access) {
            if (!drive_simple_has_cache(info, offset))
                drive_simple_add_cache(info, offset);
            drive_simple_write_cache(info, buffer, offset);
        } else {
            UNUSED(drive_simple_add_cache);
            lseek(info->fd, offset, SEEK_SET);
            if (write(info->fd, buffer, 512) != 512)
                DRIVE_FATAL("Unable to write 512 bytes to image file\n");
        }
        buffer += 512;
        offset += 512;
    }
    return DRIVE_RESULT_SYNC;
}

static int drive_simple_read(void* this, void* cb_ptr, void* buffer, uint32_t size, drv_offset_t offset, drive_cb cb)
{
    UNUSED(this);
    UNUSED(cb);
    UNUSED(cb_ptr);

    if ((size | offset) & 511)
        DRIVE_FATAL("Length/offset must be multiple of 512 bytes\n");

    struct simple_driver* info = this;

    drv_offset_t end = size + offset;
    while (offset != end) {
        if (!drive_simple_fetch_cache(info, buffer, offset)) {
            lseek(info->fd, offset, SEEK_SET);
            if (read(info->fd, buffer, 512) != 512)
                DRIVE_FATAL("Unable to read 512 bytes from image file\n");
        }
        buffer += 512;
        offset += 512;
    }
    return DRIVE_RESULT_SYNC;
}

int drive_simple_init(struct drive_info* info, char* filename)
{
    int fd;
    if (!info->modify_backing_file)
        fd = open(filename, O_RDONLY | O_BINARY);
    else
        fd = open(filename, O_RDWR | O_BINARY);
    if (fd < 0)
        return -1;

    uint64_t size = lseek(fd, 0, SEEK_END);
    if (size == (uint64_t)-1)
        return -1;
    lseek(fd, 0, SEEK_SET);

    struct simple_driver* sync_info = malloc(sizeof(struct simple_driver));
    info->data = sync_info;
    sync_info->fd = fd;
    sync_info->image_size = size;
    sync_info->block_size = BLOCK_SIZE;
    sync_info->block_array_size = (size + sync_info->block_size - 1) / sync_info->block_size;
    sync_info->blocks = calloc(sizeof(uint8_t*), sync_info->block_array_size);

    sync_info->raw_file_access = info->modify_backing_file;

    info->read = drive_simple_read;
    info->state = drive_simple_state;
    info->write = drive_simple_write;
    info->prefetch = drive_simple_prefetch;

    // Now determine drive geometry
    info->sectors = size / 512;
    info->sectors_per_cylinder = 63;
    info->heads = 16;
    info->cylinders_per_head = info->sectors / (info->sectors_per_cylinder * info->heads);

    return 0;
}

void drive_destroy_simple(struct drive_info* info)
{
    struct simple_driver* simple_info = info->data;
    for (unsigned int i = 0; i < simple_info->block_array_size; i++)
        free(simple_info->blocks[i]);
    free(simple_info->blocks);
    free(simple_info);
}

#ifndef EMSCRIPTEN
// Autodetect drive type
int drive_autodetect_type(char* path)
{
    struct stat statbuf;

    // Check for URL
    if (strstr(path, "http://") != NULL || strstr(path, "https://") != NULL)
        return 2;
    
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if(fstat(fd, &statbuf)){
        close(fd);
        return -1;
    }
    close(fd);
    if(S_ISDIR(statbuf.st_mode))
        return 0; // Chunked file 
    else
        return 1; // Raw image file 
    
}
#endif
#endif