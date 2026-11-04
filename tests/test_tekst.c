#include "test_framework.h"
#include "../melodia.h"
#include "../melodia_przyklad.h"
#include "../tekst.h"

#include <stdlib.h>

static Tekst* tekst_nowy(void) {
    Tekst* t = malloc(sizeof(Tekst));
    memset(t, 0x5A, sizeof(Tekst)); /* śmieci - parser musi wszystko ustawić */
    return t;
}

static TekstWynik parsuj(Tekst* t, const char* s) {
    return tekst_parsuj(t, s, strlen(s));
}

static void sprawdz_sylabe(const Tekst* t, uint16_t i, const char* oczekiwana, uint16_t nuta) {
    char buf[64];
    const TekstSylaba* s = &t->sylaby[i];
    memcpy(buf, &t->znaki[s->start], s->dlugosc);
    buf[s->dlugosc] = '\0';
    CHECK_STR(buf, oczekiwana);
    CHECK(s->nuta == nuta);
}

static void test_podstawy(void) {
    Tekst* t = tekst_nowy();
    CHECK(parsuj(t, "# komentarz\nWlazl ko-tek na plo-tek\n  i mru-ga  \n") == TekstOk);
    CHECK(t->ile_linii == 2u);
    CHECK(t->ile_sylab == 9u);
    CHECK(t->ile_zwrotek == 1u);
    CHECK_STR(tekst_linia(t, 0), "Wlazl kotek na plotek");
    CHECK_STR(tekst_linia(t, 1), "i mruga");
    CHECK_STR(tekst_linia(t, 2), ""); /* poza zakresem */
    sprawdz_sylabe(t, 0, "Wlazl", 0);
    sprawdz_sylabe(t, 1, "ko", 1);
    sprawdz_sylabe(t, 2, "tek", 2);
    sprawdz_sylabe(t, 5, "tek", 5);
    sprawdz_sylabe(t, 6, "i", 6);
    sprawdz_sylabe(t, 8, "ga", 8);
    CHECK(t->linie[1].pierwsza_sylaba == 6u);
    CHECK(t->linie[1].ile_sylab == 3u);
    free(t);
}

static void test_polskie_litery_i_kodowanie(void) {
    Tekst* t = tekst_nowy();
    /* BOM, CRLF, polskie litery, inny znak spoza ASCII -> '?' */
    CHECK(parsuj(t, "\xEF\xBB\xBF" "Za-żół-ć gę-ślą jaźń\r\nĄĆĘŁŃÓŚŹŻ \xE2\x99\xAA\r\n") == TekstOk);
    CHECK_STR(tekst_linia(t, 0), "Zazolc gesla jazn");
    CHECK_STR(tekst_linia(t, 1), "ACELNOSZZ ?");
    sprawdz_sylabe(t, 1, "zol", 1);
    free(t);
}

static void test_przedluzenia(void) {
    Tekst* t = tekst_nowy();
    /* "_" osobno i jako sylaba w słowie; "_" na początku = nuta bez tekstu */
    CHECK(parsuj(t, "_ La _ la-_-la\n") == TekstOk);
    CHECK_STR(tekst_linia(t, 0), "La lala");
    sprawdz_sylabe(t, 0, "La", 1);
    sprawdz_sylabe(t, 1, "la", 3);
    sprawdz_sylabe(t, 2, "la", 5);
    /* "~" = spacja w sylabie: dwa słowa na jednej nucie */
    CHECK(parsuj(t, "raz i~ra-zy dwa\n") == TekstOk);
    CHECK_STR(tekst_linia(t, 0), "raz i razy dwa");
    sprawdz_sylabe(t, 1, "i ra", 1);
    sprawdz_sylabe(t, 2, "zy", 2);
    sprawdz_sylabe(t, 3, "dwa", 3);
    /* linia z samymi "_" nie pojawia się na ekranie, ale liczy nuty */
    CHECK(parsuj(t, "_ _\nraz\n") == TekstOk);
    CHECK(t->ile_linii == 1u);
    sprawdz_sylabe(t, 0, "raz", 2);
    free(t);
}

static void test_zwrotki(void) {
    Tekst* t = tekst_nowy();
    CHECK(parsuj(t, "\n\nraz dwa\ntrzy\n\n\n\ncztery\n# kom\n\npiec\n\n") == TekstOk);
    CHECK(t->ile_zwrotek == 3u);
    CHECK(t->linie[0].zwrotka == 0u);
    CHECK(t->linie[1].zwrotka == 0u);
    CHECK(t->linie[2].zwrotka == 1u);
    CHECK(t->linie[3].zwrotka == 2u);
    sprawdz_sylabe(t, 2, "trzy", 2);
    sprawdz_sylabe(t, 3, "cztery", 0); /* nowa zwrotka liczy nuty od zera */
    sprawdz_sylabe(t, 4, "piec", 0);
    free(t);
}

static void test_bledy_i_limity(void) {
    Tekst* t = tekst_nowy();
    CHECK(parsuj(t, "") == TekstPusty);
    CHECK(parsuj(t, "# tylko komentarz\n\n   \n") == TekstPusty);
    CHECK(parsuj(t, "_ _ -- -\n") == TekstPusty);

    /* za dużo sylab */
    const size_t n = TEKST_MAX_SYLAB + 1u;
    char* duzo = malloc(n * 2u + 1u);
    for(size_t i = 0; i < n; i++) {
        duzo[2u * i] = 'a';
        duzo[2u * i + 1u] = '-';
    }
    duzo[2u * n] = '\0';
    CHECK(parsuj(t, duzo) == TekstZaDuzy);
    free(duzo);

    /* za dużo zwrotek */
    char zwrotki[TEKST_MAX_ZWROTEK * 4u + 1u];
    size_t p = 0;
    for(size_t i = 0; i <= TEKST_MAX_ZWROTEK; i++) {
        zwrotki[p++] = 'x';
        zwrotki[p++] = '\n';
        zwrotki[p++] = '\n';
    }
    zwrotki[p] = '\0';
    CHECK(parsuj(t, zwrotki) == TekstZaDuzy);
    free(t);
}

static void test_pozycja(void) {
    Tekst* t = tekst_nowy();
    CHECK(parsuj(t, "_ raz dwa\ntrzy\n\ncztery\n") == TekstOk);
    TekstPozycja p = tekst_pozycja(t, 0, 0); /* przed pierwszą sylabą */
    CHECK(p.linia == 0 && p.sylaba == -1);
    p = tekst_pozycja(t, 0, 1);
    CHECK(p.linia == 0 && p.sylaba == 0);
    p = tekst_pozycja(t, 0, 2);
    CHECK(p.linia == 0 && p.sylaba == 1);
    p = tekst_pozycja(t, 0, 3);
    CHECK(p.linia == 1 && p.sylaba == 2);
    p = tekst_pozycja(t, 0, 50); /* po końcu tekstu zostaje ostatnia sylaba */
    CHECK(p.linia == 1 && p.sylaba == 2);
    p = tekst_pozycja(t, 1, 0);
    CHECK(p.linia == 2 && p.sylaba == 3);
    p = tekst_pozycja(t, 2, 0); /* zwrotki nie ma */
    CHECK(p.linia == -1 && p.sylaba == -1);
    free(t);
}

static void test_przyklad_pasuje_do_melodii(void) {
    /* przyklady/wlazl_kotek.txt: tyle sylab, ile brzmiących nut melodii */
    FILE* f = fopen("../przyklady/wlazl_kotek.txt", "rb");
    CHECK(f != NULL);
    if(f == NULL) return;
    char buf[1024];
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    Tekst* t = tekst_nowy();
    CHECK(tekst_parsuj(t, buf, n) == TekstOk);
    CHECK(t->ile_zwrotek == 1u);
    const size_t dzwieki = melodia_dzwieki_przed(&melodia_przyklad, melodia_przyklad.ile);
    CHECK(dzwieki == 18u);
    CHECK(t->ile_sylab == dzwieki);
    CHECK(t->sylaby[t->ile_sylab - 1u].nuta == dzwieki - 1u);
    CHECK_STR(tekst_linia(t, 0), "Wlazl kotek na plotek i mruga,");
    free(t);
}

/* Udawany font: każdy znak ma 5 px */
static uint16_t szer5(const char* s, size_t dl, void* kontekst) {
    (void)s;
    (void)kontekst;
    return (uint16_t)(dl * 5u);
}

static void test_zawijanie(void) {
    const char* s = "ala ma kota a kot ma ale";
    uint16_t pocz[8];
    /* 50 px = 10 znaków */
    size_t n = tekst_zawin(s, strlen(s), 50, szer5, NULL, pocz, 8);
    CHECK(n == 3u);
    CHECK(pocz[0] == 0u); /* "ala ma" (+"kota" = 11 znaków - za dużo) */
    CHECK(pocz[1] == 7u); /* "kota a kot" */
    CHECK(pocz[2] == 18u); /* "ma ale" */
    CHECK(tekst_koniec_wiersza(s, strlen(s), pocz, n, 0) == 6u);
    CHECK(tekst_koniec_wiersza(s, strlen(s), pocz, n, 1) == 17u);
    CHECK(tekst_koniec_wiersza(s, strlen(s), pocz, n, 2) == strlen(s));
    /* limit wierszy */
    CHECK(tekst_zawin(s, strlen(s), 50, szer5, NULL, pocz, 2) == 2u);
    /* za długie słowo cięte po znakach */
    const char* d = "abcdefghijklmnop xy";
    n = tekst_zawin(d, strlen(d), 25, szer5, NULL, pocz, 8);
    CHECK(n == 4u); /* "abcde" "fghij" "klmno" "p xy" */
    CHECK(pocz[1] == 5u && pocz[2] == 10u && pocz[3] == 15u);
    /* spacje na początku i pusty tekst */
    CHECK(tekst_zawin("  ab", 4, 50, szer5, NULL, pocz, 8) == 1u && pocz[0] == 2u);
    CHECK(tekst_zawin("", 0, 50, szer5, NULL, pocz, 8) == 0u);
}

static void test_dzwieki_przed(void) {
    const Nuta nuty[] = {{60, 1}, {NUTA_PAUZA, 1}, {62, 1}, {64, 1}};
    const Melodia m = {"x", 60, 1, nuty, 4};
    CHECK(melodia_dzwieki_przed(&m, 0) == 0u);
    CHECK(melodia_dzwieki_przed(&m, 1) == 1u);
    CHECK(melodia_dzwieki_przed(&m, 2) == 1u);
    CHECK(melodia_dzwieki_przed(&m, 3) == 2u);
    CHECK(melodia_dzwieki_przed(&m, 99) == 3u);
}

void test_tekst_suite(void) {
    printf("[tekst]\n");
    RUN(test_podstawy);
    RUN(test_polskie_litery_i_kodowanie);
    RUN(test_przedluzenia);
    RUN(test_zwrotki);
    RUN(test_bledy_i_limity);
    RUN(test_pozycja);
    RUN(test_przyklad_pasuje_do_melodii);
    RUN(test_zawijanie);
    RUN(test_dzwieki_przed);
}
