#pragma once

#include "tabby_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

void tabbyTlsKur(WiFiClientSecure &istemci);

bool tabbyHttpGonder(const char *url, const char *govde, int zamanAsimiMs,
                     int &kod, String &yanit);

void apiSonucDoldur(JsonVariantConst v, ApiResult &r);
ApiResult apiSonucGovde(int httpCode, const String &govde);

bool tabbyHttpGecis(const char *regno, const char *msgId, ApiResult &out);
bool tabbyHttpHeartbeat(String &yanit, int &kod);
bool tabbyHttpQr(char *kod, size_t n, int &ttlSaniye);
bool tabbyHttpKomutPoll(String &yanit, int &kod);
void tabbyHttpKomutAlindi(int komutId);
bool tabbyKomutKapiMi(JsonVariantConst doc);

// Eski imzasiz HTTP OTA. Basari cihazi yeniden baslatir.
bool tabbyHttpOtaEski(const char *url);
