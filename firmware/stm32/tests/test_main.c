#include "test_framework.h"

int g_test_failures = 0;
int g_test_cases = 0;

void test_run(const char *name, TestFunction function)
{
    const int failures_before = g_test_failures;
    ++g_test_cases;
    function();
    if (g_test_failures == failures_before) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
    }
}

void test_fail(const char *file,
               int line,
               const char *expression,
               long long expected,
               long long actual)
{
    ++g_test_failures;
    printf("  %s:%d: %s expected=%lld actual=%lld\n",
           file,
           line,
           expression,
           expected,
           actual);
}

int main(void)
{
    register_alarm_model_tests();
    register_codec_tests();
    register_acquisition_tests();

    printf("SUMMARY cases=%d failures=%d\n", g_test_cases, g_test_failures);
    return g_test_failures == 0 ? 0 : 1;
}
