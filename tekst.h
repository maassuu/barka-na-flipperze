/**
 * @file tekst.h
 * Tekst do melodii: wczytywanie prostego formatu tekstowego, sylaba dla
 * bieżącej nuty i zawijanie linii. Bez zależności od sprzętu (tests/).
 *
 * Format pliku (UTF-8):
 *   - linia zaczynająca się od '#' to komentarz,
 *   - każda inna niepusta linia to jedna linia tekstu na ekranie,
 *   - spacje dzielą słowa, '-' dzieli słowo na sylaby; każda sylaba
 *     przypada na kolejną brzmiącą nutę melodii (pauzy się nie liczą),
 *   - "_" (osobno albo jako sylaba, np. "ko-_-tek") przedłuża poprzednią
 *     sylabę o jedną nutę,
 *   - pusta linia zaczyna nową zwrotkę - liczenie nut wraca do początku
 *     melodii, która gra się wtedy jeszcze raz.
 * Polskie litery zamieniane są na łacińskie (font Flippera ich nie ma).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEKST_MAX_ZNAKOW (4096u)
#define TEKST_MAX_SYLAB  (640u)
#define TEKST_MAX_LINII  (160u)
#define TEKST_MAX_ZWROTEK (32u)

typedef struct {
    uint16_t nuta; /**< numer brzmiącej nuty w zwrotce (od 0) */
    uint16_t start; /**< początek w Tekst.znaki */
    uint8_t dlugosc;
} TekstSylaba;

typedef struct {
    uint16_t start; /**< początek w Tekst.znaki (tekst zakończony '\0') */
    uint16_t dlugosc;
    uint16_t pierwsza_sylaba;
    uint16_t ile_sylab;
    uint8_t zwrotka;
} TekstLinia;

typedef struct {
    char znaki[TEKST_MAX_ZNAKOW];
    TekstSylaba sylaby[TEKST_MAX_SYLAB];
    TekstLinia linie[TEKST_MAX_LINII];
    uint16_t ile_znakow;
    uint16_t ile_sylab;
    uint16_t ile_linii;
    uint8_t ile_zwrotek;
} Tekst;

typedef enum {
    TekstOk,
    TekstPusty, /**< brak sylab */
    TekstZaDuzy, /**< przekroczony któryś z limitów TEKST_MAX_* */
} TekstWynik;

typedef struct {
    int16_t linia; /**< -1: brak tekstu dla tej zwrotki */
    int16_t sylaba; /**< -1: zwrotka jeszcze nie doszła do pierwszej sylaby */
} TekstPozycja;

/** Szerokość fragmentu tekstu w pikselach (na Flipperze: font ekranu). */
typedef uint16_t (*TekstSzerokosc)(const char* s, size_t dlugosc, void* kontekst);

TekstWynik tekst_parsuj(Tekst* t, const char* bufor, size_t dlugosc);

/** Linia i sylaba śpiewane przy danej brzmiącej nucie danej zwrotki. */
TekstPozycja tekst_pozycja(const Tekst* t, uint8_t zwrotka, uint16_t nuta);

/** Tekst linii (zakończony '\0'). */
const char* tekst_linia(const Tekst* t, uint16_t linia);

/**
 * Zawija tekst do wierszy nie szerszych niż max_px (łamie na spacjach,
 * za długie słowo tnie po znakach). Zapisuje początki wierszy w poczatki,
 * zwraca liczbę wierszy (najwyżej max_wierszy).
 */
size_t tekst_zawin(
    const char* s,
    size_t dlugosc,
    uint16_t max_px,
    TekstSzerokosc szerokosc,
    void* kontekst,
    uint16_t* poczatki,
    size_t max_wierszy);

/** Koniec wiersza w o numerze nr (bez spacji na końcu). */
size_t tekst_koniec_wiersza(
    const char* s,
    size_t dlugosc,
    const uint16_t* poczatki,
    size_t ile_wierszy,
    size_t nr);
