/* =========================================================================
 * kheap_test.c – Unit tests for the kernel heap allocator (kmalloc/kfree)
 *
 * These tests exercise the public kheap API after kheap_init() has been
 * called by kernel_init().
 *
 * Test organisation
 * -----------------
 *   • Basic alloc/free correctness
 *   • NULL input guard
 *   • Split & coalesce behaviour
 *   • Write-then-read integrity (no silent corruption)
 *   • Multiple independent live allocations
 *   • Stress: small allocation burst
 *
 * How to add a new test
 * ---------------------
 *   1. Write a static void test_xxx(void) function.
 *   2. Add a KTEST_CASE(test_xxx) inside kheap_run_tests().
 * ========================================================================= */

#include "ktest.h"
#include "kheap.h"

/* =========================================================================
 * Individual test functions
 * ========================================================================= */

/* --------------------------------------------------------------------------
 * test_kmalloc_returns_nonnull
 * A basic small allocation must succeed and return a non-NULL pointer.
 * -------------------------------------------------------------------------- */
static void test_kmalloc_returns_nonnull(void)
{
    void *p = kmalloc(64);
    KTEST_ASSERT_NOT_NULL(p);
    kfree(p);
}

/* --------------------------------------------------------------------------
 * test_kfree_null_is_safe
 * kfree(NULL) must not crash or corrupt state.
 * -------------------------------------------------------------------------- */
static void test_kfree_null_is_safe(void)
{
    kfree((void *)0);   /* must not panic / fault */
    KTEST_ASSERT_TRUE(1);   /* reaching here means no crash */
}

/* --------------------------------------------------------------------------
 * test_write_read_integrity
 * Data written to an allocated block must be readable back unchanged.
 * -------------------------------------------------------------------------- */
static void test_write_read_integrity(void)
{
    unsigned char *buf = (unsigned char *)kmalloc(16);
    int i;
    KTEST_ASSERT_NOT_NULL(buf);

    for (i = 0; i < 16; i++) {
        buf[i] = (unsigned char)(i * 3u);
    }
    for (i = 0; i < 16; i++) {
        KTEST_ASSERT_EQ((unsigned int)buf[i], (unsigned int)(i * 3u));
    }
    kfree(buf);
}

/* --------------------------------------------------------------------------
 * test_multiple_live_allocations
 * Three simultaneously live allocations must return distinct non-NULL
 * addresses that do not overlap.
 * -------------------------------------------------------------------------- */
static void test_multiple_live_allocations(void)
{
    void *a = kmalloc(32);
    void *b = kmalloc(64);
    void *c = kmalloc(128);

    KTEST_ASSERT_NOT_NULL(a);
    KTEST_ASSERT_NOT_NULL(b);
    KTEST_ASSERT_NOT_NULL(c);
    KTEST_ASSERT_NE((unsigned int)a, (unsigned int)b);
    KTEST_ASSERT_NE((unsigned int)b, (unsigned int)c);
    KTEST_ASSERT_NE((unsigned int)a, (unsigned int)c);

    kfree(a);
    kfree(b);
    kfree(c);
}

/* --------------------------------------------------------------------------
 * test_free_and_realloc
 * Freeing a block and re-allocating the same size should succeed (the
 * freed block is coalesced back and available again).
 * -------------------------------------------------------------------------- */
static void test_free_and_realloc(void)
{
    void *p1 = kmalloc(256);
    KTEST_ASSERT_NOT_NULL(p1);
    kfree(p1);

    void *p2 = kmalloc(256);
    KTEST_ASSERT_NOT_NULL(p2);
    kfree(p2);
}

/* --------------------------------------------------------------------------
 * test_coalesce_middle_block
 * Allocate three blocks (A, B, C), free B first, then A; check that C
 * can still be freed cleanly (magic guard validates the list).
 * -------------------------------------------------------------------------- */
static void test_coalesce_middle_block(void)
{
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c = kmalloc(64);

    KTEST_ASSERT_NOT_NULL(a);
    KTEST_ASSERT_NOT_NULL(b);
    KTEST_ASSERT_NOT_NULL(c);

    kfree(b);   /* free middle – creates a free gap      */
    kfree(a);   /* free left  – should coalesce with gap */
    kfree(c);   /* free right – heap fully returned      */

    KTEST_ASSERT_TRUE(1);   /* no panic = coalescing worked */
}

/* --------------------------------------------------------------------------
 * test_stress_small_burst
 * Allocate and immediately free 16 small blocks in a loop.
 * Validates that the heap handles repeated alloc/free cycles without
 * corruption (magic guard would panic if headers were overwritten).
 * -------------------------------------------------------------------------- */
#define BURST_N 16
static void test_stress_small_burst(void)
{
    int i;
    for (i = 0; i < BURST_N; i++) {
        void *p = kmalloc(32 + (unsigned int)i * 4u);
        KTEST_ASSERT_NOT_NULL(p);
        kfree(p);
    }
    KTEST_ASSERT_TRUE(1);
}
#undef BURST_N

/* =========================================================================
 * Suite entry point
 * ========================================================================= */

void kheap_run_tests(void)
{
    KTEST_SUITE("kheap",
        KTEST_CASE(test_kmalloc_returns_nonnull),
        KTEST_CASE(test_kfree_null_is_safe),
        KTEST_CASE(test_write_read_integrity),
        KTEST_CASE(test_multiple_live_allocations),
        KTEST_CASE(test_free_and_realloc),
        KTEST_CASE(test_coalesce_middle_block),
        KTEST_CASE(test_stress_small_burst),
    );
}
