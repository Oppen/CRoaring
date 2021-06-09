/*
 * mixed_union.c
 *
 */

#include <assert.h>
#include <string.h>

#include <roaring/bitset_util.h>
#include <roaring/containers/convert.h>
#include <roaring/containers/mixed_union.h>
#include <roaring/containers/perfparameters.h>

#include <roaring/containers/containers.h>

#ifdef __cplusplus
extern "C" { namespace roaring { namespace internal {
#endif

/* Compute the union of src_1 and src_2 and write the result to
 * dst.  */
void array_bitset_container_union(const array_container_t *src_1,
                                  const bitset_container_t *src_2,
                                  bitset_container_t *dst) {
    if (src_2 != dst) bitset_container_copy(src_2, dst);
    dst->cardinality = (int32_t)bitset_set_list_withcard(
        dst->words, dst->cardinality, src_1->array, src_1->cardinality);
}

/* Compute the union of src_1 and src_2 and write the result to
 * dst. It is allowed for src_2 to be dst.  This version does not
 * update the cardinality of dst (it is set to BITSET_UNKNOWN_CARDINALITY). */
void array_bitset_container_lazy_union(const array_container_t *src_1,
                                       const bitset_container_t *src_2,
                                       bitset_container_t *dst) {
    if (src_2 != dst) bitset_container_copy(src_2, dst);
    bitset_set_list(dst->words, src_1->array, src_1->cardinality);
    dst->cardinality = BITSET_UNKNOWN_CARDINALITY;
}

void run_bitset_container_union(const run_container_t *src_1,
                                const bitset_container_t *src_2,
                                bitset_container_t *dst) {
    assert(!run_container_is_full(src_1));  // catch this case upstream
    if (src_2 != dst) bitset_container_copy(src_2, dst);
    for (int32_t rlepos = 0; rlepos < src_1->n_runs; ++rlepos) {
        rle16_t rle = src_1->runs[rlepos];
        bitset_set_lenrange(dst->words, rle.value, rle.length);
    }
    dst->cardinality = bitset_container_compute_cardinality(dst);
}

void run_bitset_container_lazy_union(const run_container_t *src_1,
                                     const bitset_container_t *src_2,
                                     bitset_container_t *dst) {
    assert(!run_container_is_full(src_1));  // catch this case upstream
    if (src_2 != dst) bitset_container_copy(src_2, dst);
    for (int32_t rlepos = 0; rlepos < src_1->n_runs; ++rlepos) {
        rle16_t rle = src_1->runs[rlepos];
        bitset_set_lenrange(dst->words, rle.value, rle.length);
    }
    dst->cardinality = BITSET_UNKNOWN_CARDINALITY;
}

// why do we leave the result as a run container??
void array_run_container_union(const array_container_t *src_1,
                               const run_container_t *src_2,
                               run_container_t *dst) {
    if (run_container_is_full(src_2)) {
        run_container_copy(src_2, dst);
        return;
    }
    // TODO: see whether the "2*" is spurious
    run_container_grow(dst, 2 * (src_1->cardinality + src_2->n_runs), false);
    int32_t rlepos = 0;
    int32_t arraypos = 0;
    rle16_t previousrle;
    if (src_2->runs[rlepos].value <= src_1->array[arraypos]) {
        previousrle = run_container_append_first(dst, src_2->runs[rlepos]);
        rlepos++;
    } else {
        previousrle =
            run_container_append_value_first(dst, src_1->array[arraypos]);
        arraypos++;
    }
    while ((rlepos < src_2->n_runs) && (arraypos < src_1->cardinality)) {
        if (src_2->runs[rlepos].value <= src_1->array[arraypos]) {
            run_container_append(dst, src_2->runs[rlepos], &previousrle);
            rlepos++;
        } else {
            run_container_append_value(dst, src_1->array[arraypos],
                                       &previousrle);
            arraypos++;
        }
    }
    if (arraypos < src_1->cardinality) {
        while (arraypos < src_1->cardinality) {
            run_container_append_value(dst, src_1->array[arraypos],
                                       &previousrle);
            arraypos++;
        }
    } else {
        while (rlepos < src_2->n_runs) {
            run_container_append(dst, src_2->runs[rlepos], &previousrle);
            rlepos++;
        }
    }
}

void array_run_container_inplace_union(const array_container_t *src_1,
                                       run_container_t *src_2) {
    if (run_container_is_full(src_2)) {
        return;
    }
    const int32_t maxoutput = src_1->cardinality + src_2->n_runs;
    const int32_t neededcapacity = maxoutput + src_2->n_runs;
    if (src_2->capacity < neededcapacity)
        run_container_grow(src_2, neededcapacity, true);
    memmove(src_2->runs + maxoutput, src_2->runs,
            src_2->n_runs * sizeof(rle16_t));
    rle16_t *inputsrc2 = src_2->runs + maxoutput;
    int32_t rlepos = 0;
    int32_t arraypos = 0;
    int src2nruns = src_2->n_runs;
    src_2->n_runs = 0;

    rle16_t previousrle;

    if (inputsrc2[rlepos].value <= src_1->array[arraypos]) {
        previousrle = run_container_append_first(src_2, inputsrc2[rlepos]);
        rlepos++;
    } else {
        previousrle =
            run_container_append_value_first(src_2, src_1->array[arraypos]);
        arraypos++;
    }

    while ((rlepos < src2nruns) && (arraypos < src_1->cardinality)) {
        if (inputsrc2[rlepos].value <= src_1->array[arraypos]) {
            run_container_append(src_2, inputsrc2[rlepos], &previousrle);
            rlepos++;
        } else {
            run_container_append_value(src_2, src_1->array[arraypos],
                                       &previousrle);
            arraypos++;
        }
    }
    if (arraypos < src_1->cardinality) {
        while (arraypos < src_1->cardinality) {
            run_container_append_value(src_2, src_1->array[arraypos],
                                       &previousrle);
            arraypos++;
        }
    } else {
        while (rlepos < src2nruns) {
            run_container_append(src_2, inputsrc2[rlepos], &previousrle);
            rlepos++;
        }
    }
}

bool array_array_container_union(
    const array_container_t *src_1, const array_container_t *src_2,
    container_t **dst
){
    int totalCardinality = src_1->cardinality + src_2->cardinality;
    if (totalCardinality <= DEFAULT_MAX_SIZE) {
        *dst = array_container_create_given_capacity(totalCardinality);
        if (*dst != NULL) {
            array_container_union(src_1, src_2, CAST_array(*dst));
        } else {
            return true; // otherwise failure won't be caught
        }
        return false;  // not a bitset
    }
    *dst = bitset_container_create();
    bool returnval = true;  // expect a bitset
    if (*dst != NULL) {
        bitset_container_t *ourbitset = CAST_bitset(*dst);
        bitset_set_list(ourbitset->words, src_1->array, src_1->cardinality);
        ourbitset->cardinality = (int32_t)bitset_set_list_withcard(
            ourbitset->words, src_1->cardinality, src_2->array,
            src_2->cardinality);
        if (ourbitset->cardinality <= DEFAULT_MAX_SIZE) {
            // need to convert!
            *dst = array_container_from_bitset(ourbitset);
            bitset_container_free(ourbitset);
            returnval = false;  // not going to be a bitset
        }
    }
    return returnval;
}

bool array_array_container_inplace_union(
    array_container_t *src_1, const array_container_t *src_2,
    container_t **dst
){
    int totalCardinality = src_1->cardinality + src_2->cardinality;
    *dst = NULL;
    if (totalCardinality <= DEFAULT_MAX_SIZE) {
        if(src_1->capacity < totalCardinality) {
          *dst = array_container_create_given_capacity(2  * totalCardinality); // be purposefully generous
          if (*dst != NULL) {
              array_container_union(src_1, src_2, CAST_array(*dst));
          } else {
              return true; // otherwise failure won't be caught
          }
          return false;  // not a bitset
        } else {
          memmove(src_1->array + src_2->cardinality, src_1->array, src_1->cardinality * sizeof(uint16_t));
          src_1->cardinality = (int32_t)union_uint16(src_1->array + src_2->cardinality, src_1->cardinality,
                                  src_2->array, src_2->cardinality, src_1->array);
          return false; // not a bitset
        }
    }
    *dst = bitset_container_create();
    bool returnval = true;  // expect a bitset
    if (*dst != NULL) {
        bitset_container_t *ourbitset = CAST_bitset(*dst);
        bitset_set_list(ourbitset->words, src_1->array, src_1->cardinality);
        ourbitset->cardinality = (int32_t)bitset_set_list_withcard(
            ourbitset->words, src_1->cardinality, src_2->array,
            src_2->cardinality);
        if (ourbitset->cardinality <= DEFAULT_MAX_SIZE) {
            // need to convert!
            if(src_1->capacity < ourbitset->cardinality) {
              array_container_grow(src_1, ourbitset->cardinality, false);
            }

            bitset_extract_setbits_uint16(ourbitset->words, BITSET_CONTAINER_SIZE_IN_WORDS,
                                  src_1->array, 0);
            src_1->cardinality =  ourbitset->cardinality;
            *dst = src_1;
            bitset_container_free(ourbitset);
            returnval = false;  // not going to be a bitset
        }
    }
    return returnval;
}


bool array_array_container_lazy_union(
    const array_container_t *src_1, const array_container_t *src_2,
    container_t **dst
){
    int totalCardinality = src_1->cardinality + src_2->cardinality;
    if (totalCardinality <= ARRAY_LAZY_LOWERBOUND) {
        *dst = array_container_create_given_capacity(totalCardinality);
        if (*dst != NULL) {
            array_container_union(src_1, src_2, CAST_array(*dst));
        } else {
              return true; // otherwise failure won't be caught
        }
        return false;  // not a bitset
    }
    *dst = bitset_container_create();
    bool returnval = true;  // expect a bitset
    if (*dst != NULL) {
        bitset_container_t *ourbitset = CAST_bitset(*dst);
        bitset_set_list(ourbitset->words, src_1->array, src_1->cardinality);
        bitset_set_list(ourbitset->words, src_2->array, src_2->cardinality);
        ourbitset->cardinality = BITSET_UNKNOWN_CARDINALITY;
    }
    return returnval;
}


bool array_array_container_lazy_inplace_union(
    array_container_t *src_1, const array_container_t *src_2,
    container_t **dst
){
    int totalCardinality = src_1->cardinality + src_2->cardinality;
    *dst = NULL;
    if (totalCardinality <= ARRAY_LAZY_LOWERBOUND) {
        if(src_1->capacity < totalCardinality) {
          *dst = array_container_create_given_capacity(2  * totalCardinality); // be purposefully generous
          if (*dst != NULL) {
              array_container_union(src_1, src_2, CAST_array(*dst));
          } else {
            return true; // otherwise failure won't be caught
          }
          return false;  // not a bitset
        } else {
          memmove(src_1->array + src_2->cardinality, src_1->array, src_1->cardinality * sizeof(uint16_t));
          src_1->cardinality = (int32_t)union_uint16(src_1->array + src_2->cardinality, src_1->cardinality,
                                  src_2->array, src_2->cardinality, src_1->array);
          return false; // not a bitset
        }
    }
    *dst = bitset_container_create();
    bool returnval = true;  // expect a bitset
    if (*dst != NULL) {
        bitset_container_t *ourbitset = CAST_bitset(*dst);
        bitset_set_list(ourbitset->words, src_1->array, src_1->cardinality);
        bitset_set_list(ourbitset->words, src_2->array, src_2->cardinality);
        ourbitset->cardinality = BITSET_UNKNOWN_CARDINALITY;
    }
    return returnval;
}

// TODO: shrink to fit.
container_t* _container_or_many(int number, container_t * const *containers,
                                             uint8_t *types, uint8_t *result_type) {
	bitset_container_t *bitset = NULL;
	array_container_t *array = NULL;
	run_container_t *run = NULL;
	container_t *answer = NULL;
	uint8_t res_type = 0;

	if (number == 0) {
		return NULL;
	}
	if (number == 1) {
		*result_type = types[0];
		return container_clone(containers[0], types[0]);
	}
	for (int i = 0; i < number; ++i) {
		if (container_is_full(containers[i], types[i])) {
			*result_type = RUN_CONTAINER_TYPE;
			return run_container_create_range(0, (1 << 16));
		}
	}
	// TODO: check for fullness where reasonable (i.e. mostly runs).
	for (int i = 0; i < number; ++i) {
		uint8_t type = types[i];
		const container_t *c = container_unwrap_shared(containers[i], &type);
		// There will be at most 3 copies, so I think we can afford them.
		switch (type) {
		case BITSET_CONTAINER_TYPE: {
			if (bitset == NULL) {
				bitset = bitset_container_clone(const_CAST_bitset(c));
				break;
			}
			bitset_container_or_nocard(bitset, const_CAST_bitset(c), bitset);
			break;
		}
		case ARRAY_CONTAINER_TYPE: {
			// If we're already dealing with bitset containers we can
			// avoid pointless allocs and copies by just merging on it.
			/*
			if (bitset != NULL) {
				// TODO: lazy.
				array_bitset_container_union(const_CAST_array(c), bitset, bitset);
				break;
			}
			*/
			if (array == NULL) {
				array = array_container_create_given_capacity(DEFAULT_MAX_SIZE);
				array_container_copy(const_CAST_array(c), array);
				break;
			}
			container_t *dst = NULL;
			if (array_array_container_lazy_inplace_union(array, const_CAST_array(c), &dst)) {
				if (dst == NULL) {
					// Indicates failure.
					if (bitset != NULL) {
						bitset_container_free(bitset);
					}
					if (array != NULL) {
						array_container_free(array);
					}
					if (run != NULL) {
						run_container_free(run);
					}
					return NULL;
				}
				// bitset == NULL because of early break.
				bitset = CAST_bitset(dst);
				array_container_free(array);
				array = NULL;
				break;
			}
			// Reallocated? Shouldn't happen because we allocated the max.
			if (dst != NULL) {
				array_container_free(array);
				array = CAST_array(dst);
			}
			break;
		}
		case RUN_CONTAINER_TYPE: {
			if (run == NULL) {
				run = run_container_create_given_capacity(DEFAULT_MAX_SIZE);
				run_container_copy(const_CAST_run(c), run);
				break;
			}
			// TODO: lazy.
			run_container_union_inplace(run, const_CAST_run(c));
			break;
			if (run_container_is_full(run)) {
				if (array != NULL) {
					array_container_free(array);
				}
				if (bitset != NULL) {
					bitset_container_free(bitset);
				}
				*result_type = RUN_CONTAINER_TYPE;
				return run;
			}
			break;
		}
		default:
			assert(false);
			__builtin_unreachable();
			return NULL;
		}
		/* FULL SET OPTIMIZATION; computing this every time may be more expensive than it's worth...
		if ((array != NULL && array_container_full(array)) ||
				(bitset != NULL && bitset_container_cardinality(bitset) == (1 << 16)) ||
				(run != NULL && run_container_is_full(run))) {
			*result_type = RUN_CONTAINER_TYPE;
			if (array != NULL) {
				array_container_free(array);
			}
			if (bitset != NULL) {
				bitset_container_free(bitset);
			}
			if (run != NULL) {
				run_container_add_range(run, 0, (1 << 16));
				return run;
			}
			return run_container_create_range(0, (1 << 16));
		}
		*/
	}

	if (bitset != NULL) {
		if (array != NULL) {
			array_bitset_container_lazy_union(array, bitset, bitset);
			array_container_free(array);
		}
		if (run != NULL) {
			run_bitset_container_lazy_union(run, bitset, bitset);
			run_container_free(run);
		}
		answer = bitset;
		res_type = BITSET_CONTAINER_TYPE;
	} else if (array != NULL) {
		if (run != NULL) {
			array_run_container_inplace_union(array, run);
			array_container_free(array);
			answer = run;
			res_type = RUN_CONTAINER_TYPE;
		} else {
			answer = array;
			res_type = ARRAY_CONTAINER_TYPE;
		}
	} else if (run != NULL) {
		answer = run;
		res_type = RUN_CONTAINER_TYPE;
	} else {
		assert(false);
		__builtin_unreachable();
		return NULL;
	}

	return convert_run_optimize(answer, res_type, result_type);
}

// TODO: shrink to fit.
container_t* container_or_many(int number, container_t * const *containers,
                                             uint8_t *types, uint8_t *result_type) {
	bitset_container_t *bitset = NULL;
	run_container_t *run = NULL;
	container_t *answer = NULL;
	uint8_t res_type = 0;

	if (number == 0) {
		return NULL;
	}
	if (number == 1) {
		*result_type = types[0];
		return container_clone(containers[0], types[0]);
	}
	for (int i = 0; i < number; ++i) {
		if (container_is_full(containers[i], types[i])) {
			*result_type = RUN_CONTAINER_TYPE;
			return run_container_create_range(0, (1 << 16));
		}
	}
	// TODO: check for fullness where reasonable (i.e. mostly runs).
	for (int i = 0; i < number; ++i) {
		uint8_t type = types[i];
		const container_t *c = container_unwrap_shared(containers[i], &type);
		
		if (!container_nonzero_cardinality(c, type)) {
			continue;
		}

		// There will be at most 3 copies, so I think we can afford them.
		switch (type) {
		case BITSET_CONTAINER_TYPE: {
			if (bitset == NULL) {
				bitset = bitset_container_clone(const_CAST_bitset(c));
				break;
			}
			bitset_container_or_nocard(bitset, const_CAST_bitset(c), bitset);
			break;
		}
		case ARRAY_CONTAINER_TYPE: {
			// Always use bitsets only for arrays.
			if (bitset == NULL) {
				bitset = bitset_container_from_array(const_CAST_array(c));
				break;
			}
			array_bitset_container_lazy_union(const_CAST_array(c), bitset, bitset);
			break;
		}
		case RUN_CONTAINER_TYPE: {
			if (run == NULL) {
				run = run_container_create_given_capacity(DEFAULT_MAX_SIZE);
				run_container_copy(const_CAST_run(c), run);
				break;
			}
			// TODO: lazy.
			run_container_union_inplace(run, const_CAST_run(c));
			if (run_container_is_full(run)) {
				if (bitset != NULL) {
					bitset_container_free(bitset);
				}
				*result_type = RUN_CONTAINER_TYPE;
				return run;
			}
			break;
		}
		default:
			assert(false);
			__builtin_unreachable();
			return NULL;
		}
		/* FULL SET OPTIMIZATION; computing this every time may be more expensive than it's worth...
		if ((array != NULL && array_container_full(array)) ||
				(bitset != NULL && bitset_container_cardinality(bitset) == (1 << 16)) ||
				(run != NULL && run_container_is_full(run))) {
			*result_type = RUN_CONTAINER_TYPE;
			if (array != NULL) {
				array_container_free(array);
			}
			if (bitset != NULL) {
				bitset_container_free(bitset);
			}
			if (run != NULL) {
				run_container_add_range(run, 0, (1 << 16));
				return run;
			}
			return run_container_create_range(0, (1 << 16));
		}
		*/
	}

	if (bitset != NULL) {
		if (run != NULL) {
			run_bitset_container_lazy_union(run, bitset, bitset);
			run_container_free(run);
		}
		answer = bitset;
		res_type = BITSET_CONTAINER_TYPE;
	} else if (run != NULL) {
		answer = run;
		res_type = RUN_CONTAINER_TYPE;
	} else {
		assert(false);
		__builtin_unreachable();
		return NULL;
	}

	return convert_run_optimize(answer, res_type, result_type);
}

#ifdef __cplusplus
} } }  // extern "C" { namespace roaring { namespace internal {
#endif
