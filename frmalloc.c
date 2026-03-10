/*
 * frmalloc - explicit free list allocator 
 * 
 * Blocks store size and 2 flag bits - 0x1 allocated flag, 0x2 prev_allocated flag 
 * 
 * Free blocks store next/prev pointers in the payload area, and 
 * a footer for faster coalescing  
 *
 * Allocated blocks have no footer and next/prev pointers 
 */

#include "frmalloc.h"
#include <stddef.h>
#include <sys/mman.h>
#include <stdio.h>

/*
 *  size_t size - size of a block
 *  since the block size is always 16 byte aligned the first 4 bits are free
 *  so 2 lowest bits are used as flags:
 *    1 bit (0x01) - allocated 
 *    2 bit (0x02) - previous allocated
 *  size_t padding - keeps the struct 16 byte aligned
 */
typedef struct block {
  size_t size;
  size_t padding;
} block_t;

/*
 * free block metadata
 * this struct stores pointers to the next and previous free block.
 * when a block is allocated, this struct is overwritten by payload, as well as footer.  
 */
typedef struct free_meta{
  block_t *next;
  block_t *prev; 
} free_meta_t; 

#define HEADER_SIZE sizeof(block_t) 
#define MMAP_INIT_SIZE 4*1024*1024 // mmap 4 MB
#define ALIGN(size) ((size + 15) & ~15) // align the size to a multiple of 16
#define MIN_FREE_BLOCK_SIZE ALIGN(HEADER_SIZE + 2*sizeof(void *) + sizeof(size_t)) /* minimum size of a free block consists of:
                                                                                      -header (16 bytes)
                                                                                      -metadata (free_meta struct, 16 bytes) 
                                                                                      -footer (holds size, 8 bytes)
                                                                                         = 48 bytes (aligned) */ 
/* Helper functions */ 
#define GET_SIZE(block) ((block)->size & ~0xF)
#define IS_ALLOC(block) ((block)->size & 0x1)
#define IS_PREV_ALLOC(block) ((block)->size & 0x2)
#define SET_ALLOC(block) ((block)->size |= 0x1)
#define SET_PREV_ALLOC(block) ((block)->size |= 0x2)
#define SET_FREE(block) ((block)->size &= ~0x1)
#define SET_PREV_FREE(block) ((block)->size &= ~0x2)
#define GET_FOOTER(block) \
  ((size_t *)((char *)(block) + GET_SIZE(block) - sizeof(size_t))) 
#define GET_PREV_SIZE(block) \
  ((*((size_t *)((char *)(block) - sizeof(size_t)))) & ~0xF) 
#define GET_NEXT_BLOCK(block) \
  ((block_t *)((char *)(block) + GET_SIZE(block)))
#define GET_PREV_BLOCK(block) \
  ((block_t*)((char *)(block) - GET_PREV_SIZE(block))) 
#define GET_META(block) \
  ((free_meta_t *)((block)+1))

static block_t *free_list = NULL;
static void *heap_start;
static void *heap_end;
static int is_heap_init = 0;

/* remove a free block from the free list */
void remove_node(block_t *block) {
  free_meta_t *meta = GET_META(block);
  if (meta->prev) {
    GET_META(meta->prev)->next = meta->next;
  }
  else {
    free_list = meta->next;
  }
  if (meta->next) {
    GET_META(meta->next)->prev = meta->prev;
  }
}

/* replace old_block with new_free_block in the free list */
void replace_node(block_t *new_free_block, block_t *old_block) {
  free_meta_t *new_meta = GET_META(new_free_block);
  free_meta_t *old_meta = GET_META(old_block);
 
  new_meta->next = old_meta->next;
  new_meta->prev = old_meta->prev;
 
  if (old_meta->prev) {
    GET_META(old_meta->prev)->next = new_free_block;
  } 
  else {
    free_list = new_free_block; // block was the head of the free_list
  }
  if (old_meta->next) {
    GET_META(old_meta->next)->prev = new_free_block;
  }
}

/* insert a block at the head of the free list */
void insert_node(block_t *block) {
  free_meta_t *block_meta= GET_META(block);
  block_meta->next = free_list;
  block_meta->prev = NULL;
  if (free_list) {
    GET_META(free_list)->prev = block;
  }
  free_list = block;
}

/* Initialize heap */
void heap_init() {
  void *start = mmap(NULL, MMAP_INIT_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (start== MAP_FAILED) {
    perror("mmap");
    return;
  }
    
  heap_start = start;
  heap_end = (char *)start + MMAP_INIT_SIZE;

  free_list = (block_t *)(start);
  free_list->size = MMAP_INIT_SIZE;
  SET_PREV_ALLOC(free_list); // does not coalesce the first block backwords

  free_meta_t *meta = (free_meta_t*)((char *)start + HEADER_SIZE);
  meta->next = NULL;
  meta->prev = NULL;
    
  is_heap_init = 1;
}

void split(block_t *block, size_t size) {
  /* don't split if the resulting free block is less than the minimum free block size */
  if (GET_SIZE(block) - size < MIN_FREE_BLOCK_SIZE) {
    block_t *next_block = GET_NEXT_BLOCK(block);
    if ((void *)next_block < heap_end) {
      SET_PREV_ALLOC(next_block);
    }
    /* remove the block from the free_list */
    remove_node(block);
    return; 
  } 
  
  /* split */
  block_t *new_free_block = (block_t *)((char *)block + size);
  new_free_block->size = GET_SIZE(block) - size; 
    
  SET_PREV_ALLOC(new_free_block); // set previous allocated flag
  
  size_t *footer = GET_FOOTER(new_free_block);
  *footer = new_free_block->size; 
  
  /* set new_free_block in the free_list */
  replace_node(new_free_block, block);
}

void *frmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  
  if (!is_heap_init) {
    heap_init();
  }
  
/* if the aligned requested size is less than the minimum size of a free block 
 * than the size is equal to the minimum size of a free block */
  size = ALIGN(size + HEADER_SIZE); 
  if (size < MIN_FREE_BLOCK_SIZE) {
    size = MIN_FREE_BLOCK_SIZE; 
  } 

/* traverse the free_list and look for a suitable block */
  block_t *current_free = free_list;
  while (current_free) {
    if (GET_SIZE(current_free) >= size) {
      split(current_free, size);   
      current_free->size = size | IS_PREV_ALLOC(current_free); 
      SET_ALLOC(current_free); 
      return (void *)(current_free+1);  
    } 
    current_free = GET_META(current_free)->next; // go to the next block 
  }

  return NULL;
}

void frfree(void *pointer) {
  if (!pointer) {
    return;
  }

  block_t *block = (block_t *)pointer - 1; // get to the header   
  SET_FREE(block); // set the allocated bit to 0

  block_t *next_block = GET_NEXT_BLOCK(block);
  /* make sure that it doesn't go outside of the heap */
  if ((void *)next_block < heap_end && !IS_ALLOC(next_block)) {
    block->size = (GET_SIZE(next_block) + GET_SIZE(block)) | IS_PREV_ALLOC(block);
    remove_node(next_block);
  }

  /* coalesce with the previous block */
  if (!IS_PREV_ALLOC(block)) {
    block_t *prev_block = GET_PREV_BLOCK(block);
    remove_node(prev_block);
    prev_block->size = (GET_SIZE(block) + GET_SIZE(prev_block)) | IS_PREV_ALLOC(prev_block); 
    block = prev_block; 
  }  

  size_t *footer = GET_FOOTER(block);
  *footer = block->size;

  /* set the prev flag in the next block */
  block_t *new_next_block = GET_NEXT_BLOCK(block);
  if ((void *)new_next_block < heap_end) {
    SET_PREV_FREE(new_next_block);
  }

  /* push onto the free_list */
  insert_node(block);
} 
