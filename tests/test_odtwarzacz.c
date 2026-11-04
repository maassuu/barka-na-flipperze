#include "test_framework.h"
#include "../odtwarzacz.h"

/* 60 BPM, 1 tik = 1 ms (tiki_na_cwiercnute = 1000) - łatwe rachunki */
static const Nuta nuty[] = {
    {69, 500}, /* A4, 500 ms: brzmi 0-475, cisza 475-500 */
    {69, 500}, /* to samo A4 - musi zabrzmieć od nowa */
    {NUTA_PAUZA, 300},
    {72, 200}, /* C5: brzmi 0-175 */
};
static const Melodia melodia = {"Test", 60, 1000, nuty, 4};

static void test_odstep(void) {
    CHECK(odtwarzacz_odstep_ms(500) == ODTWARZACZ_ODSTEP_MAX_MS);
    CHECK(odtwarzacz_odstep_ms(100) == 20u); /* krótkie nuty: 1/5 długości */
    CHECK(odtwarzacz_odstep_ms(4) == 0u);
    CHECK(odtwarzacz_odstep_ms(0) == 0u);
}

static void test_init(void) {
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    CHECK(o.stan == OdtwarzaczGotowy);
    CHECK(odtwarzacz_krok(&o, 1000).akcja == DzwiekBezZmian);
    CHECK(odtwarzacz_postep(&o, 1000) == 0.0f);
    CHECK(odtwarzacz_uplynelo_ms(&o, 1000) == 0u);
}

static void test_cala_melodia(void) {
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    const uint32_t t0 = 1000;

    Dzwiek d = odtwarzacz_start(&o, t0);
    CHECK(o.stan == OdtwarzaczGra);
    CHECK(d.akcja == DzwiekWlacz);
    CHECK_NEAR(d.czestotliwosc, 440.0, 0.01);

    CHECK(odtwarzacz_krok(&o, t0 + 10).akcja == DzwiekBezZmian);
    CHECK(odtwarzacz_krok(&o, t0 + 474).akcja == DzwiekBezZmian);
    /* odstęp na końcu nuty */
    CHECK(odtwarzacz_krok(&o, t0 + 475).akcja == DzwiekWylacz);
    CHECK(odtwarzacz_krok(&o, t0 + 490).akcja == DzwiekBezZmian);
    /* druga nuta tej samej wysokości - włączona ponownie */
    d = odtwarzacz_krok(&o, t0 + 500);
    CHECK(d.akcja == DzwiekWlacz);
    CHECK_NEAR(d.czestotliwosc, 440.0, 0.01);
    CHECK(o.indeks == 1u);
    CHECK(odtwarzacz_krok(&o, t0 + 975).akcja == DzwiekWylacz);
    /* pauza - nic nie gra */
    CHECK(odtwarzacz_krok(&o, t0 + 1000).akcja == DzwiekBezZmian);
    CHECK(odtwarzacz_krok(&o, t0 + 1250).akcja == DzwiekBezZmian);
    CHECK(o.indeks == 2u);
    /* C5 */
    d = odtwarzacz_krok(&o, t0 + 1300);
    CHECK(d.akcja == DzwiekWlacz);
    CHECK_NEAR(d.czestotliwosc, 523.25, 0.02);
    CHECK_NEAR(odtwarzacz_postep(&o, t0 + 750), 0.5, 0.001);
    CHECK(odtwarzacz_uplynelo_ms(&o, t0 + 1300) == 1300u);
    /* koniec: ostatnia nuta brzmiała, więc wyłączenie i stan "gotowe" */
    d = odtwarzacz_krok(&o, t0 + 1500);
    CHECK(d.akcja == DzwiekWylacz);
    CHECK(o.stan == OdtwarzaczGotowy);
    CHECK(odtwarzacz_krok(&o, t0 + 1600).akcja == DzwiekBezZmian);
}

static void test_koniec_w_ciszy(void) {
    /* gdy ostatni krok trafia już w odstęp, dźwięk był wyłączony wcześniej */
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    odtwarzacz_start(&o, 0);
    odtwarzacz_krok(&o, 1300);
    CHECK(odtwarzacz_krok(&o, 1480).akcja == DzwiekWylacz);
    CHECK(odtwarzacz_krok(&o, 1500).akcja == DzwiekBezZmian);
    CHECK(o.stan == OdtwarzaczGotowy);
}

static void test_spozniony_krok(void) {
    /* krok spóźniony o kilka nut: przeskok wprost do bieżącej */
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    odtwarzacz_start(&o, 0);
    const Dzwiek d = odtwarzacz_krok(&o, 1310);
    CHECK(o.indeks == 3u);
    CHECK(d.akcja == DzwiekWlacz);
    CHECK_NEAR(d.czestotliwosc, 523.25, 0.02);
    /* spóźnienie za koniec melodii */
    odtwarzacz_start(&o, 0);
    CHECK(odtwarzacz_krok(&o, 99999).akcja == DzwiekWylacz);
    CHECK(o.stan == OdtwarzaczGotowy);
}

static void test_stop_i_przelacz(void) {
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    Dzwiek d = odtwarzacz_przelacz(&o, 0);
    CHECK(o.stan == OdtwarzaczGra);
    CHECK(d.akcja == DzwiekWlacz);
    d = odtwarzacz_przelacz(&o, 200);
    CHECK(o.stan == OdtwarzaczGotowy);
    CHECK(d.akcja == DzwiekWylacz);
    CHECK(odtwarzacz_krok(&o, 300).akcja == DzwiekBezZmian);
    /* ponowny start gra od początku */
    d = odtwarzacz_przelacz(&o, 5000);
    CHECK(o.indeks == 0u);
    CHECK(d.akcja == DzwiekWlacz);
    /* stop w stanie "gotowe" też wyłącza głośnik (na wszelki wypadek) */
    odtwarzacz_stop(&o);
    CHECK(odtwarzacz_stop(&o).akcja == DzwiekWylacz);
}

static void test_przepelnienie_licznika(void) {
    /* licznik ms przekręca się co ~49 dni */
    Odtwarzacz o;
    odtwarzacz_init(&o, &melodia);
    const uint32_t t0 = 0xFFFFFF00u;
    odtwarzacz_start(&o, t0);
    CHECK(odtwarzacz_krok(&o, t0 + 475u).akcja == DzwiekWylacz);
    const Dzwiek d = odtwarzacz_krok(&o, t0 + 500u); /* już po przekręceniu */
    CHECK(d.akcja == DzwiekWlacz);
    CHECK(o.indeks == 1u);
    CHECK_NEAR(odtwarzacz_postep(&o, t0 + 750u), 0.5, 0.001);
}

static void test_zla_melodia(void) {
    const Melodia pusta = {"x", 60, 1000, nuty, 0};
    Odtwarzacz o;
    odtwarzacz_init(&o, &pusta);
    CHECK(odtwarzacz_start(&o, 0).akcja == DzwiekWylacz);
    CHECK(o.stan == OdtwarzaczGotowy);
}

void test_odtwarzacz_suite(void) {
    printf("[odtwarzacz]\n");
    RUN(test_odstep);
    RUN(test_init);
    RUN(test_cala_melodia);
    RUN(test_koniec_w_ciszy);
    RUN(test_spozniony_krok);
    RUN(test_stop_i_przelacz);
    RUN(test_przepelnienie_licznika);
    RUN(test_zla_melodia);
}
