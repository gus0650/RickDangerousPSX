/*
 * xrick/system/sysfile_psx.c -- PlayStation: read-only files embedded in the executable
 *
 * The data files are packed by tools/mkassets.py into "romfs.bin", which is
 * linked into the executable. Layout (little endian):
 *   "XRFS" u32 count, then count x { char name[48]; u32 offset; u32 size; }
 * Offsets are from the start of the blob.
 *
 * PlayStation port 2026.
 */

#include "xrick/system/system.h"
#include "xrick/config.h"

#include <string.h>

extern const U8 romfs[];
extern const size_t romfs_size;

enum { NAME_LEN = 48, MAX_OPEN = 4 };

typedef struct
{
    char name[NAME_LEN];
    U32 offset;
    U32 size;
} romfs_entry_t;

typedef struct
{
    const U8 *data;
    U32 size;
    U32 pos;
    bool used;
} romfile_t;

static romfile_t files[MAX_OPEN];

const char *sysfile_defaultPath = "";

bool sysfile_setRootPath(const char *name)
{
    (void)name;
    if (memcmp(romfs, "XRFS", 4) != 0)
    {
        sys_error("(file) bad romfs");
        return false;
    }
    return true;
}

void sysfile_clearRootPath(void)
{
}

static U32 rd32(const U8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((U32)p[3] << 24);
}

file_t sysfile_open(const char *name)
{
    U32 count = rd32(romfs + 4);
    U32 i;
    int f;

    for (f = 0; f < MAX_OPEN && files[f].used; f++) {}
    if (f == MAX_OPEN)
    {
        sys_error("(file) too many open files");
        return NULL;
    }

    for (i = 0; i < count; i++)
    {
        const U8 *e = romfs + 8 + i * (NAME_LEN + 8);
        if (strcmp((const char *)e, name) == 0)
        {
            files[f].data = romfs + rd32(e + NAME_LEN);
            files[f].size = rd32(e + NAME_LEN + 4);
            files[f].pos = 0;
            files[f].used = true;
            return &files[f];
        }
    }
    return NULL;
}

int sysfile_read(file_t file, void *buf, size_t size, size_t count)
{
    romfile_t *f = (romfile_t *)file;
    size_t avail, n;

    if (!f || size == 0)
    {
        return 0;
    }
    avail = (f->size - f->pos) / size;
    n = count < avail ? count : avail;
    memcpy(buf, f->data + f->pos, n * size);
    f->pos += n * size;
    return (int)n;
}

int sysfile_seek(file_t file, long offset, int origin)
{
    romfile_t *f = (romfile_t *)file;
    long pos;

    switch (origin)
    {
        case 0: pos = offset; break;                     /* SEEK_SET */
        case 1: pos = (long)f->pos + offset; break;      /* SEEK_CUR */
        default: pos = (long)f->size + offset; break;    /* SEEK_END */
    }
    if (pos < 0 || pos > (long)f->size)
    {
        return -1;
    }
    f->pos = (U32)pos;
    return 0;
}

int sysfile_tell(file_t file)
{
    return (int)((romfile_t *)file)->pos;
}

off_t sysfile_size(file_t file)
{
    return (off_t)((romfile_t *)file)->size;
}

void sysfile_close(file_t file)
{
    if (file)
    {
        ((romfile_t *)file)->used = false;
    }
}

/* eof */
