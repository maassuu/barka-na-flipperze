#include "melodia.h"

/* Częstotliwości nut C-1 .. H-1 (MIDI 0..11); wyższe oktawy to kolejne
 * podwojenia. Bez libm, żeby wynik był taki sam na Flipperze i w testach. */
static const float melodia_oktawa_zero[12] = {
    8.175799f,
    8.661957f,
    9.177024f,
    9.722718f,
    10.300861f,
    10.913382f,
    11.562326f,
    12.249857f,
    12.978272f,
    13.750000f,
    14.567618f,
    15.433853f,
};

float melodia_czestotliwosc(uint8_t nuta) {
    if(nuta == NUTA_PAUZA || nuta > 127u) return 0.0f;
    return melodia_oktawa_zero[nuta % 12u] * (float)(1u << (nuta / 12u));
}

uint32_t melodia_tiki_na_ms(const Melodia* melodia, uint32_t tiki) {
    const uint64_t mianownik =
        (uint64_t)melodia->tempo_bpm * (uint64_t)melodia->tiki_na_cwiercnute;
    if(mianownik == 0u) return 0u;
    return (uint32_t)(((uint64_t)tiki * 60000u + mianownik / 2u) / mianownik);
}

uint32_t melodia_czas_ms(const Melodia* melodia) {
    uint32_t suma = 0;
    for(size_t i = 0; i < melodia->ile; i++) {
        suma += melodia_tiki_na_ms(melodia, melodia->nuty[i].tiki);
    }
    return suma;
}

bool melodia_poprawna(const Melodia* melodia) {
    if(melodia == NULL || melodia->nuty == NULL || melodia->ile == 0u) return false;
    if(melodia->tempo_bpm == 0u || melodia->tiki_na_cwiercnute == 0u) return false;
    bool jest_dzwiek = false;
    for(size_t i = 0; i < melodia->ile; i++) {
        if(melodia->nuty[i].tiki == 0u || melodia->nuty[i].nuta > 127u) return false;
        if(melodia->nuty[i].nuta != NUTA_PAUZA) jest_dzwiek = true;
    }
    return jest_dzwiek;
}
