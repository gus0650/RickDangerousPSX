/*
 * xrick/system/sysmem_psx.c -- PlayStation: stack-style memory allocator
 *
 * Copyright (C) 2008-2014 Pierluigi Vicinanza.
 * PlayStation port 2026.
 * All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include "xrick/system/system.h"
#include "xrick/debug.h"

enum
{
    ALIGNMENT = 8,
    POOL_SIZE = 640 * 1024
};

static U8 pool[POOL_SIZE] __attribute__((aligned(8)));
static U8 * stackTop;
static size_t stackSize;
static size_t maxUsed;

bool sysmem_init(void)
{
    stackTop = pool;
    stackSize = 0;
    maxUsed = 0;
    return true;
}

void sysmem_shutdown(void)
{
    sys_printf("xrick/memory: max usage %u of %u bytes\n",
               (unsigned)maxUsed, (unsigned)POOL_SIZE);
}

void *sysmem_push(size_t size)
{
    uintptr_t alignedPtr;
    size_t neededSize = sizeof(size_t) + size + ALIGNMENT;

    if (stackSize + neededSize > POOL_SIZE)
    {
        sys_error("(memory) out of memory: %u bytes requested, %u used",
                  (unsigned)size, (unsigned)stackSize);
        return NULL;
    }

    alignedPtr = (((uintptr_t)stackTop) + sizeof(size_t) + ALIGNMENT) & ~((uintptr_t)(ALIGNMENT - 1));
    ((size_t *)alignedPtr)[-1] = neededSize;

    stackTop += neededSize;
    stackSize += neededSize;
    if (stackSize > maxUsed)
    {
        maxUsed = stackSize;
    }
    return (void *)alignedPtr;
}

void sysmem_pop(void * alignedPtr)
{
    size_t allocatedSize;

    if (!alignedPtr)
    {
        return;
    }
    if (stackSize == 0)
    {
        sys_error("(memory) tried to release a block when memory empty");
        return;
    }
    allocatedSize = ((size_t *)(alignedPtr))[-1];
    stackTop -= allocatedSize;
    stackSize -= allocatedSize;
}

/* eof */
