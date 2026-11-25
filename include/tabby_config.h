#pragma once

#include <stdint.h>

/*
 * tabby-touch 1.2.0 — MQTT sozlesmesi (Tabby Admin)
 * -------------------------------------------------
 * Broker anonim: kullanici/sifre YOK, TLS YOK, port 1883.
 * Kok: tabby/touch/<MAC>/...
 * MAC: STA, buyuk harf, iki nokta yok (ornek AABBCCDDEEFF).
 *
 * durum      cihaz->broker  retained + LWT offline
 * olay       cihaz->broker  retained degil, gecis olayi
 * olay/cevap broker->cihaz  retained degil
 * komut      broker->cihaz  retained degil, Ed25519 imzali
 * komut/cevap cihaz->broker retained degil
 * qr         broker->cihaz  retained, tek kullanimlik kod
 * ayar       broker->cihaz  retained, qr ile ayni kod alani (yedek konu)
 * ota        broker->cihaz  retained degil
 * ota/durum  cihaz->broker  retained degil
 *
 * Varyant: "cyd". tip: "touch".
 * Hastane secimi derleme bayragi TABBY_SITE (0 Izmir, 1 Kocaeli).
 * HTTP yedek: TABBY_HTTP_FALLBACK (1 acik). Broker hazir olana kadar
 * mevcut HTTPS gecis/komut/qr/heartbeat calismaya devam eder.
 *
 * Ed25519 acik anahtar bu dosyada. Yer tutucu iken imzali komut ve
 * imzali OTA reddedilir; HTTP yedek calisir.
 *
 * OTA imzasi (ayni anahtar):
 *   1) Tercih: firmware baytlarinin ham icerigi (RFC 8032 Ed25519).
 *   2) Alternatif: sha256 ozetinin 64 karakter hex yazimi
 *      (once kucuk harf, olmazsa buyuk harf).
 * Baslik: X-Tabby-Imza (base64, 64 bayt). Yedek adlar:
 *   X-Tabby-Signature, X-Tabby-Ed25519.
 * X-Tabby-Sha256: 64 hex. MQTT sha256 alani ile ayni olmali.
 * Indirme yalnizca yapilandirilmis Tabby sunucu hostuna (https).
 * Istek basligi: X-Tabby-Cihaz: <MAC>. X-Tabby-Gizli yalnizca
 * TABBY_CIHAZ_GIZLI doluysa gonderilir (bu depoda bos).
 *
 * Komut kanonik dizgi (UTF-8, ayracsiz birlestirme):
 *   msg_id|cihaz_mac|komut|arg|son_gecerlilik_ts
 * arg: JSON nesnesi, anahtarlar bayt sirasiyla, bosluksuz.
 * Eksik arg = {}.
 */

#define TABBY_FW_SURUM "1.2.0"
#define TABBY_VARYANT "cyd"
#define TABBY_TIP "touch"
#define TABBY_MQTT_KOK "tabby/touch"

#ifndef TABBY_SITE
#define TABBY_SITE 0
#endif

#ifndef TABBY_HTTP_FALLBACK
#define TABBY_HTTP_FALLBACK 1
#endif

// Saha sertifikasi self-signed ve yenileniyor (sunucu scripts/tabby-certs.sh).
// Sabit CA gomulene kadar 0: setInsecure (1.1.1 ile ayni). 1 ise setCACert.
#ifndef TABBY_TLS_CA_KULLAN
#define TABBY_TLS_CA_KULLAN 0
#endif

#define TABBY_OTA_SLOT_BOYUT 0x1C0000u
#define TABBY_OTA_ONAY_MS 180000u
#define TABBY_MQTT_CEVAP_MS 4000u
#define TABBY_OLAY_KAPI_SN 120u
#define TABBY_KOMUT_SAAT_TOLERANS_SN 30u
#define TABBY_KOMUT_TEKRAR 32
#define TABBY_OLAY_KUYRUK 6

struct TabbySite {
  const char *ad;
  const char *serverHost;
  const char *ekHost;
  uint16_t serverPort;
  const char *apiRoot;
  const char *apiGecis;
  const char *apiKodOnek;
  const char *apiKomut;
  const char *apiQr;
  const char *apiHeartbeat;
  const char *mqttHost;
  uint16_t mqttPort;
  const char *ntpEk;
};

extern const TabbySite TABBY_SITE_AKTIF;

// 32 bayt ham Ed25519 acik anahtar (SPKI DEGIL).
extern const uint8_t TABBY_ED25519_ACIK_ANAHTAR[32];
extern const bool TABBY_ED25519_YER_TUTUCU;

// Bos string: X-Tabby-Gizli gonderilmez.
extern const char *const TABBY_CIHAZ_GIZLI;

extern const char TABBY_SERVER_CA_PEM[];
