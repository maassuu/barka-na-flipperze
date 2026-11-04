/**
 * @file odtwarzacz.h
 * Logika odtwarzania melodii: stany i decyzje "włącz / wyłącz dźwięk".
 * Nie dotyka sprzętu - aplikacja wykonuje zwrócone polecenia.
 * Czas w milisekundach, odporny na przepełnienie licznika uint32_t.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "melodia.h"

/** Najdłuższa cisza między nutami, żeby powtórzone dźwięki były słyszalne. */
#define ODTWARZACZ_ODSTEP_MAX_MS (25u)

typedef enum {
    OdtwarzaczGotowy,
    OdtwarzaczGra,
} OdtwarzaczStan;

typedef enum {
    DzwiekBezZmian,
    DzwiekWlacz, /**< zagraj Dzwiek.czestotliwosc (także zmiana wysokości) */
    DzwiekWylacz,
} DzwiekAkcja;

typedef struct {
    DzwiekAkcja akcja;
    float czestotliwosc;
} Dzwiek;

typedef struct {
    const Melodia* melodia;
    OdtwarzaczStan stan;
    size_t indeks; /**< bieżąca nuta */
    uint32_t start_nuty_ms;
    uint32_t start_ms; /**< początek odtwarzania */
    bool brzmi;
    size_t brzmi_indeks; /**< która nuta aktualnie brzmi */
} Odtwarzacz;

void odtwarzacz_init(Odtwarzacz* o, const Melodia* melodia);

/** Gra od początku. */
Dzwiek odtwarzacz_start(Odtwarzacz* o, uint32_t teraz_ms);

/** Zatrzymuje i wraca do stanu "gotowe". Zawsze zwraca DzwiekWylacz. */
Dzwiek odtwarzacz_stop(Odtwarzacz* o);

/** OK: gotowy -> gra, gra -> stop. */
Dzwiek odtwarzacz_przelacz(Odtwarzacz* o, uint32_t teraz_ms);

/** Wołane często (co ~10 ms); mówi, co zrobić z głośnikiem. */
Dzwiek odtwarzacz_krok(Odtwarzacz* o, uint32_t teraz_ms);

/** Ile już zagrano: 0.0 - 1.0 (0 w stanie "gotowe"). */
float odtwarzacz_postep(const Odtwarzacz* o, uint32_t teraz_ms);

/** Ile milisekund od początku odtwarzania (0 w stanie "gotowe"). */
uint32_t odtwarzacz_uplynelo_ms(const Odtwarzacz* o, uint32_t teraz_ms);

/** Długość ciszy na końcu nuty o podanym czasie trwania. */
uint32_t odtwarzacz_odstep_ms(uint32_t dlugosc_ms);
