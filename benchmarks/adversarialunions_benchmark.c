#define _GNU_SOURCE
#include <roaring/roaring.h>
#include <stdio.h>
#include "benchmark.h"
int quickfull() {
    printf("The naive approach works well when the bitmaps quickly become full\n");
    uint64_t cycles_start, cycles_final;
    size_t bitmapcount = 100;
    size_t size = 1000000;
    roaring_bitmap_t **bitmaps =
        (roaring_bitmap_t **)malloc(sizeof(roaring_bitmap_t *) * bitmapcount);
    for (size_t i = 0; i < bitmapcount; i++) {
        bitmaps[i] = roaring_bitmap_from_range(0, 1000000, 1);
        for (size_t j = 0; j < size / 20; j++)
            roaring_bitmap_remove(bitmaps[i], rand() % size);
        roaring_bitmap_run_optimize(bitmaps[i]);
    }

    while (true) {
        RDTSC_START(cycles_start);
        roaring_bitmap_t *answer0 = roaring_bitmap_or_many_heap(bitmapcount, (const roaring_bitmap_t **)bitmaps);
        RDTSC_FINAL(cycles_final);
        printf("%f cycles per union (many heap) \n",
               (cycles_final - cycles_start) * 1.0 / bitmapcount);
        roaring_bitmap_free(answer0);
    }

    for (size_t i = 0; i < bitmapcount; i++) {
        roaring_bitmap_free(bitmaps[i]);
    }
    free(bitmaps);
    return 0;
}

int notsofull() {
    printf("The naive approach works less well when the bitmaps do not quickly become full\n");
    uint64_t cycles_start, cycles_final;
    size_t bitmapcount = 100;
    size_t size = 1000000;
    roaring_bitmap_t **bitmaps =
        (roaring_bitmap_t **)malloc(sizeof(roaring_bitmap_t *) * bitmapcount);
    for (size_t i = 0; i < bitmapcount; i++) {
        bitmaps[i] = roaring_bitmap_from_range(0, 1000000, 100);
        for (size_t j = 0; j < size / 20; j++)
            roaring_bitmap_remove(bitmaps[i], rand() % size);
        roaring_bitmap_run_optimize(bitmaps[i]);
    }

    while (true) {
        RDTSC_START(cycles_start);
        roaring_bitmap_t *answer0 = roaring_bitmap_or_many_heap(bitmapcount, (const roaring_bitmap_t **)bitmaps);
        RDTSC_FINAL(cycles_final);
        printf("%f cycles per union (many heap) \n",
               (cycles_final - cycles_start) * 1.0 / bitmapcount);
        roaring_bitmap_free(answer0);
    }

    for (size_t i = 0; i < bitmapcount; i++) {
        roaring_bitmap_free(bitmaps[i]);
    }
    free(bitmaps);
    return 0;
}


int main() {
    printf("How to best aggregate the bitmaps is data-sensitive.\n");
    while (true) {
        quickfull();
        notsofull();
    }
    return 0;
}
