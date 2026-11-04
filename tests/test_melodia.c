#include "test_framework.h"
#include "../melodia.h"
#include "../melodia_przyklad.h"
#include "../melodia_wybrana.h"

static void test_czestotliwosci(void) {
    CHECK_NEAR(melodia_czestotliwosc(69), 440.0, 0.01); /* A4 */
    CHECK_NEAR(melodia_czestotliwosc(60), 261.626, 0.01); /* C4 */
    CHECK_NEAR(melodia_czestotliwosc(72), 523.251, 0.02); /* C5 */
    CHECK_NEAR(melodia_czestotliwosc(1), 8.662, 0.001);
    CHECK_NEAR(melodia_czestotliwosc(127), 12543.85, 0.5); /* G9 */
    /* pauza i numery spoza MIDI - cisza */
    CHECK(melodia_czestotliwosc(NUTA_PAUZA) == 0.0f);
    CHECK(melodia_czestotliwosc(128) == 0.0f);
    CHECK(melodia_czestotliwosc(255) == 0.0f);
    /* oktawa wyżej = dwa razy wyższa częstotliwość */
    for(uint8_t n = 1; n + 12u <= 127u; n++) {
        CHECK_NEAR(
            melodia_czestotliwosc((uint8_t)(n + 12u)), 2.0f * melodia_czestotliwosc(n), 0.001);
    }
    /* półton wyżej ~ 2^(1/12) */
    CHECK_NEAR(melodia_czestotliwosc(70) / melodia_czestotliwosc(69), 1.059463, 0.00001);
}

static const Nuta test_nuty[] = {{60, 384}, {NUTA_PAUZA, 192}, {64, 96}};
static const Melodia test_melodia = {"Test", 120, 384, test_nuty, 3};

static void test_czasy(void) {
    /* 120 BPM: ćwierćnuta = 500 ms */
    CHECK(melodia_tiki_na_ms(&test_melodia, 384) == 500u);
    CHECK(melodia_tiki_na_ms(&test_melodia, 192) == 250u);
    CHECK(melodia_tiki_na_ms(&test_melodia, 1) == 1u); /* 1.30 ms - zaokrąglenie */
    CHECK(melodia_tiki_na_ms(&test_melodia, 0) == 0u);
    CHECK(melodia_czas_ms(&test_melodia) == 875u);
    /* duże wartości: iloczyn tiki * 60000 nie mieści się w 32 bitach */
    CHECK(melodia_tiki_na_ms(&test_melodia, 3000000000u) == 3906250000u);
    const Melodia bez_tempa = {"x", 0, 384, test_nuty, 3};
    CHECK(melodia_tiki_na_ms(&bez_tempa, 384) == 0u);
}

static void test_poprawnosc(void) {
    CHECK(melodia_poprawna(&test_melodia));
    CHECK(!melodia_poprawna(NULL));
    const Melodia pusta = {"x", 120, 384, test_nuty, 0};
    CHECK(!melodia_poprawna(&pusta));
    const Melodia bez_nut = {"x", 120, 384, NULL, 3};
    CHECK(!melodia_poprawna(&bez_nut));
    const Melodia bez_tempa = {"x", 0, 384, test_nuty, 3};
    CHECK(!melodia_poprawna(&bez_tempa));
    const Melodia bez_tikow = {"x", 120, 0, test_nuty, 3};
    CHECK(!melodia_poprawna(&bez_tikow));
    const Nuta zerowa[] = {{60, 0}};
    const Melodia z_zerowa = {"x", 120, 384, zerowa, 1};
    CHECK(!melodia_poprawna(&z_zerowa));
    const Nuta za_wysoka[] = {{128, 10}};
    const Melodia z_za_wysoka = {"x", 120, 384, za_wysoka, 1};
    CHECK(!melodia_poprawna(&z_za_wysoka));
    const Nuta same_pauzy[] = {{NUTA_PAUZA, 10}, {NUTA_PAUZA, 10}};
    const Melodia cisza = {"x", 120, 384, same_pauzy, 2};
    CHECK(!melodia_poprawna(&cisza));
}

/* Melodie wbudowane w aplikację muszą być poprawne i słyszalne na głośniku */
static void test_wbudowane(const Melodia* m) {
    CHECK(melodia_poprawna(m));
    CHECK(m->tytul != NULL && m->tytul[0] != '\0');
    const uint32_t czas = melodia_czas_ms(m);
    CHECK(czas >= 1000u && czas <= 10u * 60u * 1000u);
    for(size_t i = 0; i < m->ile; i++) {
        if(m->nuty[i].nuta == NUTA_PAUZA) continue;
        /* zakres, który mały głośnik Flippera jeszcze sensownie gra */
        const float f = melodia_czestotliwosc(m->nuty[i].nuta);
        CHECK(f >= 100.0f && f <= 5000.0f);
    }
    /* tytuł bez polskich znaków - font Flippera ich nie ma */
    for(const char* c = m->tytul; *c; c++) CHECK((unsigned char)*c < 128u);
}

static void test_przyklad(void) {
    test_wbudowane(&melodia_przyklad);
    CHECK_STR(melodia_przyklad.tytul, "Wlazl kotek");
    CHECK(melodia_przyklad.ile == 19u);
    /* pierwsza fraza: G E E, ostatnia nuta C (półnuta) */
    CHECK(melodia_przyklad.nuty[0].nuta == 79u);
    CHECK(melodia_przyklad.nuty[18].nuta == 72u);
    CHECK(melodia_przyklad.nuty[18].tiki == 8u);
}

static void test_wybrana(void) {
    test_wbudowane(MELODIA_WYBRANA);
#if MELODIA_JEST_BARKA
    CHECK_STR(MELODIA_WYBRANA->tytul, "Barka");
#else
    CHECK(MELODIA_WYBRANA == &melodia_przyklad);
#endif
}

void test_melodia_suite(void) {
    printf("[melodia]\n");
    RUN(test_czestotliwosci);
    RUN(test_czasy);
    RUN(test_poprawnosc);
    RUN(test_przyklad);
    RUN(test_wybrana);
}
