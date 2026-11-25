#pragma once

#include "tabby_api.h"

#include <Arduino.h>
#include <stddef.h>

void tabbyNetBaslat();
void tabbyNtpKur();
void tabbyOtaBoot();

bool tabbyGecisSor(const char *regno, ApiResult &out, uint32_t bekleMs);

void tabbyQrOku(char *dst, size_t n);
bool tabbyQrKirli();
bool tabbyKapiCek();

void tabbyMqttIsle();
bool tabbyMqttYayin(const char *konu, const char *govde, bool retained);
const char *tabbyKonuOtaDurum();
