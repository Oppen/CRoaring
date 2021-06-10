#define _GNU_SOURCE
#include <roaring/containers/containers.h>
#include <stdio.h>
#include "benchmark.h"

// NOTE: since the main advantage of this is being usable from a heap, we
// experiment with different key groupings, as we may as well receive our
// input grouped like that.

static unsigned int seed = 123456789;
static const int OUR_RAND_MAX = (1 << 30) - 1;
inline static unsigned int our_rand() {  // we do not want to depend on a system-specific
                                // random number generator
    seed = (1103515245 * seed + 12345);
    return seed & OUR_RAND_MAX;
}

typedef struct {
    size_t N_total;
    size_t N_bitsets;
    size_t N_arrays;
    size_t N_runs;
    const container_t **bitsets;
    const container_t **arrays;
    const container_t **runs;
} type_buckets;

typedef enum {
    ORDER_NONE,
    ORDER_BAR,
    ORDER_BRA,
    ORDER_RBA,
    ORDER_RAB,
    ORDER_ARB,
    ORDER_ABR,
    ORDER_MAX = ORDER_ABR,
} order_e;

static const char *order_names[] = {
    [ORDER_NONE] = "RANDOM",
    [ORDER_BAR] = "BITSET-ARRAY-RUN",
    [ORDER_BRA] = "BITSET-RUN-ARRAY",
    [ORDER_RBA] = "RUN-BITSET-ARRAY",
    [ORDER_RAB] = "RUN-ARRAY-BITSET",
    [ORDER_ARB] = "ARRAY-RUN-BITSET",
    [ORDER_ABR] = "ARRAY-BITSET-RUN",
};

#define SORT_BITSET -1
static int size_compar_bitset(const void *lhs, const void *rhs) {
    const bitset_container_t *b1 = *(bitset_container_t**)lhs;
    const bitset_container_t *b2 = *(bitset_container_t**)rhs;
    return SORT_BITSET*(bitset_container_cardinality(b1) - bitset_container_cardinality(b2));
}
#define SORT_ARRAY -1
static int size_compar_array(const void *lhs, const void *rhs) {
    const array_container_t *a1 = *(array_container_t**)lhs;
    const array_container_t *a2 = *(array_container_t**)rhs;
    return SORT_ARRAY*(array_container_cardinality(a1) - array_container_cardinality(a2));
}
#define SORT_RUN -1
static int size_compar_run(const void *lhs, const void *rhs) {
    const run_container_t *r1 = *(run_container_t**)lhs;
    const run_container_t *r2 = *(run_container_t**)rhs;
    return SORT_RUN*(run_container_cardinality(r1) - run_container_cardinality(r2));
}

static type_buckets *build_buckets(size_t N, const container_t **conts,
                                   const uint8_t *types) {
    size_t size = sizeof(type_buckets) + N*sizeof(conts[0]);
    type_buckets *bs = malloc(size);

    assert(bs != NULL);

    bs->N_total = N;
    bs->N_bitsets = 0;
    bs->N_arrays = 0;
    bs->N_runs = 0;

    for (size_t i = 0; i < N; ++i) {
        assert(1 <= types[i] && types[i] <= 3);

        switch (types[i]) {
        case BITSET_CONTAINER_TYPE:
            bs->N_bitsets++;
            break;
        case ARRAY_CONTAINER_TYPE:
            bs->N_arrays++;
            break;
        case RUN_CONTAINER_TYPE:
            bs->N_runs++;
            break;
        default:
            assert(false);
        }
    }

    bs->bitsets = (const container_t **)(bs+1);
    bs->arrays = bs->bitsets+bs->N_bitsets;
    bs->runs = bs->arrays+bs->N_arrays;

    for (size_t i = 0, b = 0, a = 0, r = 0; i < N; ++i) {
        switch (types[i]) {
        case BITSET_CONTAINER_TYPE:
            bs->bitsets[b++] = conts[i];
            break;
        case ARRAY_CONTAINER_TYPE:
            bs->arrays[a++] = conts[i];
            break;
        case RUN_CONTAINER_TYPE:
            bs->runs[r++] = conts[i];
            break;
        default:
            assert(false);
        }
    }

    assert(N == bs->N_total);
    assert(N == bs->N_bitsets+bs->N_arrays+bs->N_runs);

    qsort(bs->bitsets, bs->N_bitsets, sizeof(container_t*), size_compar_bitset);
    qsort(bs->arrays, bs->N_arrays, sizeof(container_t*), size_compar_array);
    qsort(bs->runs, bs->N_runs, sizeof(container_t*), size_compar_run);

    return bs;
}

static void free_buckets(type_buckets *bs) {
    free(bs);
}

// Returns order if it's an original combination or a lesser equivalent order.
// Used to avoid repeating permutations of empty categories.
static order_e sort_from_buckets(size_t N, container_t **conts, uint8_t *types,
				const type_buckets *bs, order_e order) {
    assert(N == bs->N_total);
    assert(ORDER_NONE <= order && order <= ORDER_MAX);

    if (order == ORDER_NONE) {
        return ORDER_NONE;
    }

    uint8_t flags = ((bs->N_bitsets > 0 ? 1 : 0) << 0) |
                    ((bs->N_arrays  > 0 ? 1 : 0) << 1) |
		    ((bs->N_runs    > 0 ? 1 : 0) << 2);
    order_e min_eq_order[][ORDER_MAX+1] = {
        //     NONE,      BAR,       BRA,       RBA,       RAB,       ARB,       ABR
        //     NONE,      ---,       ---,       ---,       ---,       ---,       ---
        {ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE},
        //     B
        // B   NONE,      B--,       B--,       -B.,       --B,       --B,       -B-
        {ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_BAR},
        //  A  NONE,      -A-,       --A,       --A,       -A-,       A--,       A--
        {ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_BAR},
        // BA  NONE,      BA-,       B-A,       -BA,       -AB,       A-B,       AB-
        {ORDER_NONE,ORDER_BAR, ORDER_BAR, ORDER_BAR, ORDER_RAB, ORDER_RAB, ORDER_RAB},
        //   R NONE,      --R,       -R-,       R--,       R--,       -R-,       --R
        {ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_NONE,ORDER_BAR},
        // B R NONE,      B-R,       BR-,       RB-,       R-B,       -RB,       -BR
        {ORDER_NONE,ORDER_BAR, ORDER_BAR, ORDER_RBA, ORDER_RBA, ORDER_RBA, ORDER_BAR},
        //  AR NONE,      -AR,       -RA,       R-A,       RA-,       AR-,       A-R
        {ORDER_NONE,ORDER_BAR, ORDER_BRA, ORDER_BRA, ORDER_BRA, ORDER_BAR, ORDER_BAR},
        // RAB NONE,      BAR,       BRA,       RBA,       RAB,       ARB,       ABR
        {ORDER_NONE,ORDER_BAR, ORDER_BRA, ORDER_RBA, ORDER_RAB, ORDER_ARB, ORDER_ABR},
    };

    if (order > min_eq_order[flags][order]) {
        return min_eq_order[flags][order];
    }

    switch (order) {
    case ORDER_NONE:
        return ORDER_NONE;
    case ORDER_BAR:
        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        break;
    case ORDER_BRA:
        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        break;
    case ORDER_RBA:
        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        break;
    case ORDER_RAB:
        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        break;
    case ORDER_ARB:
        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        break;
    case ORDER_ABR:
        memset(types, ARRAY_CONTAINER_TYPE, bs->N_arrays);
        types += bs->N_arrays;
        memcpy(conts, bs->arrays, bs->N_arrays*sizeof(conts[0]));
        conts += bs->N_arrays;

        memset(types, BITSET_CONTAINER_TYPE, bs->N_bitsets);
        types += bs->N_bitsets;
        memcpy(conts, bs->bitsets, bs->N_bitsets*sizeof(conts[0]));
        conts += bs->N_bitsets;

        memset(types, RUN_CONTAINER_TYPE, bs->N_runs);
        types += bs->N_runs;
        memcpy(conts, bs->runs, bs->N_runs*sizeof(conts[0]));
        conts += bs->N_runs;

        break;
    default:
        assert(false);
    }

    return order;
}

static container_t *gen_container(double density, uint8_t *type, bool opti,
                                  double p_array, double p_run) {
    // Be lazy, just put enough for the whole range.
    array_container_t *a = array_container_create_given_capacity(1 << 16);
    bitset_container_t *b;
    run_container_t *r;

retry:
    for (int i = 0; i < (1 << 16); ++i) {
        if (our_rand() / (double)OUR_RAND_MAX < density) {
            array_container_append(a, i);
        }
    }

    // Empty run containers cause issues. Easier to
    // force containers to have contents.
    if (a->cardinality == 0) {
        goto retry;
    }

    array_container_shrink_to_fit(a);

    // Do it before any return so all runs have the same seed regardless of opti.
    double rand_type = our_rand() / (double)OUR_RAND_MAX;

    if (opti) {
        container_t *c = convert_run_optimize(a, ARRAY_CONTAINER_TYPE, type);
        return c;
    }

    if (rand_type < p_array) {
        *type = ARRAY_CONTAINER_TYPE;
        return a;
    }
    if (rand_type < p_run || (p_array > 0. && rand_type < p_run+p_array)) {
        *type = RUN_CONTAINER_TYPE;
        r = run_container_from_array(a);
        array_container_free(a);
        return r;
    }
    *type = BITSET_CONTAINER_TYPE;
    b = bitset_container_from_array(a);
    array_container_free(a);
    return b;
}

#define N_REPS 100

static double cycles_per_cont_batch(size_t N, const container_t **conts, const uint8_t *types) {
    uint64_t cycles_start, cycles_final;
    double cycles = 0.;
    uint8_t restype;
    container_t *res;

    for (int r = 0; r < N_REPS; ++r) {
        RDTSC_START(cycles_start);
        res = container_or_many(N, conts, types, &restype);
        RDTSC_FINAL(cycles_final);
        container_free(res, restype);
        cycles += (double)(cycles_final - cycles_start) / N;
    }

    return cycles;
}

static double cycles_per_cont_naive(size_t N, const container_t **conts, const uint8_t *types) {
    uint64_t cycles_start, cycles_final;
    double cycles = .0;
    uint8_t restype, tmptype;
    container_t *res, *tmp;

    for (int r = 0; r < N_REPS; ++r) {
        RDTSC_START(cycles_start);
	restype = types[0];
        res = container_clone(conts[0], types[0]);
        for (int i = 1; i < N; i++) {
            // The library forces catching this upstream, so it's fair to include in the timing.
            if (container_is_full(res, restype)) {
                break;
            }
            tmp = container_ior(res, restype, conts[i], types[i], &tmptype);
            if (tmp != res) {
                container_free(res, restype);
            }
            res = tmp;
            restype = tmptype;
        }
        RDTSC_FINAL(cycles_final);
        container_free(res, restype);
        cycles += (double)(cycles_final - cycles_start) / N;
    }

    return cycles;
}

#define N_CONTS 250

void benchmark(bool opti, double d, double p_array, double p_run) {
    static const container_t *conts[N_CONTS];
    static uint8_t types[N_CONTS];

    type_buckets *bs;

    // Generate the inputs.
    for (int i = 0; i < N_CONTS; i++) {
        conts[i] = gen_container(d, &types[i], opti, p_array, p_run);
        assert(conts[i] != NULL);
	assert(!container_is_full(conts[i], types[i]));
    }

    // And the buckets to test different orderings.
    bs = build_buckets(N_CONTS, conts, types);
    printf("========================================\n");
    printf("RUNNING BENCHMARK FOR %sOPTIMIZED CONTAINERS\n", opti ? "" : "NON-");
    printf("DENSITY: %f - BITSETS: %lu - ARRAYS: %lu - RUNS: %lu\n",
           d, bs->N_bitsets, bs->N_arrays, bs->N_runs);
    printf("----------------------------------------\n");

    for (order_e order = ORDER_NONE; order <= ORDER_MAX; ++order) {
        // Apply the relevant ordering.
        order_e eq = sort_from_buckets(N_CONTS, conts, types, bs, order);
        if (eq != order) {
            continue;
        }

        printf("----------------------------------------\n");
        printf("Comparing for order: %s\n", order_names[order]);
        printf("%f cycles per union (batch)\n",
               cycles_per_cont_batch(N_CONTS, conts, types));

        printf("%f cycles per union (naive)\n",
               cycles_per_cont_naive(N_CONTS, conts, types));
    }
    printf("========================================\n\n\n");

    // Free the buckets.
    free_buckets(bs);

    // Cleanup the inputs.
    for (int i = 0; i < N_CONTS; i++) {
        container_free(conts[i], types[i]);
    }
}

int main() {
    double densities[] = {.0001, .001, .01, .05, .10, .25, .50, .70, .90, .99, .999, .9999};

    for (size_t i = 0; i < sizeof(densities)/sizeof(densities[0]); ++i) {
        double d = densities[i];

        seed = 123456789;
        //benchmark(true, d, -1., -1.);

        double p_types[][3] = {
            //{0.000, 0.000, 1.000},
            //{0.000, 1.000, 0.000},
            //{1.000, 0.000, 0.000},
            //{0.000, 0.500, 0.500},
	    //{0.500, 0.000, 0.500},
	    //{0.500, 0.500, 0.000},
	    {0.333, 0.333, 0.333},
        };

        for (size_t j = 0; j < sizeof(p_types)/sizeof(p_types[0]); ++j) {
            seed = 123456789;
            benchmark(false, d, p_types[j][1], p_types[j][2]);
        }
    }
    return 0;
}
