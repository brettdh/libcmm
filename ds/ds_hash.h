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
** The "hash" data structure; a hash table
*/

#ifndef _DS_HASH_H_
#define _DS_HASH_H_

#include <include/comdefs.h>
#include "ds_list.h"

/*
   A hash table is a collection of ds_list_t's.
   They are not thread safe

   Hash tables have many of the same properties that lists do.
   It's probably necessary to understand ds_list_t to understand
   ds_hash_t.  In particular, the ordering function determines whether
   or not the buckets are sorted, and whether the member and duplicate
   tests use pointer-equality or the ordering function for equality.

*/

typedef struct ds_hash_t ds_hash_t;    /* opaque hashtable */

#ifdef __STDC__                       /* hash tables have hash functions */
typedef long (*HFN)(void*);
#else
typedef long (*HFN)();
#endif

/*
   The following operations are allowed on hash tables
*/

/*** Observers ***/

extern bool            ds_hash_valid    (ds_hash_t *t);
extern int             ds_hash_count    (ds_hash_t *t);
extern void           *ds_hash_first    (ds_hash_t *t, 
					 void *e);       
extern void           *ds_hash_last     (ds_hash_t *t, 
					 void *e);       
extern void           *ds_hash_member   (ds_hash_t *t, 
					 void *e); 

/*** Mutators ***/

extern ds_hash_t       *ds_hash_create     (COMPFN c,
					    HFN h,
					    int nbuckets,
					    bool safe_destroy,
					    bool dups_ok);
extern void             ds_hash_destroy    (ds_hash_t *t);
extern void            *ds_hash_insert     (ds_hash_t *t, void *e);
extern void            *ds_hash_append     (ds_hash_t *t, void *e);
extern void            *ds_hash_get_any    (ds_hash_t *t);
extern void            *ds_hash_get_first  (ds_hash_t *t, void *e);
extern void            *ds_hash_get_last   (ds_hash_t *t, void *e);
extern void            *ds_hash_remove     (ds_hash_t *t, void *e);
					 
/*** Iterators ***/

typedef struct ds_hash_iter_t ds_hash_iter_t;  /* opaque */

/* 
   You can create an interator, destroy an iterator, or ask for the
   "next" element in the sequence.  Iterators and hash tables
   communicate with one another: if an iterator's "next" element is
   removed from the iterator's table, the iterator will be advanced
   one element.  Once an iterator has reached the end of the table
   (i.e.  ds_hash_iter_next returns NULL) it is considered closed: new
   items added to the table will not be picked up by the iterator.
   New items added to a table may or may not be picked up by the
   iterator.  Frankly, twiddling the table while the iterator is
   hooked up is kinda silly anyway.
*/

extern ds_hash_iter_t *ds_hash_iter_create  (ds_hash_t *t);
extern void            ds_hash_iter_destroy (ds_hash_iter_t *i);
extern void           *ds_hash_iter_next    (ds_hash_iter_t *i);

#endif /* _DS_HASH_H_ */

