#include "tekst.h"

#include <string.h>

/* Polskie litery (kod Unicode) -> litery łacińskie */
static const struct {
    uint16_t kod;
    char litera;
} tekst_polskie[] = {
    {0x0105, 'a'}, {0x0107, 'c'}, {0x0119, 'e'}, {0x0142, 'l'}, {0x0144, 'n'}, {0x00F3, 'o'},
    {0x015B, 's'}, {0x017A, 'z'}, {0x017C, 'z'}, {0x0104, 'A'}, {0x0106, 'C'}, {0x0118, 'E'},
    {0x0141, 'L'}, {0x0143, 'N'}, {0x00D3, 'O'}, {0x015A, 'S'}, {0x0179, 'Z'}, {0x017B, 'Z'},
};

/* Odczytuje jeden znak UTF-8 od p (najwyżej do konca), zwraca znak ASCII
 * do wyświetlenia i przesuwa *p. */
static char tekst_znak(const char** p, const char* koniec) {
    const unsigned char c = (unsigned char)**p;
    (*p)++;
    if(c < 0x80u) return (char)c;
    size_t dalej = (c >= 0xF0u) ? 3u : (c >= 0xE0u) ? 2u : (c >= 0xC0u) ? 1u : 0u;
    uint32_t kod = c & (0x3Fu >> dalej);
    while(dalej > 0u && *p < koniec && (((unsigned char)**p) & 0xC0u) == 0x80u) {
        kod = (kod << 6) | (((unsigned char)**p) & 0x3Fu);
        (*p)++;
        dalej--;
    }
    for(size_t i = 0; i < sizeof(tekst_polskie) / sizeof(tekst_polskie[0]); i++) {
        if(tekst_polskie[i].kod == kod) return tekst_polskie[i].litera;
    }
    return '?';
}

static bool tekst_dopisz(Tekst* t, char c) {
    if(t->ile_znakow >= TEKST_MAX_ZNAKOW) return false;
    t->znaki[t->ile_znakow++] = c;
    return true;
}

static bool tekst_bialy(char c) {
    return c == ' ' || c == '\t';
}

/* Jedna linia pliku [p, koniec) -> linia tekstu z sylabami */
static TekstWynik
    tekst_linia_pliku(Tekst* t, const char* p, const char* koniec, uint8_t zwrotka, uint16_t* nuta) {
    if(t->ile_linii >= TEKST_MAX_LINII) return TekstZaDuzy;
    TekstLinia* linia = &t->linie[t->ile_linii];
    linia->start = t->ile_znakow;
    linia->pierwsza_sylaba = t->ile_sylab;
    linia->zwrotka = zwrotka;

    while(p < koniec) {
        while(p < koniec && tekst_bialy(*p)) p++;
        if(p >= koniec) break;
        const char* koniec_slowa = p;
        while(koniec_slowa < koniec && !tekst_bialy(*koniec_slowa)) koniec_slowa++;

        bool pierwsza_w_slowie = true;
        while(p < koniec_slowa) {
            const char* koniec_czesci = p;
            while(koniec_czesci < koniec_slowa && *koniec_czesci != '-') koniec_czesci++;
            if(koniec_czesci - p == 1 && *p == '_') {
                (*nuta)++; /* przedłużenie poprzedniej sylaby */
            } else if(koniec_czesci > p) {
                if(pierwsza_w_slowie && t->ile_znakow > linia->start) {
                    if(!tekst_dopisz(t, ' ')) return TekstZaDuzy;
                }
                pierwsza_w_slowie = false;
                if(t->ile_sylab >= TEKST_MAX_SYLAB) return TekstZaDuzy;
                TekstSylaba* s = &t->sylaby[t->ile_sylab];
                s->nuta = *nuta;
                s->start = t->ile_znakow;
                const char* q = p;
                while(q < koniec_czesci) {
                    char c = tekst_znak(&q, koniec_czesci);
                    if(c == '~') c = ' '; /* spacja w sylabie, bez nowej nuty */
                    if(!tekst_dopisz(t, c)) return TekstZaDuzy;
                }
                const uint16_t dl = (uint16_t)(t->ile_znakow - s->start);
                s->dlugosc = dl > 255u ? 255u : (uint8_t)dl;
                t->ile_sylab++;
                (*nuta)++;
            }
            p = koniec_czesci < koniec_slowa ? koniec_czesci + 1 : koniec_czesci;
        }
    }
    linia->dlugosc = (uint16_t)(t->ile_znakow - linia->start);
    linia->ile_sylab = (uint16_t)(t->ile_sylab - linia->pierwsza_sylaba);
    if(!tekst_dopisz(t, '\0')) return TekstZaDuzy;
    if(linia->dlugosc == 0u && linia->ile_sylab == 0u) {
        t->ile_znakow = linia->start; /* sama "_" - bez linii na ekranie */
        return TekstOk;
    }
    t->ile_linii++;
    return TekstOk;
}

TekstWynik tekst_parsuj(Tekst* t, const char* bufor, size_t dlugosc) {
    t->ile_znakow = 0;
    t->ile_sylab = 0;
    t->ile_linii = 0;
    t->ile_zwrotek = 0;

    const char* p = bufor;
    const char* koniec = bufor + dlugosc;
    if(dlugosc >= 3u && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3; /* BOM */

    uint8_t zwrotka = 0;
    uint16_t nuta = 0;
    bool zwrotka_ma_tekst = false;
    while(p < koniec) {
        const char* koniec_linii = memchr(p, '\n', (size_t)(koniec - p));
        if(koniec_linii == NULL) koniec_linii = koniec;
        const char* k = koniec_linii;
        while(k > p && (k[-1] == '\r' || tekst_bialy(k[-1]))) k--;
        const char* s = p;
        while(s < k && tekst_bialy(*s)) s++;

        if(s == k) {
            if(zwrotka_ma_tekst) { /* pusta linia: następna zwrotka */
                if(zwrotka + 1u >= TEKST_MAX_ZWROTEK) return TekstZaDuzy;
                zwrotka++;
                nuta = 0;
                zwrotka_ma_tekst = false;
            }
        } else if(*s != '#') {
            const TekstWynik w = tekst_linia_pliku(t, s, k, zwrotka, &nuta);
            if(w != TekstOk) return w;
            zwrotka_ma_tekst = true;
        }
        p = koniec_linii < koniec ? koniec_linii + 1 : koniec;
    }
    if(t->ile_sylab == 0u) return TekstPusty;
    t->ile_zwrotek = (uint8_t)(zwrotka_ma_tekst ? zwrotka + 1u : zwrotka);
    return TekstOk;
}

TekstPozycja tekst_pozycja(const Tekst* t, uint8_t zwrotka, uint16_t nuta) {
    TekstPozycja poz = {-1, -1};
    for(uint16_t l = 0; l < t->ile_linii; l++) {
        const TekstLinia* linia = &t->linie[l];
        if(linia->zwrotka != zwrotka) continue;
        if(poz.linia < 0) poz.linia = (int16_t)l;
        for(uint16_t i = 0; i < linia->ile_sylab; i++) {
            const uint16_t s = (uint16_t)(linia->pierwsza_sylaba + i);
            if(t->sylaby[s].nuta > nuta) return poz;
            poz.linia = (int16_t)l;
            poz.sylaba = (int16_t)s;
        }
    }
    return poz;
}

const char* tekst_linia(const Tekst* t, uint16_t linia) {
    if(linia >= t->ile_linii) return "";
    return &t->znaki[t->linie[linia].start];
}

size_t tekst_zawin(
    const char* s,
    size_t dlugosc,
    uint16_t max_px,
    TekstSzerokosc szerokosc,
    void* kontekst,
    uint16_t* poczatki,
    size_t max_wierszy) {
    size_t ile = 0;
    size_t start = 0;
    while(start < dlugosc && s[start] == ' ') start++;
    while(start < dlugosc && ile < max_wierszy) {
        poczatki[ile++] = (uint16_t)start;
        size_t koniec = start; /* koniec ostatniego słowa, które się mieści */
        size_t i = start;
        while(i < dlugosc) {
            size_t k = i;
            while(k < dlugosc && s[k] != ' ') k++;
            if(szerokosc(s + start, k - start, kontekst) > max_px) break;
            koniec = k;
            i = k;
            while(i < dlugosc && s[i] == ' ') i++;
        }
        if(koniec == start) {
            /* słowo dłuższe niż wiersz: tnij po znakach (co najmniej 1) */
            koniec = start + 1u;
            while(koniec < dlugosc && s[koniec] != ' ' &&
                  szerokosc(s + start, koniec + 1u - start, kontekst) <= max_px) {
                koniec++;
            }
        }
        start = koniec;
        while(start < dlugosc && s[start] == ' ') start++;
    }
    return ile;
}

size_t tekst_koniec_wiersza(
    const char* s,
    size_t dlugosc,
    const uint16_t* poczatki,
    size_t ile_wierszy,
    size_t nr) {
    size_t koniec = (nr + 1u < ile_wierszy) ? poczatki[nr + 1u] : dlugosc;
    while(koniec > poczatki[nr] && s[koniec - 1u] == ' ') koniec--;
    return koniec;
}
