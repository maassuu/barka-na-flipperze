/* Minimalny framework testów hosta (z projektu Papa Monitor). */
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

extern int pm_test_failures;
extern int pm_test_checks;

/* True tylko gdy |a - b| <= eps; NaN i przeciwne nieskończoności
 * nie są "blisko". */
static inline int pm_test_near(double a, double b, double eps) {
    return fabs(a - b) <= eps;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        pm_test_checks++;                                                    \
        if(!(cond)) {                                                        \
            pm_test_failures++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while(0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        pm_test_checks++;                                                    \
        const double _a = (double)(a);                                       \
        const double _b = (double)(b);                                       \
        if(!pm_test_near(_a, _b, (double)(eps))) {                           \
            pm_test_failures++;                                              \
            printf(                                                          \
                "  FAIL %s:%d: %s = %.6f, expected %.6f\n",                  \
                __FILE__,                                                    \
                __LINE__,                                                    \
                #a,                                                          \
                _a,                                                          \
                _b);                                                         \
        }                                                                    \
    } while(0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        pm_test_checks++;                                                    \
        if(strcmp((a), (b)) != 0) {                                          \
            pm_test_failures++;                                              \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                    \
    } while(0)

#define RUN(test)                    \
    do {                             \
        printf("- %s\n", #test);     \
        test();                      \
    } while(0)

void test_melodia_suite(void);
void test_odtwarzacz_suite(void);
void test_portret_suite(void);
void test_tekst_suite(void);
