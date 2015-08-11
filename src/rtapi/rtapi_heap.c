/********************************************************************
 * Copyright (C) 2014 Michael Haberler <license AT mah DOT priv DOT at>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ********************************************************************/

#include "rtapi.h"
#include "rtapi_heap.h"
#include "rtapi_heap_private.h"
#include "rtapi_export.h"
#include "rtapi_bitops.h"
#ifdef ULAPI
#include <stdio.h>
#endif

// this is straight from the malloc code in:
// K&R The C Programming Language, Edition 2, pages 185-189
// adapted to use offsets relative to the heap descriptor
// so it can be used as a shared memory malloc
static void _rtapi_unlocked_free(struct rtapi_heap *h, void *ap);

void __attribute__((format(printf,3,4)))
heap_print(struct rtapi_heap *h, int level, const char *fmt, ...)
{
#ifdef RTAPI
    if (!h->msg_handler)
	return;
    char buffer[80];

    va_list args;
    va_start(args, fmt);
    strcpy(buffer, h->name);
    strcat(buffer, " ");
    size_t l = strlen(buffer);
    rtapi_vsnprintf(buffer + l, sizeof(buffer) - l, fmt, args);
    va_end(args);
    h->msg_handler(level, buffer, NULL);
#endif
}

// scoped lock helper
static void malloc_autorelease_mutex(rtapi_atomic_type **mutex) {
    rtapi_mutex_give(*mutex);
}


void  *_rtapi_malloc_aligned(struct rtapi_heap *h, size_t nbytes, size_t align)
{
    if (h->flags & RTAPIHEAP_TRACE_MALLOC)
	heap_print(h, RTAPI_MSG_INFO, "%s: size=%zu align=%zu",
		   __FUNCTION__, nbytes, align);

    if ((align & (align - 1)) != 0) { // m must be power of 2
	heap_print(h, RTAPI_MSG_ERR,
		   "%s: odd alignment %zu, size=%zu\n",
		   __FUNCTION__, align, nbytes);
	return NULL;
    }
    void *base = _rtapi_malloc(h, nbytes + align);
    void *result = (void *)((uintptr_t)(base + align) & - align);
    size_t slack = result - base;
    if (slack < sizeof(rtapi_malloc_tag_t)) {
	heap_print(h, RTAPI_MSG_ERR, "%s: ASSUMPTION VIOLATED slack=%zu\n",
		   __FUNCTION__, slack);
	return NULL;	// grind things to a halt
    }

    rtapi_malloc_tag_t *rt = (rtapi_malloc_tag_t *) result - 1;
    rt->attr = ATTR_ALIGNED;      // so free can deal with it properly
    rt->size = heap_off(h, base); // refer to actual malloc region

    size_t trim = (align-slack)/sizeof(rtapi_malloc_hdr_t);

    if ((h->flags & RTAPIHEAP_TRIM) && (trim > 0)) {

	// trim allignment overallocation:
	// split the block into two allocations
	rtapi_malloc_hdr_t *this = (rtapi_malloc_hdr_t *) base - 1;
	rtapi_malloc_hdr_t *new = this + (this->s.tag.size - trim);

	// splice in the new block adjusting sizes
	new->s.next = this->s.next;
	new->s.tag.size = trim;
	new->s.tag.attr = 0;
	this->s.next = heap_off(h, new);
	this->s.tag.size -= trim;
	// and free the overallocated block
	_rtapi_unlocked_free(h, new + 1);
    }

    if (!is_aligned(result, align)) { // QA
	heap_print(h, RTAPI_MSG_ERR, "%s: BAD ALIGNMENT %p, size=%zu align=%zu,\n",
		   __FUNCTION__, result, nbytes, align);
    }
    return result;
}

void _rtapi_free(struct rtapi_heap *h, void *);

void *_rtapi_malloc(struct rtapi_heap *h, size_t nbytes)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    rtapi_malloc_hdr_t *p, *prevp;
    size_t nunits  = (nbytes + sizeof(rtapi_malloc_hdr_t) - 1) /
	sizeof(rtapi_malloc_hdr_t) + 1;

    // heaps are explicitly initialized, see rtapi_heap_init()
    // if ((prevp = h->freep) == NULL) {	// no free list yet
    // 	h->base.s.ptr = h->freep = prevp = &h->base;
    // 	h->base.s.tag.size = 0;
    // }
    rtapi_malloc_hdr_t *freep = heap_ptr(h, h->free_p);

    prevp = freep;
    for (p = heap_ptr(h, prevp->s.next); ; prevp = p, p = heap_ptr(h, p->s.next)) {
	if (p->s.tag.size >= nunits) {	/* big enough */
	    if (p->s.tag.size == nunits)	/* exactly */
		prevp->s.next = p->s.next;
	    else {				/* allocate tail end */
		p->s.tag.size -= nunits;
		p += p->s.tag.size;
		p->s.tag.size = nunits;
	    }
	    p->s.tag.attr = 0;
	    h->free_p = heap_off(h, prevp);
	    size_t alloced = _rtapi_allocsize(h, p+1);
	    h->requested += nbytes;
	    h->allocated += alloced;
	    if (h->flags & RTAPIHEAP_TRACE_MALLOC)
		heap_print(h, RTAPI_MSG_INFO, "malloc req=%zu actual=%zu at %p\n",
			   nbytes, alloced, p);
	    return (void *)(p+1);
	}
	if (p == freep)	{	/* wrapped around free list */
	    heap_print(h, RTAPI_MSG_INFO, "rtapi_malloc: out of memory"
		       " (size=%zu arena=%zu)\n", nbytes, h->arena_size);
	    //if ((p = morecore(nunits)) == NULL)
	    return NULL;	/* none left */
	}
    }
}

static void _rtapi_unlocked_free(struct rtapi_heap *h, void *ap)
{
    rtapi_malloc_hdr_t *bp, *p;
    rtapi_malloc_hdr_t *freep =  heap_ptr(h,h->free_p);

    rtapi_malloc_tag_t *rt = (rtapi_malloc_tag_t *) ap - 1;

    // a block with non-standard alignment?
    if (rt->attr & ATTR_ALIGNED) {

	// yes, retrieve original region and release that
	void *base = heap_ptr(h, rt->size);
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_INFO,
		       "%s: free aligned %p->%p size=%zu\n",
		       __FUNCTION__, ap, base, _rtapi_allocsize(h, base));
	ap = base;
    }

    bp = (rtapi_malloc_hdr_t *)ap - 1;	// point to block header
    size_t alloc = bp->s.tag.size;

    for (p = freep;
	 !(bp > p && bp < (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next));
	 p = heap_ptr(h,p->s.next))
	if (p >= (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next) &&
	    (bp > p || bp < (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))) {
	    // freed block at start or end of arena
	    if (h->flags & RTAPIHEAP_TRACE_FREE)
		heap_print(h, RTAPI_MSG_INFO,
			   "%s: freed block at start or end of arena n=%u\n",
			   __FUNCTION__,
			   alloc);
	    break;
	}

    h->freed += sizeof(rtapi_malloc_hdr_t) * (bp->s.tag.size - 1);

    if (bp + bp->s.tag.size == ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))) {
	// join to upper neighbor
	size_t ns = ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))->s.tag.size;
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_INFO, "%s: join upper  %u+=%u\n",
		       __FUNCTION__, ns, alloc);
	bp->s.tag.size += ns;
	bp->s.next = ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))->s.next;

    } else
	bp->s.next = p->s.next;
    if (p + p->s.tag.size == bp) {
	// join to lower nbr
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_INFO, "%s: join lower %u+=%u\n",
		       __FUNCTION__, bp->s.tag.size, alloc);
	p->s.tag.size += bp->s.tag.size;
	p->s.next = bp->s.next;

    } else {
	p->s.next = heap_off(h,bp);
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_INFO,  "%s: free fragment n=%u\n",
		       __FUNCTION__, alloc);
    }
    h->free_p = heap_off(h,p);
}

void _rtapi_free(struct rtapi_heap *h,void *ap)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);
    _rtapi_unlocked_free(h, ap);
}

// given a pointer returned by _rtapi_malloc(),
// returns number of bytes actually available for use (which
// might be a bit larger than requested) due to chunk alignent)
size_t _rtapi_allocsize(struct rtapi_heap *h, const void *ap)
{
    rtapi_malloc_tag_t *rt = (rtapi_malloc_tag_t *) ap - 1;
    if (rt->attr & ATTR_ALIGNED) {
	ap = heap_ptr(h, rt->size);
    }
    rtapi_malloc_hdr_t *p = (rtapi_malloc_hdr_t *) ap - 1;
    return (p->s.tag.size -1) * sizeof (rtapi_malloc_hdr_t);
}

void *_rtapi_calloc(struct rtapi_heap *h, size_t nelem, size_t elsize)
{
    void *p = _rtapi_malloc (h,nelem * elsize);
    if (!p)
        return NULL;
    memset(p, 0, nelem * elsize);
    return p;
}

void *_rtapi_realloc(struct rtapi_heap *h, void *ptr, size_t size)
{
    size_t sz = _rtapi_allocsize (h, ptr);

    // requested size fits current allocation?
    if (size <= sz)
	// could use trim like in malloc_aligned but not much gained
	return ptr; // nothing to do

    void *p = _rtapi_malloc (h, size);
    if (!p)
        return (p);
    memcpy(p, ptr, (sz > size) ? size : sz);
    _rtapi_free(h, ptr);
    return p;
}

size_t _rtapi_heap_walk_freelist(struct rtapi_heap *h, chunk_t callback, void *user)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    size_t free = 0;
    rtapi_malloc_hdr_t *p, *prevp, *freep = heap_ptr(h,h->free_p);
    prevp = freep;
    for (p = heap_ptr(h,prevp->s.next); ; prevp = p, p = heap_ptr(h,p->s.next)) {
	if (p->s.tag.size && callback != NULL) {
	    callback(p->s.tag.size * sizeof(rtapi_malloc_hdr_t),
		     (void *)(p + 1),
		     user);
	    free += p->s.tag.size;
	}
	if (p == freep) {
	    return free;
	}
    }
}

int _rtapi_heap_addmem(struct rtapi_heap *h, void *space, size_t size)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    if (space < (void*) h) return -EINVAL;

    rtapi_malloc_hdr_t *arena = space;
    arena->s.tag.size = size / sizeof(rtapi_malloc_hdr_t);
    _rtapi_unlocked_free(h, (void *) (arena + 1));
    h->freed -= size;
    h->arena_size += size;
    return 0;
}

int _rtapi_heap_init(struct rtapi_heap *heap, const char *name)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &heap->mutex;
    rtapi_mutex_get(m);

    heap->base.s.next = 0; // because the first element in the heap ist the header
    heap->free_p = 0;      // and free list sentinel
    heap->base.s.tag.size = 0;
    heap->mutex = 0;
    heap->arena_size = 0;
    heap->flags = 0;
    heap->msg_handler = NULL;
    heap->requested = 0;
    heap->allocated = 0;
    heap->freed = 0;
    if (name) 
	strncpy(heap->name, name, sizeof(heap->name));
    else {
#ifdef RTAPI
	rtapi_snprintf(heap->name, sizeof(heap->name),"<%p>", heap);
#else
    snprintf(heap->name, sizeof(heap->name),"<%p>", heap);
#endif
    }
    return 0;
}

int  _rtapi_heap_setflags(struct rtapi_heap *heap, int flags)
{
    int f = heap->flags;
    heap->flags = flags;
    return f;
}

void *_rtapi_heap_setloghdlr(struct rtapi_heap *heap, void  *p)
{
    void *h = heap->msg_handler;
    heap->msg_handler = p;
    return h;
}

size_t _rtapi_heap_status(struct rtapi_heap *h,
			  struct rtapi_heap_stat *hs)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    hs->arena_size = h->arena_size;
    hs->requested = h->requested;
    hs->allocated = h->allocated;
    hs->freed = h->freed;
    hs->total_avail = 0;
    hs->fragments = 0;
    hs->largest = 0;

    rtapi_malloc_hdr_t *p, *prevp, *freep = heap_ptr(h, h->free_p);
    prevp = freep;
    for (p = heap_ptr(h, prevp->s.next); ; prevp = p, p = heap_ptr(h, p->s.next)) {
	if (p->s.tag.size) {
	    hs->fragments++;
	    hs->total_avail += p->s.tag.size;
	    if (p->s.tag.size > hs->largest)
		hs->largest = p->s.tag.size;
	}
	if (p == freep) {
	    hs->total_avail *= sizeof(rtapi_malloc_hdr_t);
	    hs->largest *= sizeof(rtapi_malloc_hdr_t);
	    return hs->largest;
	}
    }
}

