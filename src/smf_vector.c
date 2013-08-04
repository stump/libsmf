/* libsmf: Standard MIDI File library
 * Copyright (c) 2013 John Stumpo <stump@stump.io>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * ALTHOUGH THIS SOFTWARE IS MADE OF WIN AND SCIENCE, IT IS PROVIDED BY THE
 * AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * smf_vector_t-handling functions.
 *
 */

#include "smf.h"
#include "smf_private.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define VECTOR_INIT_ALLOC 16

/**
 * Change the allocated array size for vector to newlen items.
 * \return 0 on success, -1 on error
 */
static int
vector_resize_array(smf_vector_t *vector, unsigned int newlen)
{
  void **newary;

  assert(newlen >= vector->allocated_len);

  newary = realloc(vector->ary, newlen * sizeof(void *));
  if (newary == NULL) {
    smf_critical("vector_resize_array: failed to reallocate array");
    return -1;
  } else {
    vector->ary = newary;
    vector->allocated_len = newlen;
    return 0;
  }
}

/**
 * Create a new smf_vector_t.
 * \return the new vector, or NULL on error
 */
smf_vector_t *
vector_new(void)
{
  smf_vector_t *vector = malloc(sizeof(smf_vector_t));
  if (vector == NULL) {
    smf_critical("vector_new: malloc failed");
    return NULL;
  }
  memset(vector, 0, sizeof(smf_vector_t));

  if (vector_resize_array(vector, VECTOR_INIT_ALLOC) < 0) {
    vector_free(vector);
    return NULL;
  }

  return vector;
}

/**
 * Get the item at the given index.
 * \return the item
 */
void *
vector_index(smf_vector_t *vector, unsigned int index)
{
  assert(index < vector->len);
  return vector->ary[index];
}

/**
 * Find an item equal (pointer comparison) to the given item.
 * \return index of the first matching item, or -1 if there is none
 */
int
vector_find(smf_vector_t *vector, void *item)
{
  unsigned int i;

  for (i = 0; i < vector->len; i++)
    if (vector->ary[i] == item)
      return i;

  return -1;
}

/**
 * Append an item to the end of a vector.
 * \return index of the new item (i.e. new length - 1), or -1 on error
 */
int
vector_add(smf_vector_t *vector, void *item)
{
  if (vector->len == vector->allocated_len)
    if (vector_resize_array(vector, vector->allocated_len * 2) < 0)
      return -1;

  vector->ary[vector->len] = item;
  return vector->len++;
}

/**
 * Remove the first item equal (pointer comparison) to the given item.
 * \return (former) index of the removed item, or -1 if no match was found
 */
int
vector_remove(smf_vector_t *vector, void *item)
{
  int idx = vector_find(vector, item);
  if (idx < 0)
    return -1;

  vector_remove_index(vector, idx);
  return idx;
}

/**
 * Remove the item at the given index.
 * \return the removed pointer
 */
void *
vector_remove_index(smf_vector_t *vector, unsigned int index)
{
  void **to_remove = vector->ary + index;
  void *removed_pointer = *to_remove;
  assert(index < vector->len);
  memmove(to_remove, to_remove + 1, (vector->len - (index+1)) * sizeof(void *));
  vector->len--;
  return removed_pointer;
}

/**
 * Sort the vector using the given comparison function.
 *
 * The function is passed pointers to the pointers stored in the vector.
 */
void
vector_sort(smf_vector_t *vector, int (*compare)(const void *, const void *))
{
  qsort(vector->ary, vector->len, sizeof(void *), compare);
}

/**
 * Destroy a vector and free all associated memory.
 */
void
vector_free(smf_vector_t *vector)
{
  free(vector->ary);
  free(vector);
}
