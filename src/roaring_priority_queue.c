#include <roaring/roaring.h>
#include <roaring/roaring_array.h>


#ifdef __cplusplus
using namespace ::roaring::internal;

extern "C" { namespace roaring { namespace api {
#endif

struct roaring_pq_element_s {
    roaring_bitmap_t *bitmap;
    uint16_t idx;
    uint16_t key;
    uint8_t type;
};

typedef struct roaring_pq_element_s roaring_pq_element_t;

struct roaring_pq_s {
    roaring_pq_element_t *elements;
    uint64_t size;
};

typedef struct roaring_pq_s roaring_pq_t;

inline bool compare(roaring_pq_element_t *t1, roaring_pq_element_t *t2) {
    return t1->key < t2->key || (t1->key == t2->key && t1->type > t2->type);
}

void pq_add(roaring_pq_t *pq, roaring_pq_element_t *t) {
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

void pq_free(roaring_pq_t *pq) {
    free(pq);
}

void percolate_down(roaring_pq_t *pq, uint32_t i) {
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

roaring_pq_t *create_pq(const roaring_bitmap_t **arr, uint32_t length) {
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
        answer->elements[i].key = ra_get_key_at_index(&arr[i]->high_low_container, i);
        ra_get_container_at_index(&arr[i]->high_low_container, i, &answer->elements[i].type);
    }
    for (int32_t i = (answer->size >> 1); i >= 0; i--) {
        percolate_down(answer, i);
    }
    return answer;
}

roaring_pq_element_t pq_peek(roaring_pq_t *pq) {
    return pq->elements[0];
}

void pq_pop(roaring_pq_t *pq) {
    if (pq->size > 1) {
        pq->elements[0] = pq->elements[--pq->size];
        percolate_down(pq, 0);
    } else
        --pq->size;
}

roaring_pq_element_t pq_poll(roaring_pq_t *pq) {
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
// TODO: optimize full containers.
// TODO: use a single bitset.
roaring_bitmap_t *roaring_bitmap_or_many_heap(uint32_t number,
                                              const roaring_bitmap_t **x) {
	if (number == 0) {
		return roaring_bitmap_create();
	}
	if (number == 1) {
		return roaring_bitmap_copy(x[0]);
	}
	roaring_bitmap_t *answer = roaring_bitmap_create();
	roaring_pq_t *pq = create_pq(x, number);
	roaring_pq_element_t x1;
	int key = -1;
	uint8_t type, xtype;
	container_t *c, *xc;
	while (pq->size > 1) {
		x1 = pq_peek(pq);
		xc = ra_get_container_at_index(&x1.bitmap->high_low_container, x1.idx, &xtype);

		if (x1.idx < x1.bitmap->high_low_container.size-1) {
			roaring_pq_element_t y = {
				.bitmap = x1.bitmap,
				.key = ra_get_key_at_index(&x1.bitmap->high_low_container, x1.idx+1),
				.idx = x1.idx+1,
			};
			ra_get_container_at_index(&y.bitmap->high_low_container, y.idx, &y.type);
			pq->elements[0] = y;
			percolate_down(pq, 0);
		} else {
			pq_pop(pq);
		}
		
		if (key != x1.key) {
			if (key != -1) {
				ra_append(&answer->high_low_container, key, c, type);
			}
			key = x1.key;
			type = xtype;
			c = container_clone(xc, xtype);
			continue;
		}

		container_t *new_c;
		uint8_t new_type;
		new_c = container_lazy_ior(c, type, xc, xtype, &new_type);
		if (c != new_c) {
			container_free(c, type);
		}
		c = new_c;
		type = new_type;
	}
	ra_append(&answer->high_low_container, key, c, type);
	roaring_bitmap_repair_after_lazy(answer);
	pq_free(pq);
	return answer;
}

#ifdef __cplusplus
} } }  // extern "C" { namespace roaring { namespace api {
#endif
