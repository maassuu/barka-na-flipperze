#include "test_framework.h"

int pm_test_failures = 0;
int pm_test_checks = 0;

int main(void) {
    test_melodia_suite();
    test_odtwarzacz_suite();
    test_portret_suite();
    test_tekst_suite();

    printf("\n%d sprawdzeń, %d błędów\n", pm_test_checks, pm_test_failures);
    return pm_test_failures == 0 ? 0 : 1;
}
