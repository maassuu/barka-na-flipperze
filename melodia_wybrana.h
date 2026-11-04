/* Wybór melodii: "Barka" z pliku melodia_barka.h, jeśli istnieje (plik jest
 * w .gitignore - prawa autorskie), w przeciwnym razie melodia przykładowa. */
#pragma once

#if defined(__has_include) && __has_include("melodia_barka.h")
#include "melodia_barka.h"
#define MELODIA_WYBRANA (&melodia_barka)
#define MELODIA_JEST_BARKA (1)
#else
#include "melodia_przyklad.h"
#define MELODIA_WYBRANA (&melodia_przyklad)
#define MELODIA_JEST_BARKA (0)
#endif
