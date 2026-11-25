#pragma once

#include <stddef.h>
#include <stdint.h>

void tabbyOtaBoot();
void tabbyOtaOnayla();
void tabbyOtaGozet();
bool tabbyOtaOnayBekliyor();

void tabbyOtaDenemeYaz(uint8_t slot);
void tabbyOtaDenemeSil();

void tabbyEkranAyarla(uint8_t tur, const char *baslik, const char *detay);
bool tabbyEkranOku(char *baslik, size_t nb, char *detay, size_t nd,
                   uint8_t *tur, uint32_t *nesil);
void tabbyOtaSuruyorAyar(bool v);
bool tabbyOtaSuruyor();

struct TabbyOtaIstek {
  char id[48];
  char surum[24];
  char url[241];
  char sha256[80];
  char imza[160];
  uint32_t boyut;
  char varyant[16];
};

bool tabbyOtaImzali(const TabbyOtaIstek &istek);
