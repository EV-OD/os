/* =========================================================================
 * ktest.c – Lightweight Kernel Unit-Test Framework Implementation
 *
 * See ktest.h for the full API description and usage guide.
 * ========================================================================= */

#include "ktest.h"
#include "log.h"

/* =========================================================================
 * Module state
 * ========================================================================= */

static ktest_results_t g_results;   /* accumulated totals across all suites */
static int g_suite_fail_start;      /* snapshot before each individual test  */

/* =========================================================================
 * Public API
 * ========================================================================= */

void ktest_init(void)
{
    g_results.passed     = 0;
    g_results.failed     = 0;
    g_results.total      = 0;
    g_suite_fail_start   = 0;
    log_info("[ktest] test runner initialised");
}

/* --------------------------------------------------------------------------
 * ktest_run_entries – core suite runner
 * -------------------------------------------------------------------------- */
void ktest_run_entries(const char *suite_name, const ktest_entry_t *entries)
{
    int suite_pass = 0;
    int suite_fail = 0;
    int i;

    log_info("[ktest] ─── suite: %s ───────────────────────────", suite_name);

    for (i = 0; entries[i].fn != (ktest_fn_t)0; i++) {
        int failed_before = g_results.failed;

        /* Run the test function; assertions inside update g_results. */
        entries[i].fn();

        if (g_results.failed > failed_before) {
            suite_fail++;
            log_error("[ktest]   FAIL  %s", entries[i].name);
        } else {
            suite_pass++;
            log_info("[ktest]   PASS  %s", entries[i].name);
        }
    }

    if (suite_fail == 0) {
        log_info("[ktest] suite '%s': all %d test(s) passed",
                 suite_name, suite_pass);
    } else {
        log_error("[ktest] suite '%s': %d passed, %d FAILED",
                  suite_name, suite_pass, suite_fail);
    }
}

/* --------------------------------------------------------------------------
 * ktest_record_pass / ktest_record_fail – called by assertion macros
 * -------------------------------------------------------------------------- */
void ktest_record_pass(void)
{
    g_results.passed++;
    g_results.total++;
}

void ktest_record_fail(const char *file, int line, const char *expr)
{
    g_results.failed++;
    g_results.total++;
    log_error("[ktest]     ASSERT  %s  (at %s:%d)", expr, file, line);
}

/* --------------------------------------------------------------------------
 * ktest_totals / ktest_report
 * -------------------------------------------------------------------------- */
ktest_results_t ktest_totals(void)
{
    return g_results;
}

void ktest_report(void)
{
    log_info("[ktest] ════════════════════════════════════════════");
    if (g_results.failed == 0) {
        log_info("[ktest] ALL TESTS PASSED  (%d assertions in %d test(s))",
                 g_results.passed, g_results.total);
    } else {
        log_error("[ktest] TESTS FAILED  –  %d passed, %d FAILED  (%d total assertions)",
                  g_results.passed, g_results.failed,
                  g_results.passed + g_results.failed);
    }
    log_info("[ktest] ════════════════════════════════════════════");
}
