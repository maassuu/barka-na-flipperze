/**
 * @file portret.h
 * Portret papieża 52x64 zapisany jako wiersze ASCII, rysowany piksel po pikselu.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PORTRET_W (52u)
#define PORTRET_H (64u)

extern const char* const portret_wiersze[PORTRET_H];

/** True, gdy piksel (x, y) jest czarny. Poza obrazkiem zwraca false. */
bool portret_piksel(uint8_t x, uint8_t y);
