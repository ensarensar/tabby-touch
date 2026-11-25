#pragma once

#include "tabby_config.h"

#define API_URL (TABBY_SITE_AKTIF.apiRoot)
#define API_GECIS_URL (TABBY_SITE_AKTIF.apiGecis)
#define API_KOD_PREFIX (TABBY_SITE_AKTIF.apiKodOnek)
#define API_KOMUT_URL (TABBY_SITE_AKTIF.apiKomut)
#define API_QR_URL (TABBY_SITE_AKTIF.apiQr)
#define API_HEARTBEAT_URL (TABBY_SITE_AKTIF.apiHeartbeat)
#define FIRMWARE_VERSION TABBY_FW_SURUM
static const char *CIHAZ_ID = "tabby-touch-1";
static const char *CIHAZ_ADI_GIRIS = "Tabby Touch Giriş";
static const char *CIHAZ_ADI_CIKIS = "Tabby Touch Çıkış";
static const uint32_t API_TIMEOUT_MS = 15000;
static const uint32_t KOMUT_POLL_MS = 1500;
static const uint32_t KOMUT_TIMEOUT_MS = 4000;
static const uint32_t HEARTBEAT_MS = 60000;
static const uint32_t HEARTBEAT_TIMEOUT_MS = 6000;
static const uint32_t QR_REFRESH_MS = 10000;
static const uint32_t QR_TIMEOUT_MS = 5000;
static const uint8_t QR_KOD_LEN = 22;

/*
 * Sicil gecis (JWT yok, Content-Type: application/json).
 * Karar HTTP kodundan degil govde alanlarindan.
 *
 * POST /tabby-touch  veya  POST /tabby-touch/gecis
 * { "regno": "123456" }
 *
 * Personel yok:
 * { "ok": false, "found": false, "yetkili": false,
 *   "message": "Bu sicil numarasi sistemde tanimli degildir." }
 *
 * Listede yok:
 * { "ok": false, "found": true, "yetkili": false,
 *   "adSoyad": "Cihan Ogur",
 *   "message": "Cihan Ogur Yetkiniz Bulunmamaktadir" }
 *
 * Guvenli listede (kapi ac):
 * { "ok": true, "found": true, "yetkili": true,
 *   "adSoyad": "Cihan Ogur",
 *   "message": "Cihan Ogur Hosgeldiniz" }
 *
 * Alias: regno|regNo|personelRegNo|sicilNo|sicil
 *        yetkili|izin|izinli|guvenliListe|yetki|yetkiVar
 *
 * Uzaktan ac (kiosk POST heartbeat, sunucu cevaplar):
 * POST /tabby-touch/komut
 * Kiosk: { "cihazId": "tabby-touch-1", "mac": "...", "online": true }
 *
 * Manuel ac:
 * { "ok": true, "manuelAc": true, "komut": "ac", "komutId": 12 }
 *
 * Kirmizi kod:
 * { "ok": true, "kirmiziKod": true, "komut": "ac", "komutId": 13 }
 *
 * Alias: serbestBirak, kapiAc, roleCek, ac, open, kirmizi, redCode, alarm
 *
 * Cihaz ayakta izleme (her 60 sn, JWT yok):
 * POST https://<hastane-ip>:8000/tabby-touch-hardbeard
 * { "cihazId": "tabby-touch-1", "mac": "...", "ip": "...",
 *   "online": true, "surum": "1.2.1" }
 * Sunucu: { "ok": true }
 * Imzali OTA ornek yol: /firmware-ota/touch/cyd/1.2.2.bin
 *   https://10.85.1.77:8000/firmware-ota/touch/cyd/1.2.2.bin
 *
 * Tek kullanimlik QR (her 10 sn):
 * POST /tabby-touch/qr
 * { "cihazId": "tabby-touch-1", "mac": "..." }
 * { "ok": true, "kod": "A1B2C3D4E5F6G7H8I9J0K1", "ttlSaniye": 10 }
 * kod tam 22 hane, tek kullanim. Foto ile tekrar kullanilamaz.
 */
