#include "tabby_crypto.h"

#include "tabby_config.h"

#include <WiFi.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include "monocypher-ed25519.h"

static char macBuf[13];
static bool kriptoSaglam = false;
static bool kriptoDenendi = false;
static mbedtls_sha256_context shaCtx;
static bool shaAcik = false;

const char *tabbyMac() {
  if (macBuf[0]) return macBuf;
  String m = WiFi.macAddress();
  m.replace(":", "");
  m.replace("-", "");
  m.toUpperCase();
  if (m.length() < 12) return "";
  strncpy(macBuf, m.c_str(), 12);
  macBuf[12] = 0;
  return macBuf;
}

void tabbyMacNorm(const char *in, char *out, size_t n) {
  size_t j = 0;
  if (!out || n == 0) return;
  for (size_t i = 0; in && in[i] && j + 1 < n; i++) {
    char c = in[i];
    if (c == ':' || c == '-' || c == ' ' || c == '.') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out[j++] = c;
  }
  out[j] = 0;
}

bool tabbySaatSenkron() {
  return time(nullptr) > 1700000000;
}

bool tabbyAnahtarYerTutucu() {
  if (TABBY_ED25519_YER_TUTUCU) return true;
  uint8_t acc = 0;
  for (int i = 0; i < 32; i++) acc |= TABBY_ED25519_ACIK_ANAHTAR[i];
  return acc == 0;
}

bool tabbyKriptoSaglam() { return kriptoSaglam; }

bool tabbyEd25519Dogrula(const uint8_t acik[32], const uint8_t imza[64],
                         const uint8_t *mesaj, size_t mesajBoyutu) {
  if (!acik || !imza) return false;
  if (!mesaj && mesajBoyutu) return false;
  return crypto_ed25519_check(imza, acik, mesaj ? mesaj : (const uint8_t *)"",
                              mesajBoyutu) == 0;
}

void tabbyKriptoKendiniDene() {
  if (kriptoDenendi) return;
  kriptoDenendi = true;
  static const uint8_t pk[32] = {
      0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
      0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
      0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
  static const uint8_t sig[64] = {
      0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2,
      0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5,
      0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f,
      0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70,
      0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe,
      0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b};
  uint8_t bozuk[64];
  memcpy(bozuk, sig, sizeof bozuk);
  bozuk[0] ^= 0x01;
  const bool iyi =
      tabbyEd25519Dogrula(pk, sig, (const uint8_t *)"", 0);
  const bool kotu =
      tabbyEd25519Dogrula(pk, bozuk, (const uint8_t *)"", 0);
  kriptoSaglam = iyi && !kotu;
  Serial.printf("Ed25519 oztest %s\n", kriptoSaglam ? "ok" : "HATA");
}

bool tabbyJeton(const char *s, size_t minN, size_t maxN) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n < minN || n > maxN) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == ':')) {
      return false;
    }
  }
  return true;
}

static int surumParca(const char *&p) {
  int v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    p++;
  }
  while (*p && *p != '.') p++;
  return v;
}

int tabbySurumKarsilastir(const char *a, const char *b) {
  const char *pa = a ? a : "";
  const char *pb = b ? b : "";
  for (int i = 0; i < 4; i++) {
    int va = surumParca(pa);
    int vb = surumParca(pb);
    if (va != vb) return (va < vb) ? -1 : 1;
    if (*pa == '.') pa++;
    if (*pb == '.') pb++;
  }
  return 0;
}

void tabbyHexYaz(const uint8_t *in, size_t n, char *out, bool buyuk) {
  static const char *kucuk = "0123456789abcdef";
  static const char *buyukH = "0123456789ABCDEF";
  const char *h = buyuk ? buyukH : kucuk;
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = h[in[i] >> 4];
    out[i * 2 + 1] = h[in[i] & 0x0f];
  }
  out[n * 2] = 0;
}

bool tabbyHexEsit(const char *hex, const uint8_t ozet[32]) {
  if (!hex) return false;
  char bek[65];
  tabbyHexYaz(ozet, 32, bek, false);
  for (int i = 0; i < 64; i++) {
    char c = hex[i];
    if (!c) return false;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != bek[i]) return false;
  }
  const char *p = hex + 64;
  while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
  return *p == 0;
}

int tabbyBase64Coz(const char *in, uint8_t *out, size_t kapasite) {
  if (!in || !out) return -1;
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (const char *p = in; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= kapasite) return -1;
      out[n++] = (uint8_t)((acc >> bits) & 0xff);
    }
  }
  return (int)n;
}

static void hostKucult(char *s) {
  for (; *s; s++) {
    if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
  }
}

bool tabbyUrlUygun(const char *url) {
  if (!url || strncmp(url, "https://", 8) != 0) return false;
  if (strlen(url) > 240) return false;
  const char *p = url + 8;
  const char *slash = strchr(p, '/');
  const char *at = strchr(p, '@');
  if (at && (!slash || at < slash)) return false;
  char host[80];
  size_t i = 0;
  while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && p[i] != '#') {
    if (i + 1 >= sizeof host) return false;
    host[i] = p[i];
    i++;
  }
  host[i] = 0;
  if (!i) return false;
  hostKucult(host);
  if (strcmp(host, TABBY_SITE_AKTIF.serverHost) == 0) return true;
  if (TABBY_SITE_AKTIF.ekHost[0] &&
      strcmp(host, TABBY_SITE_AKTIF.ekHost) == 0) {
    return true;
  }
  return false;
}

void tabbySha256Baslat() {
  if (shaAcik) mbedtls_sha256_free(&shaCtx);
  mbedtls_sha256_init(&shaCtx);
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256_starts(&shaCtx, 0);
#else
  mbedtls_sha256_starts_ret(&shaCtx, 0);
#endif
  shaAcik = true;
}

void tabbySha256Ekle(const uint8_t *veri, size_t n) {
  if (!shaAcik || !veri || !n) return;
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256_update(&shaCtx, veri, n);
#else
  mbedtls_sha256_update_ret(&shaCtx, veri, n);
#endif
}

void tabbySha256Bitir(uint8_t ozet[32]) {
  if (!ozet) return;
  if (!shaAcik) {
    memset(ozet, 0, 32);
    return;
  }
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256_finish(&shaCtx, ozet);
#else
  mbedtls_sha256_finish_ret(&shaCtx, ozet);
#endif
  mbedtls_sha256_free(&shaCtx);
  shaAcik = false;
}

static bool ekle(char *out, size_t kap, size_t &k, const char *s, size_t n) {
  if (!s) return false;
  if (k + n >= kap) return false;
  memcpy(out + k, s, n);
  k += n;
  out[k] = 0;
  return true;
}

static bool ekleStr(char *out, size_t kap, size_t &k, const char *s) {
  return ekle(out, kap, k, s, s ? strlen(s) : 0);
}

static bool jsonTirnak(char *out, size_t kap, size_t &k, const char *s) {
  if (!ekleStr(out, kap, k, "\"")) return false;
  for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
    char tmp[8];
    if (*p == '"' || *p == '\\') {
      tmp[0] = '\\';
      tmp[1] = (char)*p;
      tmp[2] = 0;
      if (!ekleStr(out, kap, k, tmp)) return false;
    } else if (*p < 0x20) {
      snprintf(tmp, sizeof tmp, "\\u%04x", *p);
      if (!ekleStr(out, kap, k, tmp)) return false;
    } else {
      char c = (char)*p;
      if (!ekle(out, kap, k, &c, 1)) return false;
    }
  }
  return ekleStr(out, kap, k, "\"");
}

static bool kanonEkle(char *out, size_t kap, size_t &k, JsonVariantConst v,
                      int derinlik);

static bool kanonNesne(char *out, size_t kap, size_t &k, JsonObjectConst obj,
                       int derinlik) {
  if (derinlik > 6) return false;
  struct Anahtar {
    char s[48];
    JsonVariantConst v;
  };
  Anahtar liste[16];
  size_t n = 0;
  for (JsonPairConst p : obj) {
    if (n >= 16) return false;
    const char *ad = p.key().c_str();
    if (!ad || strlen(ad) >= sizeof(liste[0].s)) return false;
    strncpy(liste[n].s, ad, sizeof(liste[0].s) - 1);
    liste[n].s[sizeof(liste[0].s) - 1] = 0;
    liste[n].v = p.value();
    n++;
  }
  for (size_t i = 1; i < n; i++) {
    Anahtar tmp = liste[i];
    size_t j = i;
    while (j > 0 && strcmp(liste[j - 1].s, tmp.s) > 0) {
      liste[j] = liste[j - 1];
      j--;
    }
    liste[j] = tmp;
  }
  if (!ekleStr(out, kap, k, "{")) return false;
  for (size_t i = 0; i < n; i++) {
    if (i && !ekleStr(out, kap, k, ",")) return false;
    if (!jsonTirnak(out, kap, k, liste[i].s)) return false;
    if (!ekleStr(out, kap, k, ":")) return false;
    if (!kanonEkle(out, kap, k, liste[i].v, derinlik + 1)) return false;
  }
  return ekleStr(out, kap, k, "}");
}

static bool kanonEkle(char *out, size_t kap, size_t &k, JsonVariantConst v,
                      int derinlik) {
  if (derinlik > 6) return false;
  if (v.isNull()) return ekleStr(out, kap, k, "null");
  if (v.is<bool>()) return ekleStr(out, kap, k, v.as<bool>() ? "true" : "false");
  if (v.is<const char *>()) {
    char quoted[192];
    size_t yaz = serializeJson(v, quoted, sizeof quoted);
    if (!yaz || yaz >= sizeof quoted) return false;
    return ekle(out, kap, k, quoted, yaz);
  }
  if (v.is<JsonObjectConst>()) {
    return kanonNesne(out, kap, k, v.as<JsonObjectConst>(), derinlik);
  }
  if (v.is<JsonArrayConst>()) {
    if (!ekleStr(out, kap, k, "[")) return false;
    size_t i = 0;
    for (JsonVariantConst el : v.as<JsonArrayConst>()) {
      if (i++ && !ekleStr(out, kap, k, ",")) return false;
      if (!kanonEkle(out, kap, k, el, derinlik + 1)) return false;
    }
    return ekleStr(out, kap, k, "]");
  }
  char sayi[48];
  size_t yaz = serializeJson(v, sayi, sizeof sayi);
  if (!yaz || yaz >= sizeof sayi) return false;
  return ekle(out, kap, k, sayi, yaz);
}

bool tabbyArgKanon(JsonVariantConst arg, char *out, size_t kapasite) {
  if (!out || kapasite < 3) return false;
  out[0] = 0;
  if (arg.isNull()) {
    strncpy(out, "{}", kapasite - 1);
    out[kapasite - 1] = 0;
    return true;
  }
  if (!arg.is<JsonObjectConst>()) return false;
  size_t k = 0;
  return kanonNesne(out, kapasite, k, arg.as<JsonObjectConst>(), 0);
}

bool tabbyKomutKanon(const char *msgId, const char *mac, const char *komut,
                     const char *argKanon, uint32_t sonTs, char *out,
                     size_t kapasite) {
  if (!msgId || !mac || !komut || !argKanon || !out) return false;
  if (strchr(msgId, '|') || strchr(mac, '|') || strchr(komut, '|')) return false;
  int n = snprintf(out, kapasite, "%s|%s|%s|%s|%u", msgId, mac, komut, argKanon,
                   (unsigned)sonTs);
  return n > 0 && (size_t)n < kapasite;
}

void tabbyUuid(char *out, size_t n) {
  uint8_t b[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(b + i, &r, 4);
  }
  b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);
  b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
  snprintf(out, n,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
           b[11], b[12], b[13], b[14], b[15]);
}
