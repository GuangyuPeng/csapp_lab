/*
 * mm.c - The malloc package based on Segregated Fits.
 * 
 * In this implementation of Segregated Fits, free blocks are divided into
 * different explicit free lists according to their sizes. Each free block
 * has header, footer, pointers to predecessor block and successor block.
 * Blocks in each free list are ordered by their addresses. When allocating
 * a block, search from the appropriate free list to find the first free
 * block in fit, otherwise increment the brk pointer to create a new free 
 * block. After allocating a block, the block is splitted if possible.
 * After freeing a block, the block tries to coalesce its adjacent free
 * blocks using optimazation of boundary tags.
 */
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

typedef uint32_t  Word_t;

#define WSIZE     4         /* word and header/footer size (bytes) */
#define DSIZE     8         /* double word size (bytes) */
#define CHUNKSIZE (1<<12)   /* extend heap by at list this amount (bytes)*/
#define ALIGNMENT 8         /* double word (8) alignment */
#define NUM_CLASS 20        /* number of free block classes */

#define MIN_BLOCK_IND 4     /* minimum block size (1<<IND bytes) */
#define MIN_BLOCK     (1<<MIN_BLOCK_IND)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* Pack a size and allocated bits into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)        (*(Word_t *)(p))
#define PUT(p, val)   (*(Word_t *)(p) = (val))

/* Read the size and allocated bits from address p */
#define GET_SIZE(p)         (GET(p) & ~0x7)
#define GET_ALLOC(p)        (GET(p) & 0x7)
#define GET_ALLOC_A0(p)     (GET(p) & 0x1)
#define GET_ALLOC_A1(p)     ((GET(p)>>1) & 0x1)

/* Set and unset allocated bits from address p */
#define SET_ALLOC_A0(p)     PUT(p, (GET(p) | 0x1))
#define SET_ALLOC_A1(p)     PUT(p, (GET(p) | 0x2))
#define UNSET_ALLOC_A0(p)   PUT(p, (GET(p) & ~0x1))
#define UNSET_ALLOC_A1(p)   PUT(p, (GET(p) & ~0x2))

/* Given block ptr bp, compute addresses of its header and footer*/
#define HDRP(bp)    ((char *)bp - WSIZE)
#define FTRP(bp)    ((char *)bp - DSIZE + GET_SIZE(HDRP(bp)))

/* Given block ptr bp, get ptrs of the adjacent next and previous blocks */
#define NEXT_BLKP(bp)   ((char *)bp + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp)   ((char *)bp - GET_SIZE((char *)bp - DSIZE))

/* Given block ptr bp, get ptrs of the predecessor and successor blocks */
#define PRED_BLKP(bp)   ((char *)GET(bp))
#define SUCC_BLKP(bp)   ((char *)GET((char *)bp + WSIZE))

/* Given block ptr bp, set its predecessor and successor fields */
#define SET_PRED(bp, ptr)   PUT(bp, (Word_t)ptr);
#define SET_SUCC(bp, ptr)   PUT((char *)bp + WSIZE, (Word_t)ptr)

static void *free_lists[NUM_CLASS] = {NULL};  /* segregated free lists */
static void *last_block = NULL;    /* ptr to last block before brk */

static void *find_fit(size_t asize);
static void *extend_heap(size_t asize);
static void *coalesce(void *bp);
static void rm_free_block(void *bp);
static void add_free_block(void *bp);
static void place(void *bp, size_t asize);
static size_t get_free_list_index(size_t asize);

/* 
 * mm_init - initialize the malloc package.
 *   return: 0 (OK), -1 (error).
 */
int mm_init(void) {
  char *heap_start;
  size_t i;

  /* Init global variables */
  last_block = NULL;
  for (i = 0; i < NUM_CLASS; i++) {
    free_lists[i] = NULL;
  }

  /* Init heap with 2 words */
  if ((heap_start = mem_sbrk(2 * WSIZE)) == (void *)-1) {
    return -1;
  }

  /* Word1 is padding, word2 is epilogue block header */
  PUT(heap_start, 0);
  PUT(heap_start+WSIZE, 1);

  return 0;
}

/* 
 * mm_malloc - Allocate a block based on Segregated Fits.
 */
void *mm_malloc(size_t size) {
  size_t asize;
  void *bp;
  size_t extend_size;
  
  if (size == 0) return NULL;

  /* Compute actual block size */
  size = size + WSIZE;
  if (size < WSIZE) return NULL;      /* Overflow */
  asize = ALIGN(size);
  if (asize < size) return NULL;      /* Overflow */
  asize = MAX(asize, MIN_BLOCK);

  /* Find the first-fit free block */
  bp = find_fit(asize);

  /* If no free block, extend heap */
  if (!bp) {
    extend_size = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extend_size);
    if (!bp) return NULL;             /* No more heap size */
  }
  else {
    rm_free_block(bp);
  }

  /* Allocate the free block */
  place(bp, asize);
  return bp;
}

/*
 * mm_free - Freeing a block based on Segregated Fits.
 */
void mm_free(void *ptr) {
  void *next_blk;

  if (!ptr) return;
  next_blk = NEXT_BLKP(ptr);

  /* Change ptr to free block */
  UNSET_ALLOC_A0(HDRP(ptr));
  PUT(FTRP(ptr), GET(HDRP(ptr)));
  UNSET_ALLOC_A1(HDRP(next_blk));

  /* Coalesce adjacent blocks */
  ptr = coalesce(ptr);

  /* Add to free list */
  add_free_block(ptr);
}

/*
 * mm_realloc - Resize an allocated block based on Segregated Fits.
 */
void *mm_realloc(void *ptr, size_t size) {
  size_t asize;
  size_t old_size;
  size_t delta_size;
  Word_t alloc_bits;
  void *new_blk;
  void *src;
  void *dst;
  void *end;
  
  if (!ptr) return mm_malloc(size);
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  old_size = GET_SIZE(HDRP(ptr));

  /* Compute actual block size */
  size = size + WSIZE;
  if (size < WSIZE) return NULL;      /* Overflow */
  asize = ALIGN(size);
  if (asize < size) return NULL;      /* Overflow */
  asize = MAX(asize, MIN_BLOCK);

  /* asize <= old_size: split block if possible */
  if (asize <= old_size) {
    delta_size = old_size - asize;
    if (delta_size >= MIN_BLOCK) {
      alloc_bits = GET_ALLOC(HDRP(ptr));
      PUT(HDRP(ptr), PACK(asize, alloc_bits));
      new_blk = NEXT_BLKP(ptr);
      PUT(HDRP(new_blk), PACK(delta_size, 0x3));
      mm_free(new_blk);
    }
    return ptr;
  }
  /* asize > old_size: extend block or allocate new block */
  else {
    delta_size = asize - old_size;
    delta_size = MAX(delta_size, MIN_BLOCK);
    new_blk = NEXT_BLKP(ptr);
    if (!GET_ALLOC_A0(HDRP(new_blk)) && 
        GET_SIZE(HDRP(new_blk)) >= delta_size) {    /* extend block */
      rm_free_block(new_blk);
      place(new_blk, delta_size);
      alloc_bits = GET_ALLOC(HDRP(ptr));
      PUT(HDRP(ptr), PACK(old_size+GET_SIZE(HDRP(new_blk)), alloc_bits));
      if (last_block == new_blk) {
        last_block = ptr;
      }
      return ptr;
    }
    else {       /* allocate new block */
      new_blk = mm_malloc(size - WSIZE);
      if (!new_blk) return NULL;
      /* copy data from ptr to new_blk */
      src = ptr;
      dst = new_blk;
      end = src - WSIZE + old_size;
      while (src < end) {
        PUT(dst, GET(src));
        src += WSIZE;
        dst += WSIZE;
      }
      mm_free(ptr);
      return new_blk;
    }
  }
}

/*
 * find_fit - Start from appropriate free list, find the first free block.
 *   return: ptr of the free block if found, NULL if not found.
 */
static void *find_fit(size_t asize) {
  void *bp;
  size_t i;
  size_t j;

  /* Determine free list index i */
  i = get_free_list_index(asize);

  /* Traverse from free list i, find free block */
  for (j = i; j < NUM_CLASS; j++) {
    bp = free_lists[j];
    while (bp) {
      if (GET_SIZE(HDRP(bp)) >= asize) {
        return bp;
      }
      bp = SUCC_BLKP(bp);
    }
  }

  return NULL;
}

/*
 * extend_heap - Extend heap size and create a new free block, but not add
 *   it to the free lists. Note: prameter asize should be aligned before
 *   calling the function.
 *   return: ptr to the free block if success, NULL otherwise.
 */
static void *extend_heap(size_t asize) {
  void *old_brk;
  void *brk;

  /* Extend heap by calling sbrk */
  if ((old_brk = mem_sbrk(asize)) == (void *)-1) {
    return NULL;
  }

  /* Set epilogue block */
  brk = mem_heap_hi() + 1;
  PUT(HDRP(brk), GET(HDRP(old_brk)));

  /* Set block fields */
  PUT(HDRP(old_brk), PACK(asize, 0));     /* header */
  if (!last_block) {                      /* a1 bit */
    SET_ALLOC_A1(HDRP(old_brk));
  }
  else {
    if (GET_ALLOC_A0(HDRP(last_block))) {
      SET_ALLOC_A1(HDRP(old_brk));
    }
  }
  last_block = old_brk;
  PUT(FTRP(old_brk), GET(HDRP(old_brk))); /* footer */

  /* Return coalesced block */
  return coalesce(old_brk);
}

/*
 * coalesce - Coalesce block bp with adjacent free blocks. The coalesced
 *   free block is not added to the free lists.
 *   return: ptr to the coalesced free block.
 */
static void *coalesce(void *bp) {
  int next_blk_alloc;
  int prev_blk_alloc;
  size_t size = 0;
  void *next_blk;
  void *prev_blk;

  next_blk = NEXT_BLKP(bp);
  prev_blk = PREV_BLKP(bp);
  next_blk_alloc = GET_ALLOC_A0(HDRP(next_blk));
  prev_blk_alloc = GET_ALLOC_A1(HDRP(bp));
  size = GET_SIZE(HDRP(bp));

  if (next_blk_alloc && prev_blk_alloc) {
    return bp;
  }
  else if (!next_blk_alloc && prev_blk_alloc) {   /* cur + next */
    rm_free_block(next_blk);
    size += GET_SIZE(HDRP(next_blk));
    PUT(HDRP(bp), PACK(size, 0));
    SET_ALLOC_A1(HDRP(bp));
    PUT(FTRP(bp), GET(HDRP(bp)));
    if (last_block == next_blk) {
      last_block = bp;
    }
    return bp;
  }
  else if (next_blk_alloc && !prev_blk_alloc) {   /* prev + cur */  
    rm_free_block(prev_blk);
    size += GET_SIZE(HDRP(prev_blk));
    PUT(HDRP(prev_blk), PACK(size, 0));
    SET_ALLOC_A1(HDRP(prev_blk));
    PUT(FTRP(prev_blk), GET(HDRP(prev_blk)));
    if (last_block == bp) {
      last_block = prev_blk;
    }
    return prev_blk;
  }
  else {    /* prev + cur + next */
    rm_free_block(prev_blk);
    rm_free_block(next_blk);
    size += GET_SIZE(HDRP(prev_blk));
    size += GET_SIZE(HDRP(next_blk));
    PUT(HDRP(prev_blk), PACK(size, 0));
    SET_ALLOC_A1(HDRP(prev_blk));
    PUT(FTRP(prev_blk), GET(HDRP(prev_blk)));
    if (last_block == next_blk) {
      last_block = prev_blk;
    }
    return prev_blk;
  }
}

/*
 * rm_free_block - Remove free block bp from its free list.
 */
static void rm_free_block(void *bp) {
  void *pred_blk;
  void *succ_blk;
  size_t index;

  pred_blk = PRED_BLKP(bp);
  succ_blk = SUCC_BLKP(bp);
  index = get_free_list_index(GET_SIZE(HDRP(bp)));

  if (pred_blk && succ_blk) {
    SET_SUCC(pred_blk, succ_blk);
    SET_PRED(succ_blk, pred_blk);
  }
  else if (!pred_blk && succ_blk) {
    free_lists[index] = succ_blk;
    SET_PRED(succ_blk, NULL);
  }
  else if (pred_blk && !succ_blk) {
    SET_SUCC(pred_blk, NULL);
  }
  else {
    free_lists[index] = NULL;
  }
}

/*
 * add_free_block - Add block bp to the corresponding free list,
 *   blocks in each free list are ordered by their addresses.
 */
static void add_free_block(void *bp) {
  void *pred_blk = NULL;
  void *succ_blk = NULL;
  size_t index;

  index = get_free_list_index(GET_SIZE(HDRP(bp)));
  succ_blk = free_lists[index];

  /* Find first succ_blk > bp */
  while (succ_blk && succ_blk <= bp) {
    pred_blk = succ_blk;
    succ_blk = SUCC_BLKP(succ_blk);
  }

  /* Add bp between pred_blk and succ_blk */
  if (pred_blk && succ_blk) {
    SET_SUCC(pred_blk, bp);
    SET_PRED(bp, pred_blk);
    SET_SUCC(bp, succ_blk);
    SET_PRED(succ_blk, bp);
  }
  else if (!pred_blk && succ_blk) {
    free_lists[index] = bp;
    SET_PRED(bp, NULL);
    SET_SUCC(bp, succ_blk);
    SET_PRED(succ_blk, bp);
  }
  else if (pred_blk && !succ_blk) {
    SET_SUCC(pred_blk, bp);
    SET_PRED(bp, pred_blk);
    SET_SUCC(bp, NULL);
  }
  else {
    free_lists[index] = bp;
    SET_PRED(bp, NULL);
    SET_SUCC(bp, NULL);
  }
}

/*
 * place - Allocate asize bytes in block bp, split bp to create
 *   a smaller free block if possible.
 */
static void place(void *bp, size_t asize) {
  size_t bp_size;
  size_t split_size;
  void *next_blk;
  Word_t bp_alloc;

  bp_size = GET_SIZE(HDRP(bp));
  next_blk = NEXT_BLKP(bp);
  split_size = bp_size - asize;

  /* Split block bp */
  if (split_size >= MIN_BLOCK) {
    bp_alloc = GET_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(asize, bp_alloc));     /* bp header */
    next_blk = NEXT_BLKP(bp);
    PUT(HDRP(next_blk), PACK(split_size, 0)); /* next_blk header */
    PUT(FTRP(next_blk), GET(HDRP(next_blk))); /* next_blk footer */
    add_free_block(next_blk);
    if (last_block == bp) {
      last_block = next_blk;
    }
  }

  SET_ALLOC_A0(HDRP(bp));
  SET_ALLOC_A1(HDRP(next_blk));
}

/*
 * get_free_list_index - Determine free list index from block size.
 *   Note: asize should be >= MIN_BLOCK.
 */
static size_t get_free_list_index(size_t asize) {
  size_t i = 0;

  if (asize > (1<<(NUM_CLASS+2))) i = NUM_CLASS-1;
  else {
    asize = asize - 1;
    while (asize) {
      i = i+1;
      asize = asize >> 1;
    }
    i = i - MIN_BLOCK_IND;
  }

  return i;
}
