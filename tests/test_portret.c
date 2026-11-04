#include "test_framework.h"
#include "../portret.h"

static void test_ksztalt(void) {
    int zla_dlugosc = 0, zly_znak = 0, czarne = 0;
    for(uint8_t y = 0; y < PORTRET_H; y++) {
        const char* r = portret_wiersze[y];
        if(strlen(r) != PORTRET_W) zla_dlugosc++;
        for(const char* p = r; *p; p++) {
            if(*p != '#' && *p != '.') zly_znak++;
            if(*p == '#') czarne++;
        }
    }
    CHECK(zla_dlugosc == 0);
    CHECK(zly_znak == 0);
    CHECK(czarne > 100);
    /* mieści się na ekranie 128x64 obok tekstu (tekst od x = 56) */
    CHECK(PORTRET_H <= 64u);
    CHECK(PORTRET_W <= 56u);
}

static void test_piksele(void) {
    CHECK(!portret_piksel(PORTRET_W, 0));
    CHECK(!portret_piksel(0, PORTRET_H));
    CHECK(!portret_piksel(255, 255));
    int niezgodne = 0;
    for(uint8_t y = 0; y < PORTRET_H; y++) {
        for(uint8_t x = 0; x < PORTRET_W; x++) {
            if(portret_piksel(x, y) != (portret_wiersze[y][x] == '#')) niezgodne++;
        }
    }
    CHECK(niezgodne == 0);
}

void test_portret_suite(void) {
    printf("[portret]\n");
    RUN(test_ksztalt);
    RUN(test_piksele);
}
