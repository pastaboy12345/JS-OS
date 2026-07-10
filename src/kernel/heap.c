#include <jsos/heap.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define HEAP_ALIGNMENT 16ULL
#define HEAP_MAGIC 0x4a534f53U

typedef struct HeapBlock HeapBlock;

struct HeapBlock {
    size_t size;
    HeapBlock *previous;
    HeapBlock *next;
    uint32_t magic;
    bool free;
};

static uint8_t *heap_begin;
static uint8_t *heap_end;
static HeapBlock *first_block;
static size_t allocation_count;

static size_t align_up(size_t value) {
    if (value > SIZE_MAX - (HEAP_ALIGNMENT - 1)) {
        return 0;
    }
    return (value + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
}

static size_t header_size(void) {
    return (sizeof(HeapBlock) + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
}

static void *block_payload(HeapBlock *block) {
    return (uint8_t *)block + header_size();
}

static HeapBlock *payload_block(const void *pointer) {
    if (pointer == NULL || heap_begin == NULL) {
        return NULL;
    }
    const uint8_t *bytes = pointer;
    if (bytes < heap_begin + header_size() || bytes >= heap_end) {
        return NULL;
    }
    HeapBlock *block = (HeapBlock *)(bytes - header_size());
    if ((uint8_t *)block < heap_begin || block->magic != HEAP_MAGIC) {
        return NULL;
    }
    return block;
}

int heap_init(void *memory, size_t size) {
    uintptr_t raw = (uintptr_t)memory;
    uintptr_t aligned = (raw + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
    size_t adjustment = aligned - raw;
    if (size <= adjustment + header_size() + HEAP_ALIGNMENT) {
        return -1;
    }
    size -= adjustment;
    size &= ~(HEAP_ALIGNMENT - 1);
    heap_begin = (uint8_t *)aligned;
    heap_end = heap_begin + size;
    first_block = (HeapBlock *)heap_begin;
    first_block->size = size - header_size();
    first_block->previous = NULL;
    first_block->next = NULL;
    first_block->magic = HEAP_MAGIC;
    first_block->free = true;
    allocation_count = 0;
    return 0;
}

static void split_block(HeapBlock *block, size_t size) {
    if (block->size < size + header_size() + HEAP_ALIGNMENT) {
        return;
    }
    HeapBlock *remainder = (HeapBlock *)((uint8_t *)block_payload(block) + size);
    remainder->size = block->size - size - header_size();
    remainder->previous = block;
    remainder->next = block->next;
    remainder->magic = HEAP_MAGIC;
    remainder->free = true;
    if (remainder->next != NULL) {
        remainder->next->previous = remainder;
    }
    block->next = remainder;
    block->size = size;
}

void *heap_allocate(size_t size) {
    if (size == 0 || first_block == NULL) {
        return NULL;
    }
    size = align_up(size);
    if (size == 0) {
        return NULL;
    }
    for (HeapBlock *block = first_block; block != NULL; block = block->next) {
        if (!block->free || block->size < size) {
            continue;
        }
        split_block(block, size);
        block->free = false;
        allocation_count++;
        return block_payload(block);
    }
    return NULL;
}

void *heap_callocate(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        return NULL;
    }
    size_t total = count * size;
    void *pointer = heap_allocate(total);
    if (pointer != NULL) {
        memset(pointer, 0, total);
    }
    return pointer;
}

static void merge_with_next(HeapBlock *block) {
    HeapBlock *next = block->next;
    if (next == NULL || !next->free || next->magic != HEAP_MAGIC) {
        return;
    }
    block->size += header_size() + next->size;
    block->next = next->next;
    if (block->next != NULL) {
        block->next->previous = block;
    }
    next->magic = 0;
}

void heap_release(void *pointer) {
    HeapBlock *block = payload_block(pointer);
    if (block == NULL || block->free) {
        return;
    }
    block->free = true;
    if (allocation_count != 0) {
        allocation_count--;
    }
    merge_with_next(block);
    if (block->previous != NULL && block->previous->free) {
        block = block->previous;
        merge_with_next(block);
    }
}

void *heap_reallocate(void *pointer, size_t size) {
    if (pointer == NULL) {
        return heap_allocate(size);
    }
    if (size == 0) {
        heap_release(pointer);
        return NULL;
    }
    HeapBlock *block = payload_block(pointer);
    if (block == NULL || block->free) {
        return NULL;
    }
    size_t aligned_size = align_up(size);
    if (aligned_size == 0) {
        return NULL;
    }
    if (block->size >= aligned_size) {
        split_block(block, aligned_size);
        return pointer;
    }
    if (block->next != NULL && block->next->free &&
            block->size + header_size() + block->next->size >= aligned_size) {
        merge_with_next(block);
        split_block(block, aligned_size);
        return pointer;
    }
    void *replacement = heap_allocate(size);
    if (replacement == NULL) {
        return NULL;
    }
    memcpy(replacement, pointer, block->size < size ? block->size : size);
    heap_release(pointer);
    return replacement;
}

size_t heap_usable_size(const void *pointer) {
    HeapBlock *block = payload_block(pointer);
    return block == NULL || block->free ? 0 : block->size;
}

HeapStats heap_stats(void) {
    HeapStats stats = {0};
    if (heap_begin == NULL) {
        return stats;
    }
    stats.capacity = (size_t)(heap_end - heap_begin);
    stats.allocations = allocation_count;
    for (HeapBlock *block = first_block; block != NULL; block = block->next) {
        if (block->free) {
            stats.free += block->size;
            if (block->size > stats.largest_free) {
                stats.largest_free = block->size;
            }
        } else {
            stats.used += block->size;
        }
    }
    return stats;
}
