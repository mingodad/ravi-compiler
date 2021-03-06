/*
* allocate.c - simple space-efficient blob allocator.
*
* Copyright (C) 2003 Transmeta Corp.
*               2003-2004 Linus Torvalds
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
* Simple allocator for data that doesn't get partially free'd.
* The tokenizer and parser allocate a _lot_ of small data structures
* (often just two-three bytes for things like small integers),
* and since they all depend on each other you can't free them
* individually _anyway_. So do something that is very space-
* efficient: allocate larger "blobs", and give out individual
* small bits and pieces of it with no maintenance overhead.
*/
/*
* This version is part of the Ravi Compiler project.
* Copyright (C) 2017-2020 Dibyendu Majumdar
*/

#include <allocate.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *blob_alloc(size_t size) {
  void *ptr;
  ptr = malloc(size);
  if (ptr != NULL)
    memset(ptr, 0, size);
  return ptr;
}

static void blob_free(void *addr, size_t size) {
  (void)size;
  free(addr);
}

void raviX_allocator_init(struct allocator *A, const char *name, size_t size,
                    unsigned int alignment, unsigned int chunking) {
  A->name_ = name;
  A->blobs_ = NULL;
  A->size_ = size;
  A->alignment_ = alignment;
  A->chunking_ = chunking;
  A->freelist_ = NULL;
  A->allocations = 0;
  A->total_bytes = 0;
  A->useful_bytes = 0;
}

void *raviX_allocator_allocate(struct allocator *A, size_t extra) {
  size_t size = extra + A->size_;
  size_t alignment = A->alignment_;
  struct allocation_blob *blob = A->blobs_;
  void *retval;

  assert(size <= A->chunking_);
  /*
  * NOTE! The freelist only works with things that are
  *  (a) sufficiently aligned
  *  (b) use a constant size
  * Don't try to free allocators that don't follow
  * these rules.
  */
  if (A->freelist_) {
    void **p = (void **)A->freelist_;
    retval = p;
    A->freelist_ = *p;
    memset(retval, 0, size);
    return retval;
  }

  A->allocations++;
  A->useful_bytes += size;
  size = (size + alignment - 1) & ~(alignment - 1);
  if (!blob || blob->left < size) {
    size_t offset, chunking = A->chunking_;
    struct allocation_blob *newblob =
        (struct allocation_blob *)blob_alloc(chunking);
    if (!newblob) {
      fprintf(stderr, "out of memory\n");
      abort();
    }
    A->total_bytes += chunking;
    newblob->next = blob;
    blob = newblob;
    A->blobs_ = newblob;
    offset = offsetof(struct allocation_blob, data);
    offset = (offset + alignment - 1) & ~(alignment - 1);
    blob->left = chunking - offset;
    blob->offset = offset - offsetof(struct allocation_blob, data);
  }
  retval = blob->data + blob->offset;
  blob->offset += size;
  blob->left -= size;
  return retval;
}

void raviX_allocator_free(struct allocator *A, void *entry) {
  void **p = (void **)entry;
  *p = A->freelist_;
  A->freelist_ = p;
}
void raviX_allocator_show_allocations(struct allocator *A) {
  fprintf(stderr, "%s: %d allocations, %d bytes (%d total bytes, "
                  "%6.2f%% usage, %6.2f average size)\n",
          A->name_, (int)A->allocations, (int)A->useful_bytes,
          (int)A->total_bytes, 100 * (double)A->useful_bytes / A->total_bytes,
          (double)A->useful_bytes / A->allocations);
}
void raviX_allocator_drop_all_allocations(struct allocator *A) {
  struct allocation_blob *blob = A->blobs_;
  A->blobs_ = NULL;
  A->allocations = 0;
  A->total_bytes = 0;
  A->useful_bytes = 0;
  A->freelist_ = NULL;
  while (blob) {
    struct allocation_blob *next = blob->next;
    blob_free(blob, A->chunking_);
    blob = next;
  }
}
void raviX_allocator_destroy(struct allocator *A) {
  raviX_allocator_drop_all_allocations(A);
  A->blobs_ = NULL;
  A->allocations = 0;
  A->total_bytes = 0;
  A->useful_bytes = 0;
  A->freelist_ = NULL;
}
void raviX_allocator_transfer(struct allocator *A, struct allocator *transfer_to) {
  assert(transfer_to->blobs_ == NULL);
  assert(transfer_to->freelist_ == NULL);
  transfer_to->blobs_ = A->blobs_;
  transfer_to->allocations = A->allocations;
  transfer_to->total_bytes = A->total_bytes;
  transfer_to->useful_bytes = A->useful_bytes;
  transfer_to->freelist_ = A->freelist_;
  transfer_to->alignment_ = A->alignment_;
  transfer_to->chunking_ = A->chunking_;
  transfer_to->size_ = A->size_;
  A->blobs_ = NULL;
  A->allocations = 0;
  A->total_bytes = 0;
  A->useful_bytes = 0;
  A->freelist_ = NULL;
}

#if 0

struct foo {
  int a, b;
};

int raviX_test_allocator() {
  struct allocator alloc;
  raviX_allocator_init(&alloc, "foo", sizeof(struct foo), __alignof__(struct foo),
                 sizeof(struct allocation_blob) + sizeof(struct foo) * 2);
  struct foo *t1 = (struct foo *)raviX_allocator_allocate(&alloc, 0);
  if (t1 == NULL)
    return 1;
  if (alloc.alignment_ != __alignof__(struct foo))
    return 1;
  if (alloc.allocations != 1)
    return 1;
  if (alloc.freelist_ != NULL)
    return 1;
  struct foo *t2 = (struct foo *)raviX_allocator_allocate(&alloc, 0);
  if (t2 != t1 + 1)
    return 1;
  //dmrC_allocator_show_allocations(&alloc);
  raviX_allocator_free(&alloc, t1);
  raviX_allocator_free(&alloc, t2);
  struct foo *t3 = (struct foo *)raviX_allocator_allocate(&alloc, 0);
  if (t3 != t2)
    return 1;
  struct foo *t4 = (struct foo *)raviX_allocator_allocate(&alloc, 0);
  if (t4 != t1)
    return 1;
  struct foo *t5 = (struct foo *)raviX_allocator_allocate(&alloc, 0);
  (void)t5;
  if (alloc.total_bytes !=
      (sizeof(struct allocation_blob) + sizeof(struct foo) * 2) * 2)
    return 1;
  struct allocator alloc2;
  memset(&alloc2, 0, sizeof alloc2);
  struct allocation_blob *saved = alloc.blobs_;
  raviX_allocator_transfer(&alloc, &alloc2);
  if (alloc.blobs_ != NULL)
	  return 1;
  if (alloc2.blobs_ != saved)
	  return 1;
  raviX_allocator_destroy(&alloc2);
  printf("allocator tests okay\n");
  return 0;
}

#endif