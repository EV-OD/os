/* =========================================================================
 * pfa_test.c – Unit tests for the Buddy Physical Frame Allocator
 *
 * These tests exercise the public PFA API after pfa_init() has already
 * been called by kernel_init().  They use real physical memory, so the
 * test results reflect the actual hardware configuration seen by the
 * running kernel.
 *
 * Test organisation
 * -----------------
 *   • Basic alloc/free correctness
 *   • pfa_test_frame() bitmap reflection
 *   • Uniqueness of returned addresses
 *   • Free-count accounting
 *   • Buddy merge-on-free (coalescing)
 *   • Stress: allocate/free a burst of frames
 *
 * How to add a new test
 * ---------------------
 *   1. Write a static void test_xxx(void) function.
 *   2. Add a KTEST_CASE(test_xxx) line inside pfa_run_tests().
 * ========================================================================= */

#include "ktest.h"
#include "pfa.h"

/* =========================================================================
 * Individual test functions
 * ========================================================================= */

/* --------------------------------------------------------------------------
 * test_alloc_returns_valid_addr
 * A freshly allocated frame must not be PFA_ALLOC_FAIL.
 * -------------------------------------------------------------------------- */
static void test_alloc_returns_valid_addr(void)
{
    unsigned int addr = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    pfa_free_frame(addr);
}

/* --------------------------------------------------------------------------
 * test_alloc_is_page_aligned
 * Every returned address must be 4 KB aligned (low 12 bits == 0).
 * -------------------------------------------------------------------------- */
static void test_alloc_is_page_aligned(void)
{
    unsigned int addr = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    KTEST_ASSERT_EQ(addr & (FRAME_SIZE - 1u), 0u);
    pfa_free_frame(addr);
}

/* --------------------------------------------------------------------------
 * test_alloc_marks_frame_used
 * After allocation, pfa_test_frame() must report the frame as used (returns 1).
 * -------------------------------------------------------------------------- */
static void test_alloc_marks_frame_used(void)
{
    unsigned int addr = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    KTEST_ASSERT_EQ(pfa_test_frame(addr), 1);   /* 1 = used */
    pfa_free_frame(addr);
}

/* --------------------------------------------------------------------------
 * test_free_marks_frame_free
 * After freeing a frame, pfa_test_frame() must return 0 (free).
 * -------------------------------------------------------------------------- */
static void test_free_marks_frame_free(void)
{
    unsigned int addr = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    pfa_free_frame(addr);
    KTEST_ASSERT_EQ(pfa_test_frame(addr), 0);   /* 0 = free */
}

/* --------------------------------------------------------------------------
 * test_alloc_unique_addresses
 * Two consecutive allocations must return different addresses.
 * -------------------------------------------------------------------------- */
static void test_alloc_unique_addresses(void)
{
    unsigned int a = pfa_alloc_frame();
    unsigned int b = pfa_alloc_frame();
    KTEST_ASSERT_NE(a, PFA_ALLOC_FAIL);
    KTEST_ASSERT_NE(b, PFA_ALLOC_FAIL);
    KTEST_ASSERT_NE(a, b);
    pfa_free_frame(a);
    pfa_free_frame(b);
}

/* --------------------------------------------------------------------------
 * test_free_count_decrements_on_alloc
 * Allocating one frame must reduce pfa_free_count() by exactly 1.
 * -------------------------------------------------------------------------- */
static void test_free_count_decrements_on_alloc(void)
{
    unsigned int before = pfa_free_count();
    unsigned int addr   = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    KTEST_ASSERT_EQ(pfa_free_count(), before - 1u);
    pfa_free_frame(addr);
}

/* --------------------------------------------------------------------------
 * test_free_count_restores_after_free
 * After freeing an allocated frame the count must be back to the starting
 * value (buddy merge must have coalesced the block correctly).
 * -------------------------------------------------------------------------- */
static void test_free_count_restores_after_free(void)
{
    unsigned int before = pfa_free_count();
    unsigned int addr   = pfa_alloc_frame();
    KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
    pfa_free_frame(addr);
    KTEST_ASSERT_EQ(pfa_free_count(), before);
}

/* --------------------------------------------------------------------------
 * test_buddy_merge_multi
 * Allocate two frames, free both; the allocator must coalesce them back
 * into a single higher-order block (free count returns to original).
 * -------------------------------------------------------------------------- */
static void test_buddy_merge_multi(void)
{
    unsigned int before = pfa_free_count();
    unsigned int a = pfa_alloc_frame();
    unsigned int b = pfa_alloc_frame();
    KTEST_ASSERT_NE(a, PFA_ALLOC_FAIL);
    KTEST_ASSERT_NE(b, PFA_ALLOC_FAIL);
    pfa_free_frame(a);
    pfa_free_frame(b);
    KTEST_ASSERT_EQ(pfa_free_count(), before);
}

/* --------------------------------------------------------------------------
 * test_stress_alloc_free_burst
 * Allocate 32 frames, verify all are valid and unique, then free them all.
 * The free count must be fully restored.
 * -------------------------------------------------------------------------- */
#define STRESS_N 32
static void test_stress_alloc_free_burst(void)
{
    unsigned int frames[STRESS_N];
    unsigned int before = pfa_free_count();
    int i, j;
    int all_unique = 1;

    /* Allocate */
    for (i = 0; i < STRESS_N; i++) {
        frames[i] = pfa_alloc_frame();
        KTEST_ASSERT_NE(frames[i], PFA_ALLOC_FAIL);
    }

    /* Verify uniqueness */
    for (i = 0; i < STRESS_N && all_unique; i++) {
        for (j = i + 1; j < STRESS_N; j++) {
            if (frames[i] == frames[j]) {
                all_unique = 0;
                break;
            }
        }
    }
    KTEST_ASSERT_TRUE(all_unique);

    /* Free */
    for (i = 0; i < STRESS_N; i++) {
        pfa_free_frame(frames[i]);
    }

    KTEST_ASSERT_EQ(pfa_free_count(), before);
}
#undef STRESS_N

/* =========================================================================
 * Suite entry point
 * ========================================================================= */

void pfa_run_tests(void)
{
    KTEST_SUITE("pfa",
        KTEST_CASE(test_alloc_returns_valid_addr),
        KTEST_CASE(test_alloc_is_page_aligned),
        KTEST_CASE(test_alloc_marks_frame_used),
        KTEST_CASE(test_free_marks_frame_free),
        KTEST_CASE(test_alloc_unique_addresses),
        KTEST_CASE(test_free_count_decrements_on_alloc),
        KTEST_CASE(test_free_count_restores_after_free),
        KTEST_CASE(test_buddy_merge_multi),
        KTEST_CASE(test_stress_alloc_free_burst),
    );
}
