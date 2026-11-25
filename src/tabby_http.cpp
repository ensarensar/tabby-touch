#include "tabby_http.h"

#include "api_config.h"
#include "tabby_config.h"
#include "tabby_crypto.h"
#include "tabby_ota.h"
#include "tr_ascii.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

void tabbyTlsKur(WiFiClientSecure &istemci) {
#if TABBY_TLS_CA_KULLAN
  istemci.setCACert(TABBY_SERVER_CA_PEM);
#else
  istemci.setInsecure();
#endif
  istemci.setHandshakeTimeout(12);
}

static String jsonMetin(JsonVariantConst v) {
  if (v.isNull()) return "";
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    return (s && s[0]) ? String(s) : "";
  }
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>() ||
      v.is<unsigned long>() || v.is<long long>()) {
    return String(v.as<long>());
  }
  String s = v.as<String>();
  return s;
}

static String jsonAlan(JsonVariantConst obj, const char *anahtar) {
  if (obj.isNull()) return "";
  return jsonMetin(obj[anahtar]);
}

static bool jsonDogru(JsonVariantConst v) {
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>() || v.is<long>() || v.is<unsigned long>() || v.is<long long>()) {
    return v.as<long>() != 0;
  }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (!s || !s[0]) return false;
    return strcmp(s, "1") == 0 || strcasecmp(s, "true") == 0;
  }
  String s = v.as<String>();
  s.toLowerCase();
  return s == "1" || s == "true";
}

static bool jsonAlanDogru(JsonVariantConst obj, const char *anahtar) {
  if (obj.isNull()) return false;
  return jsonDogru(obj[anahtar]);
}

static int jsonSayi(JsonVariantConst obj, const char *anahtar, int varsayilan) {
  if (obj.isNull()) return varsayilan;
  JsonVariantConst v = obj[anahtar];
  if (v.isNull()) return varsayilan;
  if (v.is<int>() || v.is<long>() || v.is<unsigned long>() || v.is<long long>()) {
    return v.as<int>();
  }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    return (s && s[0]) ? atoi(s) : varsayilan;
  }
  return varsayilan;
}

static String ilkSicil(JsonVariantConst doc) {
  const char *anahtarlar[] = {"regno", "regNo", "personelRegNo", "sicilNo",
                              "sicil"};
  for (const char *k : anahtarlar) {
    String v = jsonAlan(doc, k);
    if (v.length()) return v;
  }
  return "";
}

void apiSonucDoldur(JsonVariantConst v, ApiResult &r) {
  r.ok = jsonAlanDogru(v, "ok");
  r.success = jsonAlanDogru(v, "success");
  r.found = jsonAlanDogru(v, "found");
  r.verified = jsonAlanDogru(v, "verified");
  r.pinBekleniyor = jsonAlanDogru(v, "pinBekleniyor");
  r.kayit = jsonAlanDogru(v, "kayit");
  r.yetkili = jsonAlanDogru(v, "yetkili") || jsonAlanDogru(v, "izin") ||
              jsonAlanDogru(v, "izinli") || jsonAlanDogru(v, "guvenliListe") ||
              jsonAlanDogru(v, "yetki") || jsonAlanDogru(v, "yetkiVar");
  r.oturumId = jsonSayi(v, "oturumId", 0);
  r.ttlSaniye = jsonSayi(v, "ttlSaniye", 0);
  r.regno = ilkSicil(v);
  r.isim = utf8ToAsciiTr(jsonAlan(v, "isim"));
  r.soyisim = utf8ToAsciiTr(jsonAlan(v, "soyisim"));
  r.adSoyad = utf8ToAsciiTr(jsonAlan(v, "adSoyad"));
  r.message = utf8ToAsciiTr(jsonAlan(v, "message"));
  if (!r.message.length()) r.message = utf8ToAsciiTr(jsonAlan(v, "mesaj"));
  r.yon = utf8ToAsciiTr(jsonAlan(v, "yon"));
  if (!r.adSoyad.length() && (r.isim.length() || r.soyisim.length())) {
    r.adSoyad = r.isim;
    if (r.soyisim.length()) {
      if (r.adSoyad.length()) r.adSoyad += " ";
      r.adSoyad += r.soyisim;
    }
  }
}

ApiResult apiSonucGovde(int httpCode, const String &govde) {
  ApiResult r;
  r.httpCode = httpCode;
  if (httpCode < 0) {
    r.message = "Baglanti hatasi";
    return r;
  }
  String trimmed = govde;
  trimmed.trim();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, trimmed);
  if (err) {
    r.message = trimmed.length() ? utf8ToAsciiTr(trimmed)
                                 : "Sunucu yaniti okunamadi";
    Serial.printf("JSON err: %s\n", err.c_str());
    return r;
  }
  apiSonucDoldur(doc.as<JsonVariantConst>(), r);
  return r;
}

bool tabbyHttpGonder(const char *url, const char *govde, int zamanAsimiMs,
                     int &kod, String &yanit) {
  kod = -1;
  yanit = "";
  if (!url || !url[0] || WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure istemci;
  tabbyTlsKur(istemci);

  HTTPClient http;
  http.setTimeout(zamanAsimiMs);
  http.setConnectTimeout(zamanAsimiMs);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(istemci, url)) return false;
  if (govde) {
    http.addHeader("Content-Type", "application/json");
    kod = http.POST(govde);
  } else {
    kod = http.GET();
  }
  yanit = http.getString();
  http.end();
  return kod > 0;
}

bool tabbyHttpGecis(const char *regno, const char *msgId, ApiResult &out) {
  char govde[256];
  snprintf(govde, sizeof govde,
           "{\"regno\":\"%s\",\"sicil\":\"%s\",\"cihazId\":\"%s\","
           "\"mac\":\"%s\",\"msg_id\":\"%s\"}",
           regno ? regno : "", regno ? regno : "", CIHAZ_ID, tabbyMac(),
           msgId ? msgId : "");
  int kod = 0;
  String yanit;
  bool gitti = tabbyHttpGonder(API_GECIS_URL, govde, API_TIMEOUT_MS, kod, yanit);
  if (!gitti || kod == 404) {
    gitti = tabbyHttpGonder(API_URL, govde, API_TIMEOUT_MS, kod, yanit);
  }
  if (!gitti && kod < 0) {
    out.httpCode = -1;
    out.message = "WiFi yok";
    return false;
  }
  Serial.printf("POST gecis/tabby-touch HTTP %d\n", kod);
  out = apiSonucGovde(kod, yanit);
  Serial.printf("ok=%d found=%d yetkili=%d kayit=%d\n", out.ok, out.found,
                out.yetkili, out.kayit);
  return kod > 0;
}

bool tabbyHttpHeartbeat(String &yanit, int &kod) {
  char govde[256];
  snprintf(govde, sizeof govde,
           "{\"cihazId\":\"%s\",\"mac\":\"%s\",\"ip\":\"%s\","
           "\"online\":true,\"surum\":\"%s\"}",
           CIHAZ_ID, tabbyMac(), WiFi.localIP().toString().c_str(),
           FIRMWARE_VERSION);
  bool gitti =
      tabbyHttpGonder(API_HEARTBEAT_URL, govde, HEARTBEAT_TIMEOUT_MS, kod, yanit);
  Serial.printf("Heartbeat %s ip=%s v=%s HTTP %d\n", tabbyMac(),
                WiFi.localIP().toString().c_str(), FIRMWARE_VERSION, kod);
  return gitti;
}

bool tabbyHttpQr(char *kod, size_t n, int &ttlSaniye) {
  ttlSaniye = 10;
  if (!kod || n == 0) return false;
  kod[0] = 0;
  char govde[160];
  snprintf(govde, sizeof govde, "{\"cihazId\":\"%s\",\"mac\":\"%s\"}", CIHAZ_ID,
           tabbyMac());
  int httpKod = 0;
  String yanit;
  if (!tabbyHttpGonder(API_QR_URL, govde, QR_TIMEOUT_MS, httpKod, yanit)) {
    return false;
  }
  if (httpKod < 200 || httpKod >= 300) {
    Serial.printf("QR HTTP %d\n", httpKod);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, yanit)) return false;
  String deger = jsonAlan(doc.as<JsonVariantConst>(), "kod");
  if (!deger.length()) deger = jsonAlan(doc.as<JsonVariantConst>(), "qrKod");
  if (!deger.length()) deger = jsonAlan(doc.as<JsonVariantConst>(), "qr");
  if (!deger.length()) deger = jsonAlan(doc.as<JsonVariantConst>(), "code");
  if (!deger.length()) deger = jsonAlan(doc.as<JsonVariantConst>(), "token");
  deger.trim();
  if (deger.length() < 16 || deger.length() > 30) {
    Serial.printf("QR kod uzunluk %d\n", deger.length());
    return false;
  }
  strncpy(kod, deger.c_str(), n - 1);
  kod[n - 1] = 0;
  int ttl = jsonSayi(doc.as<JsonVariantConst>(), "ttlSaniye", 10);
  ttlSaniye = ttl > 0 ? ttl : 10;
  return true;
}

static String komutGovde(bool alindi, int komutId) {
  String json = String("{\"cihazId\":\"") + CIHAZ_ID + "\",\"mac\":\"" +
                tabbyMac() + "\",\"online\":true,\"surum\":\"" +
                FIRMWARE_VERSION + "\"";
  if (alindi) {
    json += ",\"alindi\":true";
    if (komutId > 0) {
      json += ",\"komutId\":";
      json += String(komutId);
    }
  }
  json += "}";
  return json;
}

bool tabbyHttpKomutPoll(String &yanit, int &kod) {
  String govde = komutGovde(false, 0);
  bool gitti =
      tabbyHttpGonder(API_KOMUT_URL, govde.c_str(), KOMUT_TIMEOUT_MS, kod, yanit);
  if (!gitti || kod == 404) {
    String mac = tabbyMac();
    String url = String(API_KOMUT_URL) + "?cihazId=" + CIHAZ_ID + "&mac=" + mac;
    gitti = tabbyHttpGonder(url.c_str(), nullptr, KOMUT_TIMEOUT_MS, kod, yanit);
  }
  return gitti || kod > 0;
}

void tabbyHttpKomutAlindi(int komutId) {
  String govde = komutGovde(true, komutId);
  int kod = 0;
  String yanit;
  tabbyHttpGonder(API_KOMUT_URL, govde.c_str(), KOMUT_TIMEOUT_MS, kod, yanit);
}

bool tabbyKomutKapiMi(JsonVariantConst doc) {
  if (jsonAlanDogru(doc, "serbestBirak") || jsonAlanDogru(doc, "serbest_birak") ||
      jsonAlanDogru(doc, "serbest") || jsonAlanDogru(doc, "ac") ||
      jsonAlanDogru(doc, "open") || jsonAlanDogru(doc, "kapiAc") ||
      jsonAlanDogru(doc, "roleCek") || jsonAlanDogru(doc, "pulse") ||
      jsonAlanDogru(doc, "manuelAc") || jsonAlanDogru(doc, "manuel_ac") ||
      jsonAlanDogru(doc, "kapiyiAc") || jsonAlanDogru(doc, "kirmiziKod") ||
      jsonAlanDogru(doc, "kirmizi_kod") || jsonAlanDogru(doc, "kirmizi") ||
      jsonAlanDogru(doc, "redCode") || jsonAlanDogru(doc, "alarm")) {
    return true;
  }
  String k = jsonAlan(doc, "komut");
  k.toLowerCase();
  k.replace("_", "");
  k.replace("-", "");
  k.replace(" ", "");
  return k == "ac" || k == "serbest" || k == "serbestbirak" || k == "open" ||
         k == "pulse" || k == "kapi" || k == "role" || k == "manuel" ||
         k == "manuelac" || k == "kirmizi" || k == "kirmizikod" || k == "alarm";
}

bool tabbyHttpOtaEski(const char *url) {
  if (!url || !tabbyUrlUygun(url)) return false;
  tabbyEkranAyarla(1, "Guncelleniyor", FIRMWARE_VERSION);
  tabbyOtaSuruyorAyar(true);
  WiFiClientSecure istemci;
  tabbyTlsKur(istemci);
  istemci.setTimeout(30);
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.onProgress([](int cur, int total) {
    int yuzde = (total > 0) ? (cur * 100 / total) : 0;
    char detay[40];
    snprintf(detay, sizeof detay, "indiriliyor %d%%", yuzde);
    tabbyEkranAyarla(1, "Guncelleniyor", detay);
  });
  Serial.printf("HTTP OTA %s\n", url);
  t_httpUpdate_return ret = httpUpdate.update(istemci, url);
  tabbyOtaSuruyorAyar(false);
  if (ret == HTTP_UPDATE_OK) return true;
  Serial.printf("HTTP OTA hata %d\n", (int)ret);
  tabbyEkranAyarla(2, "Guncelleme hata", "Tekrar denenecek");
  return false;
}
