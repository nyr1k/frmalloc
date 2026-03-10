# frmalloc

A heap allocator implemented in C from scratch. Uses an explicit free list with boundary tags for O(1) coalescing.

### Block layout

Every block has a 16-byte header storing its size and two flag bits

Free blocks additionally store next/prev pointers in the payload area, and a footer which stores a copy of the size field at the end of the block. The footer allows `frfree` locate the previous block in O(1) without traversing the heap.

Allocated blocks have no footer amd next/prev pointers. The payload area is entirely available to the user.


Free block:
**[ header (size + flags) | next ptr | prev ptr | ... | footer ]**

Allocated block:
**[ header (size + flags) | payload ...                        ]**

### Allocation

`frmalloc` traverses the explicit free list (first-fit) looking for a block large enough to satisfy the request. If the block is larger than needed, it is split. The remainder becomes a new free block and is inserted into the free list in the place of the original.

### Freeing and coalescing

`frfree` marks the block as free and immediately coalesces with adjacent free neighbors:

- **Next block**: check if `IS_ALLOC(next)` is 0, merge if so
- **Previous block**: check if `IS_PREV_ALLOC` flag in the current header is 0, use the footer to find and merge with it if so

## Build 
`
gcc test.c frmalloc.c
`

I think it will compile only on unix-like systems because there are no dependencies except for `mmap`

## Resources 
To write this mini project I referenced the following resources: 
- https://gee.cs.oswego.edu/dl/html/malloc.html
- https://sourceware.org/glibc/wiki/MallocInternals
- https://web.stanford.edu/class/archive/cs/cs107/cs107.1256/lectures/24/Lecture24.pdf
