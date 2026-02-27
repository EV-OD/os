#ifndef KTEST_H
#define KTEST_H

/* =========================================================================
 * ktest.h – Lightweight Kernel Unit-Test Framework
 *
 * Design goals
 * ------------
 *   • Zero external dependencies  (no libc, no hosted OS).
 *   • Minimal BSS footprint       (a single ktest_results_t counter struct).
 *   • Unity / Catch2-inspired API for user familiarity.
 *   • Output goes through the log subsystem → framebuffer + COM1 serial.
 *
 * Quick-start
 * -----------
 *   // 1. Write individual test functions:
 *   static void test_alloc_basic(void) {
 *       unsigned int addr = pfa_alloc_frame();
 *       KTEST_ASSERT_NE(addr, PFA_ALLOC_FAIL);
 *       pfa_free_frame(addr);
 *   }
 *
 *   // 2. Register and run them as a named suite:
 *   void pfa_run_tests(void) {
 *       KTEST_SUITE("pfa",
 *           KTEST_CASE(test_alloc_basic),
 *           KTEST_CASE(test_free_count),
 *       );
 *   }
 *
 *   // 3. In kmain, after hardware init:
 *   ktest_init();
 *   pfa_run_tests();
 *   kheap_run_tests();
 *   ktest_report();
 *
 * Assertion macros available
 * --------------------------
 *   KTEST_ASSERT_TRUE(expr)
 *   KTEST_ASSERT_FALSE(expr)
 *   KTEST_ASSERT_EQ(a, b)      – integer equality
 *   KTEST_ASSERT_NE(a, b)      – integer inequality
 *   KTEST_ASSERT_NULL(ptr)
 *   KTEST_ASSERT_NOT_NULL(ptr)
 * ========================================================================= */

/* ─── Result counters ─────────────────────────────────────────────────── */

typedef struct {
    int passed;
    int failed;
    int total;
} ktest_results_t;

/* ─── Test-entry type ─────────────────────────────────────────────────── */

/** ktest_fn_t – signature every test function must match. */
typedef void (*ktest_fn_t)(void);

/**
 * ktest_entry_t – pairs a test function with its name string.
 * Used by the KTEST_SUITE macro to build a zero-terminated table.
 */
typedef struct {
    ktest_fn_t   fn;      /**< Pointer to the test function, or NULL sentinel */
    const char  *name;    /**< Human-readable name (usually the function name) */
} ktest_entry_t;

/* ─── Public API ──────────────────────────────────────────────────────── */

/**
 * ktest_init – reset all pass/fail counters.
 * Must be called once before any suites are run.
 */
void ktest_init(void);

/**
 * ktest_run_entries – execute a NULL-sentinel-terminated table of tests.
 *
 * For each entry whose .fn is non-NULL, the function is called; any
 * assertion failures recorded during that call increment the failed counter.
 * A per-test PASS/FAIL line and a per-suite summary are logged.
 *
 * @param suite_name  Label shown in the header log line.
 * @param entries     Table of ktest_entry_t, terminated by { NULL, NULL }.
 */
void ktest_run_entries(const char *suite_name, const ktest_entry_t *entries);

/**
 * ktest_totals – return a snapshot of the current result counters.
 */
ktest_results_t ktest_totals(void);

/**
 * ktest_report – emit a final summary line to the log.
 * Logs at INFO level if all tests passed, ERROR level otherwise.
 */
void ktest_report(void);

/* Internal – called by assertion macros, NOT intended for direct use. */
void ktest_record_pass(void);
void ktest_record_fail(const char *file, int line, const char *expr);

/* ─── Assertion macros ────────────────────────────────────────────────── */

#define KTEST_ASSERT_TRUE(expr)                                                     \
    do {                                                                            \
        if (expr) { ktest_record_pass(); }                                          \
        else       { ktest_record_fail(__FILE__, __LINE__, #expr " is false"); }    \
    } while (0)

#define KTEST_ASSERT_FALSE(expr)                                                    \
    do {                                                                            \
        if (!(expr)) { ktest_record_pass(); }                                       \
        else          { ktest_record_fail(__FILE__, __LINE__, #expr " is true"); }  \
    } while (0)

#define KTEST_ASSERT_EQ(a, b)                                                       \
    do {                                                                            \
        if ((a) == (b)) { ktest_record_pass(); }                                    \
        else             { ktest_record_fail(__FILE__, __LINE__, #a " != " #b); }   \
    } while (0)

#define KTEST_ASSERT_NE(a, b)                                                       \
    do {                                                                            \
        if ((a) != (b)) { ktest_record_pass(); }                                    \
        else             { ktest_record_fail(__FILE__, __LINE__, #a " == " #b); }   \
    } while (0)

#define KTEST_ASSERT_NULL(ptr)                                                          \
    do {                                                                                \
        if (!(ptr)) { ktest_record_pass(); }                                            \
        else         { ktest_record_fail(__FILE__, __LINE__, #ptr " is not NULL"); }    \
    } while (0)

#define KTEST_ASSERT_NOT_NULL(ptr)                                                  \
    do {                                                                            \
        if (ptr)  { ktest_record_pass(); }                                          \
        else       { ktest_record_fail(__FILE__, __LINE__, #ptr " is NULL"); }      \
    } while (0)

/* ─── Suite / case registration ───────────────────────────────────────── */

/**
 * KTEST_CASE(fn) – create a ktest_entry_t literal that pairs the function
 * pointer with its stringified name.  Used inside KTEST_SUITE.
 */
#define KTEST_CASE(fn)  { (fn), #fn }

/**
 * KTEST_SUITE(suite_name, ...) – declare a local test table and run it.
 *
 * The variadic arguments must be a comma-separated list of KTEST_CASE(fn)
 * entries (trailing comma after the last entry is permitted in C99).
 * A { NULL, NULL } sentinel is appended automatically.
 *
 * Example:
 *   KTEST_SUITE("pfa",
 *       KTEST_CASE(test_alloc_basic),
 *       KTEST_CASE(test_free_count),
 *   );
 */
#define KTEST_SUITE(suite_name, ...)                            \
    do {                                                        \
        static const ktest_entry_t _entries[] = {               \
            __VA_ARGS__                                         \
            { (ktest_fn_t)0, (const char *)0 }                  \
        };                                                      \
        ktest_run_entries((suite_name), _entries);              \
    } while (0)

#endif /* KTEST_H */
