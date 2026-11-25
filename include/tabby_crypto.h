#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

const char *tabbyMac();
void tabbyMacNorm(const char *in, char *out, size_t n);
bool tabbySaatSenkron();
bool tabbyAnahtarYerTutucu();
bool tabbyKriptoSaglam();
void tabbyKriptoKendiniDene();

bool tabbyJeton(const char *s, size_t minN, size_t maxN);
int tabbySurumKarsilastir(const char *a, const char *b);

void tabbyHexYaz(const uint8_t *in, size_t n, char *out, bool buyuk);
bool tabbyHexEsit(const char *hex, const uint8_t ozet[32]);
int tabbyBase64Coz(const char *in, uint8_t *out, size_t kapasite);

bool tabbyUrlUygun(const char *url);

void tabbySha256Baslat();
void tabbySha256Ekle(const uint8_t *veri, size_t n);
void tabbySha256Bitir(uint8_t ozet[32]);

bool tabbyEd25519Dogrula(const uint8_t acik[32], const uint8_t imza[64],
                         const uint8_t *mesaj, size_t mesajBoyutu);

// arg nesne degilse (ve null degilse) false. Cikti en fazla kapasite-1.
bool tabbyArgKanon(JsonVariantConst arg, char *out, size_t kapasite);
bool tabbyKomutKanon(const char *msgId, const char *mac, const char *komut,
                     const char *argKanon, uint32_t sonTs, char *out,
                     size_t kapasite);

void tabbyUuid(char *out, size_t n);
