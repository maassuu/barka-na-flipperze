/**
 * @file melodia.h
 * Jednogłosowa melodia: lista nut (wysokość + długość) i tempo.
 * Bez zależności od sprzętu - testowane na komputerze (tests/).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Wartość pola nuta oznaczająca pauzę (ciszę). */
#define NUTA_PAUZA (0u)

typedef struct {
    uint8_t nuta; /**< numer MIDI (60 = C4, 69 = A4 = 440 Hz) albo NUTA_PAUZA */
    uint16_t tiki; /**< długość w tikach, zob. Melodia.tiki_na_cwiercnute */
} Nuta;

typedef struct {
    const char* tytul;
    uint16_t tempo_bpm; /**< ćwierćnuty na minutę */
    uint16_t tiki_na_cwiercnute;
    const Nuta* nuty;
    size_t ile;
} Melodia;

/** Częstotliwość nuty w Hz; 0 dla pauzy i numerów powyżej 127. */
float melodia_czestotliwosc(uint8_t nuta);

/** Czas trwania podanej liczby tików w milisekundach. */
uint32_t melodia_tiki_na_ms(const Melodia* melodia, uint32_t tiki);

/** Czas trwania całej melodii w milisekundach. */
uint32_t melodia_czas_ms(const Melodia* melodia);

/** True, gdy melodia nadaje się do odtwarzania (tempo, nuty, długości). */
bool melodia_poprawna(const Melodia* melodia);
