#pragma once

#include <Arduino.h>

// Gecici yerel liste — personel kodu (tus takimi) -> isim
// Not: TFT varsayilan font Turkce karakter gostermez → ASCII kullan.

struct Personnel {
  const char *code;
  const char *name;
};

static const Personnel PERSONNEL_DB[] = {
  // Ornek: {"1234", "Ensar Alemdaroglu"},
};

static const size_t PERSONNEL_COUNT =
    sizeof(PERSONNEL_DB) / sizeof(PERSONNEL_DB[0]);

inline const char *lookupPersonnelName(const String &code) {
  for (size_t i = 0; i < PERSONNEL_COUNT; i++) {
    if (code.equals(PERSONNEL_DB[i].code)) {
      return PERSONNEL_DB[i].name;
    }
  }
  return nullptr;
}
