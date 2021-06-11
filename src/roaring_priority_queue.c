#include <roaring/roaring.h>
#include <roaring/roaring_array.h>


#ifdef __cplusplus
using namespace ::roaring::internal;

extern "C" { namespace roaring { namespace api {
#endif

struct roaring_pq_element_s {
    roaring_bitmap_t *bitmap;
    uint16_t idx;
};

typedef struct roaring_pq_element_s roaring_pq_element_t;

struct roaring_pq_s {
    roaring_pq_element_t *elements;
    uint64_t size;
};

typedef struct roaring_pq_s roaring_pq_t;

// TODO: evaluate in same order in different loops.
static inline bool compare(roaring_pq_element_t *e1, roaring_pq_element_t *e2) {
    int k1, k2;
    roaring_array_t *ra1 = &e1->bitmap->high_low_container;
    roaring_array_t *ra2 = &e2->bitmap->high_low_container;

    k1 = ra_get_key_at_index(ra1, e1->idx);
    k2 = ra_get_key_at_index(ra2, e2->idx);

    if (k1 < k2) {
        return true;
    }

    uint8_t t1, t2;
    int w1, w2;

    const container_t *c1, *c2;

    const bitset_container_t *b1, *b2;
    const run_container_t *r1, *r2;
    const array_container_t *a1, *a2;

    // We cast eagerly for brevity, but use with care!
    c1 = ra_get_container_at_index(ra1, e1->idx, &t1);
    c1 = container_unwrap_shared(c1, &t1);
    b1 = const_CAST_bitset(c1);
    a1 = const_CAST_array(c1);
    r1 = const_CAST_run(c1);

    c2 = ra_get_container_at_index(ra2, e2->idx, &t2);
    c2 = container_unwrap_shared(c2, &t2);
    b2 = const_CAST_bitset(c2);
    a2 = const_CAST_array(c2);
    r2 = const_CAST_run(c2);

    // If there's any confirmed full container for the key we want to process it
    // first so we can have lunch early.
    if (container_is_full(c1, t1)) {
        return true;
    }
    if (container_is_full(c2, t2)) {
        return false;
    }

    // Otherwise, always put bitsets with undetermined size; we'll need to
    // traverse them to decide anything, and merging two bitsets is probably
    // faster than merging one with something else.
    if (t1 == BITSET_CONTAINER_TYPE &&
        bitset_container_cardinality(b1) == BITSET_UNKNOWN_CARDINALITY) {
        return true;
    }
    if (t1 == BITSET_CONTAINER_TYPE &&
        bitset_container_cardinality(b1) == BITSET_UNKNOWN_CARDINALITY) {
        return false;
    }

    // Lastly, having the bigger sets first makes us less likely to need
    // reallocations. We use ballpark approximations here to avoid making
    // the comparison too slow.

    switch (t1) {
    case BITSET_CONTAINER_TYPE:
        // This is exact, since unknown cardinalities cause early returns.
        w1 = bitset_container_cardinality(b1);
        break;
    case ARRAY_CONTAINER_TYPE:
        // This is exact, since arrays need it for indexing.
        w1 = array_container_cardinality(a1);
        break;
    case RUN_CONTAINER_TYPE:
        // This is ballpark, for runs we always need to compute and it may be expensive.
        w1 = run_container_size_in_bytes(a1);
        if (r1->n_runs <= 16) {
            // This should be fast enough; 16 words == 1 round of SIMD, with luck.
            w1 = run_container_cardinality(r1);
        }
        break;
    }
    switch (t2) {
    case BITSET_CONTAINER_TYPE:
        // This is exact, since unknown cardinalities cause early returns.
        w2 = bitset_container_cardinality(b2);
        break;
    case ARRAY_CONTAINER_TYPE:
        // This is exact, since arrays need it for indexing.
        w2 = array_container_cardinality(a2);
        break;
    case RUN_CONTAINER_TYPE:
        // This is ballpark, for runs we always need to compute and it may be expensive.
        w2 = run_container_size_in_bytes(a2);
        if (r2->n_runs <= 16) {
            // This should be fast enough; 16 words == 1 round of SIMD, with luck.
            w2 = run_container_cardinality(r2);
        }
        break;
    }

    return w1 < w2;
    //c1 = container_get_cardinality(c, t);
    // Ballpark approximation, faster; may want to check cardinality for bitmaps.
    //c1 = container_size_in_bytes(c, t);

    //c2 = container_get_cardinality(c, t);
    // Ballpark approximation, faster; may want to check cardinality for bitmaps.
    //c2 = container_size_in_bytes(c, t);

    // We want the biggest ones first, as merging into
    // the bigger container tends to be faster.
    return c1 > c2;
}

static void pq_add(roaring_pq_t *pq, roaring_pq_element_t *t) {
    uint64_t i = pq->size;
    pq->elements[pq->size++] = *t;
    while (i > 0) {
        uint64_t p = (i - 1) >> 1;
        roaring_pq_element_t ap = pq->elements[p];
        if (!compare(t, &ap)) break;
        pq->elements[i] = ap;
        i = p;
    }
    pq->elements[i] = *t;
}

static void pq_free(roaring_pq_t *pq) {
    free(pq);
}

static void percolate_down(roaring_pq_t *pq, uint32_t i) {
    uint32_t size = (uint32_t)pq->size;
    uint32_t hsize = size >> 1;
    roaring_pq_element_t ai = pq->elements[i];
    while (i < hsize) {
        uint32_t l = (i << 1) + 1;
        uint32_t r = l + 1;
        roaring_pq_element_t bestc = pq->elements[l];
        if (r < size) {
            if (compare(pq->elements + r, &bestc)) {
                l = r;
                bestc = pq->elements[r];
            }
        }
        if (!compare(&bestc, &ai)) {
            break;
        }
        pq->elements[i] = bestc;
        i = l;
    }
    pq->elements[i] = ai;
}

static roaring_pq_t *create_pq(const roaring_bitmap_t **arr, uint32_t length) {
    size_t alloc_size = sizeof(roaring_pq_t) + sizeof(roaring_pq_element_t) * length;
    roaring_pq_t *answer = (roaring_pq_t *)malloc(alloc_size);
    answer->elements = (roaring_pq_element_t *)(answer + 1);
    answer->size = 0;
    for (uint32_t i = 0; i < length; i++) {
        if (arr[i]->high_low_container.size == 0) {
            continue;
        }
        answer->size++;
        answer->elements[i].bitmap = (roaring_bitmap_t *)arr[i];
        answer->elements[i].idx = 0;
    }
    for (int32_t i = (answer->size >> 1); i >= 0; i--) {
        percolate_down(answer, i);
    }
    return answer;
}

static roaring_pq_element_t pq_peek(roaring_pq_t *pq) {
    return pq->elements[0];
}

static void pq_pop(roaring_pq_t *pq) {
    if (pq->size > 1) {
        pq->elements[0] = pq->elements[--pq->size];
        percolate_down(pq, 0);
    } else
        --pq->size;
}

static roaring_pq_element_t pq_poll(roaring_pq_t *pq) {
    roaring_pq_element_t ans = pq->elements[0];
    if (pq->size > 1) {
        pq->elements[0] = pq->elements[--pq->size];
        percolate_down(pq, 0);
    } else
        --pq->size;
    // memmove(pq->elements,pq->elements+1,(pq->size-1)*sizeof(roaring_pq_element_t));--pq->size;
    return ans;
}

/**
 * Compute the union of 'number' bitmaps using a heap. This can
 * sometimes be faster than roaring_bitmap_or_many which uses
 * a naive algorithm. Caller is responsible for freeing the
 * result.
 */
roaring_bitmap_t *roaring_bitmap_or_many_heap(uint32_t number,
                                              const roaring_bitmap_t **x) {
	if (number == 0) {
		return roaring_bitmap_create();
	}
	if (number == 1) {
		return roaring_bitmap_copy(x[0]);
	}
	roaring_bitmap_t *answer = roaring_bitmap_create();
	roaring_array_t *ans_ra = &answer->high_low_container;
	roaring_pq_t *pq = create_pq(x, number);
	int pre_k = -1;
	uint8_t pre_t;
	container_t *pre_c = NULL;
	while (pq->size > 1) {
		// TODO: skip to next key when full.
		roaring_array_t *ra;
		roaring_pq_element_t x;
		int k;
		uint8_t t;
		container_t *c;

		x = pq_peek(pq);
		ra = &x.bitmap->high_low_container;
		k = ra_get_key_at_index(ra, x.idx);
		c = ra_get_container_at_index(ra, x.idx, &t);

		if (x.idx == ra_get_size(ra)-1) {
			pq_pop(pq);
		} else {
			x.idx++;
			pq->elements[0] = x;
			percolate_down(pq, 0);
		}

		if (k != pre_k) {
			if (pre_c != NULL) {
				ra_append(ans_ra, pre_k, pre_c, pre_t);
			}
			pre_c = get_copy_of_container(c, &t, false);
			pre_t = t;
			pre_k = k;
		} else {
			c = container_lazy_ior(pre_c, pre_t, c, t, &t);
			if (pre_c != c) {
				container_free(pre_c, pre_t);
				pre_c = c;
			}
			pre_t = t;
		}
	}
	if (pre_c != NULL) {
		ra_append(ans_ra, pre_k, pre_c, pre_t);
	}
	roaring_bitmap_repair_after_lazy(answer);
	pq_free(pq);
	return answer;
}

#ifdef __cplusplus
} } }  // extern "C" { namespace roaring { namespace api {
#endif
