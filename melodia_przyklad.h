/* Przykładowa melodia z domeny publicznej: "Wlazł kotek na płotek"
 * (polska piosenka ludowa). Grana, gdy nie ma pliku melodia_barka.h. */
#pragma once

#include "melodia.h"

/* 1 tik = szesnastka: ósemka = 2, ćwierćnuta = 4, półnuta = 8.
 * Oktawa 5 (C5 = 72) - na małym głośniku brzmi wyraźniej niż oktawa 4. */
static const Nuta melodia_przyklad_nuty[] = {
    /* Wlazł ko-tek na pło-tek i mru-ga */
    {79, 2}, {76, 2}, {76, 4},
    {77, 2}, {74, 2}, {74, 4},
    {72, 2}, {76, 2}, {79, 4},
    {NUTA_PAUZA, 4},
    /* ład-na to pio-sen-ka nie dłu-ga */
    {79, 2}, {76, 2}, {76, 4},
    {77, 2}, {74, 2}, {74, 4},
    {72, 2}, {76, 2}, {72, 8},
};

static const Melodia melodia_przyklad = {
    .tytul = "Wlazl kotek",
    .tempo_bpm = 100,
    .tiki_na_cwiercnute = 4,
    .nuty = melodia_przyklad_nuty,
    .ile = sizeof(melodia_przyklad_nuty) / sizeof(melodia_przyklad_nuty[0]),
};
