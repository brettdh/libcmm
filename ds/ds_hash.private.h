#ifndef _ODYBLURB_
#define _ODYBLURB_
/*
 *                               Data Station 1.0
 *                 A Data Staging System for Seamless Mobility
 * 
 *                    Copyright (c) 2002, Intel Corporation
 *                             All Rights Reserved
 * 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 * 
 *     * Neither the name of Intel Research Pittsburgh nor the names of
 *       its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#endif /* _ODYBLURB_ */

/*
** ds_hash.private.h: Implementation details of ds_hash_t, ds_hash_elt_t
*/

#ifndef _DS_HASH_PRIVATE_H_
#define _DS_HASH_PRIVATE_H_

#include <include/comdefs.h>

#include "ds_list.h"  /* we're a client of lists */
#include "ds_hash.h"  /* public parts */

/* magic numbers for structures */

extern const magic_t ds_hash_magic;
extern const magic_t ds_hash_iter_magic;

/* A hash table has a magic number, an array of ds_list_t's, 
   and a count of the number of elements.
   The safety and duplicate properties are maintained by the
   table's lists; the table doesn't bother about it.

   Hash tables are pretty simple: lists do most of the work
   They also don't need to keep track of their iterators: the lists
   do that (remember, that a hash iterator is just a list iterator
   that is pointed at several lists in a row.)
*/

struct ds_hash_t {
    magic_t       magic;
    HFN           hfn;
    int           nbuckets;
    ds_list_t   **buckets;
    int           count;
};

struct ds_hash_iter_t {
    magic_t         magic;
    ds_hash_t      *table;
    int             curbucket;
    ds_list_iter_t *curiter;
};

#define DS_HASH_VALID(tp)      ((tp) && ((tp)->magic == ds_hash_magic))
#define DS_HASH_ITER_VALID(ip) ((ip) && ((ip)->magic == ds_hash_iter_magic))

#endif /* _DS_HASH_PRIVATE_H_ */

