#ifndef OMS555TV_TEST_FRAMEWORK_H
#define OMS555TV_TEST_FRAMEWORK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef void (*TestFunction)(void);

extern int g_test_failures;
extern int g_test_cases;

void test_run(const char *name, TestFunction function);
void test_fail(const char *file,
               int line,
               const char *expression,
               long long expected,
               long long actual);

#define TEST_ASSERT_TRUE(expression)                                      \
    do {                                                                  \
        if (!(expression)) {                                              \
            test_fail(__FILE__, __LINE__, #expression, 1, 0);            \
            return;                                                       \
        }                                                                 \
    } while (0)

#define TEST_ASSERT_FALSE(expression) TEST_ASSERT_TRUE(!(expression))

#define TEST_ASSERT_EQ(expected, actual)                                  \
    do {                                                                  \
        const long long test_expected = (long long)(expected);            \
        const long long test_actual = (long long)(actual);                \
        if (test_expected != test_actual) {                               \
            test_fail(__FILE__,                                           \
                      __LINE__,                                           \
                      #actual,                                            \
                      test_expected,                                      \
                      test_actual);                                       \
            return;                                                       \
        }                                                                 \
    } while (0)

void register_alarm_model_tests(void);
void register_codec_tests(void);
void register_acquisition_tests(void);

#endif
