#include "error.h"
#include "stdio.h"    /* OS puts() / sprintf() */
#include "string.h"   /* OS sprintf() lives in string.h actually */

static int g_err_count = 0;

void error_init(void)
{
    g_err_count = 0;
}

void error_report(const char *category, const char *msg, int line, int col)
{
    char buf[256];
    if (line > 0 && col > 0) {
        sprintf(buf, "error[%s]: %s  (line %d, col %d)\n",
                category, msg, line, col);
    } else {
        sprintf(buf, "error[%s]: %s\n", category, msg);
    }
    puts(buf);
    g_err_count++;
}

int error_count(void)
{
    return g_err_count;
}
