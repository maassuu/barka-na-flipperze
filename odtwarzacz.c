#include "odtwarzacz.h"

static const Dzwiek odtwarzacz_bez_zmian = {DzwiekBezZmian, 0.0f};
static const Dzwiek odtwarzacz_wylacz = {DzwiekWylacz, 0.0f};

void odtwarzacz_init(Odtwarzacz* o, const Melodia* melodia) {
    o->melodia = melodia;
    o->stan = OdtwarzaczGotowy;
    o->indeks = 0;
    o->start_nuty_ms = 0;
    o->start_ms = 0;
    o->brzmi = false;
    o->brzmi_indeks = 0;
    o->brzmi_powtorzenie = 0;
    o->powtorzenia = 1;
    o->powtorzenie = 0;
}

void odtwarzacz_ustaw_powtorzenia(Odtwarzacz* o, uint8_t powtorzenia) {
    o->powtorzenia = powtorzenia > 0u ? powtorzenia : 1u;
}

uint32_t odtwarzacz_czas_ms(const Odtwarzacz* o) {
    return melodia_czas_ms(o->melodia) * o->powtorzenia;
}

uint32_t odtwarzacz_odstep_ms(uint32_t dlugosc_ms) {
    const uint32_t odstep = dlugosc_ms / 5u;
    return odstep < ODTWARZACZ_ODSTEP_MAX_MS ? odstep : ODTWARZACZ_ODSTEP_MAX_MS;
}

Dzwiek odtwarzacz_start(Odtwarzacz* o, uint32_t teraz_ms) {
    if(!melodia_poprawna(o->melodia)) return odtwarzacz_stop(o);
    o->stan = OdtwarzaczGra;
    o->indeks = 0;
    o->powtorzenie = 0;
    o->start_nuty_ms = teraz_ms;
    o->start_ms = teraz_ms;
    o->brzmi = false;
    return odtwarzacz_krok(o, teraz_ms);
}

Dzwiek odtwarzacz_stop(Odtwarzacz* o) {
    o->stan = OdtwarzaczGotowy;
    o->indeks = 0;
    o->powtorzenie = 0;
    o->brzmi = false;
    return odtwarzacz_wylacz;
}

Dzwiek odtwarzacz_przelacz(Odtwarzacz* o, uint32_t teraz_ms) {
    if(o->stan == OdtwarzaczGra) return odtwarzacz_stop(o);
    return odtwarzacz_start(o, teraz_ms);
}

Dzwiek odtwarzacz_krok(Odtwarzacz* o, uint32_t teraz_ms) {
    if(o->stan != OdtwarzaczGra) return odtwarzacz_bez_zmian;
    const Melodia* m = o->melodia;

    /* Przeskocz nuty, które już minęły (także kilka naraz, gdy krok się spóźnił) */
    uint32_t dlugosc = melodia_tiki_na_ms(m, m->nuty[o->indeks].tiki);
    while((uint32_t)(teraz_ms - o->start_nuty_ms) >= dlugosc) {
        o->start_nuty_ms += dlugosc;
        o->indeks++;
        if(o->indeks >= m->ile) {
            if(o->powtorzenie + 1u < o->powtorzenia) {
                o->powtorzenie++; /* następna zwrotka: melodia od początku */
                o->indeks = 0;
            } else {
                const bool brzmialo = o->brzmi;
                odtwarzacz_stop(o);
                return brzmialo ? odtwarzacz_wylacz : odtwarzacz_bez_zmian;
            }
        }
        dlugosc = melodia_tiki_na_ms(m, m->nuty[o->indeks].tiki);
    }

    const uint8_t nuta = m->nuty[o->indeks].nuta;
    const uint32_t w_nucie = (uint32_t)(teraz_ms - o->start_nuty_ms);
    const bool ma_brzmiec = nuta != NUTA_PAUZA &&
                            w_nucie < dlugosc - odtwarzacz_odstep_ms(dlugosc);

    if(ma_brzmiec && (!o->brzmi || o->brzmi_indeks != o->indeks ||
                      o->brzmi_powtorzenie != o->powtorzenie)) {
        o->brzmi = true;
        o->brzmi_indeks = o->indeks;
        o->brzmi_powtorzenie = o->powtorzenie;
        const Dzwiek wlacz = {DzwiekWlacz, melodia_czestotliwosc(nuta)};
        return wlacz;
    }
    if(!ma_brzmiec && o->brzmi) {
        o->brzmi = false;
        return odtwarzacz_wylacz;
    }
    return odtwarzacz_bez_zmian;
}

uint32_t odtwarzacz_uplynelo_ms(const Odtwarzacz* o, uint32_t teraz_ms) {
    if(o->stan != OdtwarzaczGra) return 0u;
    return (uint32_t)(teraz_ms - o->start_ms);
}

float odtwarzacz_postep(const Odtwarzacz* o, uint32_t teraz_ms) {
    if(o->stan != OdtwarzaczGra) return 0.0f;
    const uint32_t calosc = odtwarzacz_czas_ms(o);
    if(calosc == 0u) return 0.0f;
    const uint32_t uplynelo = odtwarzacz_uplynelo_ms(o, teraz_ms);
    if(uplynelo >= calosc) return 1.0f;
    return (float)uplynelo / (float)calosc;
}
