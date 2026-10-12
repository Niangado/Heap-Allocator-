# Heap Allocator (C++)

A general-purpose memory allocator — a from-scratch `malloc` / `free` — written in
C++. It requests one large chunk of memory directly from the operating system and
serves variable-size allocations out of it, using block headers, an address-sorted
free list, splitting, and coalescing.

Built on Windows with `VirtualAlloc` / `VirtualFree` (the Windows equivalent of
Linux's `mmap` / `munmap`).

---

## Features

- **Variable-size allocation:** `allocate(n)` returns a pointer to at least `n`
  usable bytes; `deallocate(p)` returns it.
- **First-fit search** over an embedded free list — no bookkeeping memory outside
  the heap itself.
- **Splitting:** a large free region is cut down to the requested size, and the
  remainder stays on the free list. Leftovers too small to ever be useful are not
  split off.
- **Coalescing:** freed blocks are inserted in address order and merged with
  physically adjacent free regions, so freeing everything restores one contiguous
  region.
- **16-byte alignment** for every returned pointer (suitable for any type,
  including SSE types).
- **Defensive frees:** `deallocate` rejects null-equivalent, misaligned,
  out-of-heap, and header-corrupted pointers, and detects simple double frees.
- **Overflow-safe sizing:** requests that would overflow during size rounding are
  rejected instead of producing a tiny allocation.
- **Move semantics:** heaps can be moved (ownership of the memory transfers);
  copying is disabled to prevent double releases.

## Usage

```cpp
#include "allocator.h"
#include <new>

struct Point { double x, y; };

int main() {
    HeapAllocator heap{ 4096 };                      // 4 KB heap

    int* nums = static_cast<int*>(heap.allocate(10 * sizeof(int)));
    Point* pt = new (heap.allocate(sizeof(Point))) Point{ 1.0, 2.0 };  // placement new

    pt->~Point();                                    // destroy before freeing
    heap.deallocate(pt);
    heap.deallocate(nums);

    heap.print();                                    // one free region: offset 0, size 4080
}
```

`allocate` hands out raw memory. To put an object with a constructor in it, use
placement new, and call the destructor yourself before `deallocate`.

## Building

Header-only (`allocator.h`). Requires a C++17 compiler on Windows (tested with
Visual Studio / MSVC). Include the header and build normally.

## How it works

### Memory layout

Every block — free or allocated — starts with a 16-byte header. The header's
`size` field always counts the **usable bytes after the header**, never the
header itself.

```
 free block                         allocated block
 ┌──────────┬──────────┐            ┌──────────┬──────────┐
 │ size (8) │ next (8) │ ...free    │ size (8) │ magic(4) │ pad(4) │ user data ...
 └──────────┴──────────┘            └──────────┴──────────┴────────┘
                                                                    ▲ returned pointer
```

The two header types are different *interpretations* of the same 16 bytes. `size`
sits at offset 0 in both, so it survives the switch between free and allocated.
A `static_assert` guarantees the two header structs stay the same size.

### Allocate

1. Reject `0`, and reject sizes that would overflow when rounded.
2. Round the request up to a multiple of 16.
3. Walk the free list for the **first** region large enough (first-fit).
4. **Split** if the region can hold the request, a new header, *and* at least 16
   usable bytes; otherwise hand over the whole region (avoids useless
   zero-size fragments).
5. Relink the free list (the leftover or the next region replaces the chosen one),
   write the allocated header, and return the address just past it.

### Deallocate

1. Ignore `nullptr`.
2. Validate the pointer: aligned, inside the heap, and header `magic` intact.
3. Walk the address-sorted free list to find the insertion point (detecting a
   block that is already on the list — a double free).
4. Insert the block in **address order**.
5. **Coalesce:** two free regions are adjacent when
   `next_address − address == header_size + size`; adjacent regions merge into one,
   absorbing the second region's header.

Keeping the list sorted by address is what makes coalescing possible: physical
neighbors in memory become neighbors in the list.

## Safety notes

Allocator bugs are a classic source of security vulnerabilities, and this project
deals with several directly:

- **Integer overflow in size calculations.** Rounding `SIZE_MAX` up naively wraps
  to a tiny value, so the allocator would "succeed" with a buffer far smaller than
  the caller believes — the classic overflow → undersized allocation → heap
  overflow pattern. The bound is checked *before* the arithmetic, in a form that
  cannot itself overflow.
- **Invalid free.** Pointers that are misaligned, outside the heap, or lack a valid
  header are rejected rather than corrupting the free list.
- **Double free.** Caught by the header integrity check (freeing overwrites the
  allocated header) and, as a second layer, by detecting the block already on the
  free list.
- **Header overwrites.** Headers sit in-band, right next to user data — the same
  design that makes real-world heap overflows exploitable. The magic value
  catches some corruption, but not deliberate forgery (see below).

## Testing

Tested with:
- allocation/free round trips (freed memory is reused at the same address)
- splitting, including exact-fit and minimum-split boundary cases
- out-of-order frees with mixed sizes coalescing back into a single region
- exhausting the heap, then freeing everything back to one region
- `deallocate(nullptr)`, foreign pointers, misaligned pointers, and double frees
- `allocate(SIZE_MAX)` (overflow rejection)
- move construction / move-from behavior
- 16-byte alignment of returned pointers

## Limitations / future work

- **Stale-pointer frees are not detected.** Free a block, have it reallocated, then
  free the old pointer again: the header looks valid because the block really is
  allocated again. Detecting this needs per-allocation state the caller can't
  forge — planned for a security-hardening milestone.
- **Magic values aren't secret.** A fixed `0xDEADBEEF` catches accidents, not
  attackers; randomized or keyed canaries are planned.
- **Coalescing scans the whole free list** (O(n) per free). Only the freed block's
  neighbors can change, so this can be made O(1) after insertion; boundary tags
  (footers) would remove the list walk entirely.
- **First-fit only.** Best-fit or segregated size classes would reduce
  fragmentation.
- **No invariant checker or randomized stress test yet.**
- **Fixed-size heap; not thread-safe; Windows only** (`VirtualAlloc`; a Linux port
  would use `mmap` / `munmap`).

## What I learned

Built after a fixed-size pool allocator, alongside the free-space management
chapter of *Operating Systems: Three Easy Pieces*. Key lessons: in-band metadata
and reinterpreting the same bytes through different struct layouts (and the
ordering bugs that causes when a write clobbers a field you still need); byte-level
pointer arithmetic; struct padding and alignment; little-endian byte order;
unsigned wraparound and how to write bounds checks that can't overflow; linked-list
relinking without losing nodes; and how allocator design decisions map directly to
heap-exploitation techniques.

---

Niangado
