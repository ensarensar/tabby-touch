#include "tabby_net.h"

#include "api_config.h"
#include "tabby_config.h"
#include "tabby_crypto.h"
#include "tabby_http.h"
#include "tabby_ota.h"

#include <Preferences.h>
#include <PubSubClient.h>
#include <esp_ota_ops.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static WiFiClient mqttTcp;
static PubSubClient mqtt(mqttTcp);

static char topicKok[40];
static char topicDurum[56];
static char topicOta[56];
static char topicOtaDurum[64];
static char topicOlay[56];
static char topicOlayCevap[64];
static char topicKomut[56];
static char topicKomutCevap[64];
static char topicQr[56];
static char topicAyar[56];
static char clientId[20];
static char willBuf[220];
static bool konuHazir = false;

static char qrBuf[32];
static volatile bool qrDirty = false;
static uint32_t qrOlum = 0;
static uint32_t qrMqttSon = 0;
static portMUX_TYPE qrMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool kapiIstendi = false;

struct Gelen {
  char konu[72];
  char govde[1400];
  bool dolu;
};
static Gelen gelen[4];

struct Olay {
  char id[40];
  char payload[360];
  uint32_t ts;
  uint32_t sonMs;
  uint8_t deneme;
  bool dolu;
};
static Olay kuyruk[TABBY_OLAY_KUYRUK];
static bool kuyrukNvsVar = false;

static char gorulen[TABBY_KOMUT_TEKRAR][40];
static uint8_t gorulenN = 0;
static uint8_t gorulenYaz = 0;

struct Emir {
  volatile bool yeni;
  volatile uint32_t nesil;
  volatile uint32_t iptal;
  char regno[8];
  uint32_t butce;
};
static Emir emir;
static SemaphoreHandle_t emirMux = nullptr;
static SemaphoreHandle_t gecisSem = nullptr;
static ApiResult gecisSonuc;
static volatile uint32_t gecisSonucNesil = 0;
static uint32_t emirSayac = 0;

static bool gecisAktif = false;
static char gecisId[40];
static bool gecisCevapHazir = false;
static ApiResult gecisCevap;

static TabbyOtaIstek otaBekleyen;
static bool otaBekliyor = false;
static char httpOtaUrl[241];
static bool httpOtaBekliyor = false;

static uint32_t mqttSonra = 0;
static uint32_t mqttBekle = 2000;
static uint32_t sonDurum = 0;
static uint32_t sonHeartbeat = 0;
static uint32_t sonQr = 0;
static uint32_t sonKomut = 0;
static uint32_t son404 = 0;
static bool komutUcuVar = true;
static bool zamanKuruldu = false;
static bool mqttHazir = false;

const char *tabbyKonuOtaDurum() { return topicOtaDurum; }

void tabbyNtpKur() {
  const char *s3 = TABBY_SITE_AKTIF.ntp3;
  if (s3 && s3[0]) {
    configTime(0, 0, TABBY_SITE_AKTIF.ntp1, TABBY_SITE_AKTIF.ntp2, s3);
  } else {
    configTime(0, 0, TABBY_SITE_AKTIF.ntp1, TABBY_SITE_AKTIF.ntp2);
  }
  Serial.printf("NTP %s %s %s\n", TABBY_SITE_AKTIF.ntp1, TABBY_SITE_AKTIF.ntp2,
                (s3 && s3[0]) ? s3 : "-");
}

bool tabbyMqttYayin(const char *konu, const char *govde, bool retained) {
  if (!konu || !konu[0] || !govde || !mqtt.connected()) return false;
  // qr / ayar sunucunun retained yapilandirmasi. Cihaz bu konulara yazmaz;
  // HTTP yedek de MQTT'ye kopyalamaz, yalnizca ekran tamponunu doldurur.
  if ((topicQr[0] && strcmp(konu, topicQr) == 0) ||
      (topicAyar[0] && strcmp(konu, topicAyar) == 0)) {
    Serial.printf("MQTT yayin reddedildi (sunucu konusu) %s\n", konu);
    return false;
  }
  const uint8_t *bayt = reinterpret_cast<const uint8_t *>(govde);
  return mqtt.publish(konu, bayt, static_cast<unsigned>(strlen(govde)),
                      retained);
}

void tabbyQrOku(char *dst, size_t n) {
  if (!dst || n == 0) return;
  portENTER_CRITICAL(&qrMux);
  strncpy(dst, qrBuf, n - 1);
  portEXIT_CRITICAL(&qrMux);
  dst[n - 1] = 0;
}

bool tabbyQrKirli() {
  portENTER_CRITICAL(&qrMux);
  bool d = qrDirty;
  qrDirty = false;
  portEXIT_CRITICAL(&qrMux);
  return d;
}

static void qrYaz(const char *kod, int ttlSaniye, bool mqttten) {
  uint32_t simdi = millis();
  portENTER_CRITICAL(&qrMux);
  strncpy(qrBuf, kod ? kod : "", sizeof(qrBuf) - 1);
  qrBuf[sizeof(qrBuf) - 1] = 0;
  qrDirty = true;
  if (qrBuf[0] && ttlSaniye > 0) qrOlum = simdi + (uint32_t)ttlSaniye * 1000u;
  else qrOlum = 0;
  if (mqttten) qrMqttSon = qrBuf[0] ? simdi : 0;
  portEXIT_CRITICAL(&qrMux);
}

static void qrSureBak() {
  uint32_t simdi = millis();
  portENTER_CRITICAL(&qrMux);
  if (qrBuf[0] && qrOlum && (int32_t)(simdi - qrOlum) >= 0) {
    qrBuf[0] = 0;
    qrOlum = 0;
    qrDirty = true;
  }
  portEXIT_CRITICAL(&qrMux);
}

static bool qrHttpAtla() {
  uint32_t simdi = millis();
  portENTER_CRITICAL(&qrMux);
  bool atla = qrMqttSon && (int32_t)(simdi - qrMqttSon) < 20000;
  portEXIT_CRITICAL(&qrMux);
  return atla;
}

static void kapiIste() { kapiIstendi = true; }

bool tabbyKapiCek() {
  if (!kapiIstendi) return false;
  kapiIstendi = false;
  return true;
}

static void konulariKur() {
  const char *mac = tabbyMac();
  snprintf(topicKok, sizeof topicKok, "%s/%s", TABBY_MQTT_KOK, mac);
  snprintf(topicDurum, sizeof topicDurum, "%s/durum", topicKok);
  snprintf(topicOta, sizeof topicOta, "%s/ota", topicKok);
  snprintf(topicOtaDurum, sizeof topicOtaDurum, "%s/ota/durum", topicKok);
  snprintf(topicOlay, sizeof topicOlay, "%s/olay", topicKok);
  snprintf(topicOlayCevap, sizeof topicOlayCevap, "%s/olay/cevap", topicKok);
  snprintf(topicKomut, sizeof topicKomut, "%s/komut", topicKok);
  snprintf(topicKomutCevap, sizeof topicKomutCevap, "%s/komut/cevap", topicKok);
  snprintf(topicQr, sizeof topicQr, "%s/qr", topicKok);
  snprintf(topicAyar, sizeof topicAyar, "%s/ayar", topicKok);
  snprintf(clientId, sizeof clientId, "tt-%s", mac);
  konuHazir = mac[0] != 0;
}

static void willDoldur() {
  String ip = WiFi.localIP().toString();
  snprintf(willBuf, sizeof willBuf,
           "{\"tip\":\"%s\",\"varyant\":\"%s\",\"surum\":\"%s\","
           "\"durum\":\"offline\",\"ip\":\"%s\",\"rssi\":0,\"uptime\":0}",
           TABBY_TIP, TABBY_VARYANT, TABBY_FW_SURUM, ip.c_str());
}

static void durumYayin() {
  if (!mqtt.connected()) return;
  char govde[240];
  snprintf(govde, sizeof govde,
           "{\"tip\":\"%s\",\"varyant\":\"%s\",\"surum\":\"%s\","
           "\"durum\":\"online\",\"ip\":\"%s\",\"rssi\":%d,\"uptime\":%lu}",
           TABBY_TIP, TABBY_VARYANT, TABBY_FW_SURUM,
           WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           (unsigned long)(millis() / 1000));
  // Retained. Canli aboneye giden kopyada retain biti MQTT 3.1.1 geregi 0
  // gorunur; broker deposundaki son mesaj retained kalir.
  if (tabbyMqttYayin(topicDurum, govde, true)) sonDurum = millis();
}

static bool mqttBaglan() {
  if (!konuHazir) konulariKur();
  if (!konuHazir) return false;
  willDoldur();
  // willRetain=true, QoS 1. Temiz DISCONNECT will'i iptal eder; dususte
  // broker ayni konuda offline'i retained olarak saklar.
  const uint8_t willQos = 1;
  const bool willRetain = true;
  const bool cleanSession = true;
  bool ok = mqtt.connect(clientId, nullptr, nullptr, topicDurum, willQos,
                         willRetain, willBuf, cleanSession);
  if (!ok) {
    Serial.printf("MQTT baglanamadi rc=%d %s:%u\n", mqtt.state(),
                  TABBY_SITE_AKTIF.mqttHost, TABBY_SITE_AKTIF.mqttPort);
    return false;
  }
  mqtt.subscribe(topicOta, 1);
  mqtt.subscribe(topicOlayCevap, 1);
  mqtt.subscribe(topicKomut, 1);
  mqtt.subscribe(topicQr, 1);
  mqtt.subscribe(topicAyar, 1);
  Serial.printf("MQTT ok %s kok=%s\n", TABBY_SITE_AKTIF.ad, topicKok);
  durumYayin();
  tabbyOtaOnayla();
  return true;
}

static void mqttBakim() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected()) mqtt.disconnect();
    return;
  }
  if (!mqttHazir) {
    mqtt.setServer(TABBY_SITE_AKTIF.mqttHost, TABBY_SITE_AKTIF.mqttPort);
    mqtt.setCallback([](char *konu, byte *govde, unsigned int len) {
      if (!konu || len >= sizeof(gelen[0].govde)) {
        Serial.println("MQTT govde sigmadi");
        return;
      }
      for (Gelen &g : gelen) {
        if (g.dolu) continue;
        strncpy(g.konu, konu, sizeof(g.konu) - 1);
        g.konu[sizeof(g.konu) - 1] = 0;
        memcpy(g.govde, govde, len);
        g.govde[len] = 0;
        g.dolu = true;
        return;
      }
      Serial.println("MQTT kutu dolu");
    });
    mqtt.setSocketTimeout(4);
    mqtt.setKeepAlive(45);
    if (!mqtt.setBufferSize(2048)) Serial.println("MQTT tampon ayrilamadi");
    mqttHazir = true;
  }
  if (mqtt.connected()) {
    mqttBekle = 2000;
    if (sonDurum == 0 || (int32_t)(millis() - sonDurum) >= (int32_t)HEARTBEAT_MS) {
      durumYayin();
    }
    return;
  }
  if ((int32_t)(millis() - mqttSonra) < 0) return;
  if (!mqttBaglan()) {
    mqttSonra = millis() + mqttBekle;
    if (mqttBekle < 30000) mqttBekle *= 2;
    if (mqttBekle > 30000) mqttBekle = 30000;
  }
}

static int kuyrukBul(const char *id) {
  if (!id) return -1;
  for (int i = 0; i < TABBY_OLAY_KUYRUK; i++) {
    if (kuyruk[i].dolu && strcmp(kuyruk[i].id, id) == 0) return i;
  }
  return -1;
}

static void kuyrukKaydet() {
  JsonDocument doc;
  JsonArray dizi = doc.to<JsonArray>();
  for (const Olay &o : kuyruk) {
    if (!o.dolu) continue;
    JsonObject n = dizi.add<JsonObject>();
    n["id"] = o.id;
    n["p"] = o.payload;
    n["t"] = o.ts;
    n["d"] = o.deneme;
  }
  char buf[2200];
  size_t n = serializeJson(doc, buf, sizeof buf);
  if (!n || n >= sizeof buf) {
    Serial.println("Olay kuyrugu sigmadi");
    return;
  }
  Preferences p;
  if (!p.begin("tabby", false)) return;
  p.putString("olayq", buf);
  p.end();
  kuyrukNvsVar = true;
}

static void kuyrukYukle() {
  Preferences p;
  if (!p.begin("tabby", true)) return;
  String ham = p.getString("olayq", "");
  p.end();
  if (!ham.length()) return;
  JsonDocument doc;
  if (deserializeJson(doc, ham)) return;
  JsonArray dizi = doc.as<JsonArray>();
  int i = 0;
  for (JsonObject o : dizi) {
    if (i >= TABBY_OLAY_KUYRUK) break;
    const char *id = o["id"] | "";
    const char *payload = o["p"] | "";
    if (!id[0] || !payload[0]) continue;
    strncpy(kuyruk[i].id, id, sizeof(kuyruk[i].id) - 1);
    strncpy(kuyruk[i].payload, payload, sizeof(kuyruk[i].payload) - 1);
    kuyruk[i].ts = o["t"] | 0;
    kuyruk[i].deneme = o["d"] | 1;
    kuyruk[i].sonMs = millis();
    kuyruk[i].dolu = true;
    i++;
  }
  if (i) {
    kuyrukNvsVar = true;
    Serial.printf("Olay kuyrugu %d\n", i);
  }
}

static bool kuyrukEkle(const char *id, const char *payload, uint32_t ts) {
  int bos = -1;
  for (int i = 0; i < TABBY_OLAY_KUYRUK; i++) {
    if (!kuyruk[i].dolu) {
      bos = i;
      break;
    }
  }
  if (bos < 0) {
    bos = 0;
    uint32_t enEski = kuyruk[0].ts;
    for (int i = 1; i < TABBY_OLAY_KUYRUK; i++) {
      if (kuyruk[i].ts < enEski) {
        enEski = kuyruk[i].ts;
        bos = i;
      }
    }
    Serial.printf("Olay kuyruk dolu, dusen %s\n", kuyruk[bos].id);
  }
  memset(&kuyruk[bos], 0, sizeof(kuyruk[bos]));
  strncpy(kuyruk[bos].id, id, sizeof(kuyruk[bos].id) - 1);
  strncpy(kuyruk[bos].payload, payload, sizeof(kuyruk[bos].payload) - 1);
  kuyruk[bos].ts = ts;
  kuyruk[bos].dolu = true;
  return true;
}

static bool kuyrukDoluMu() {
  for (const Olay &o : kuyruk) {
    if (o.dolu) return true;
  }
  return false;
}

static void kuyrukNvsTemizle() {
  if (!kuyrukNvsVar) return;
  Preferences p;
  if (!p.begin("tabby", false)) return;
  p.remove("olayq");
  p.end();
  kuyrukNvsVar = false;
}

static void kuyrukSil(int indeks) {
  if (indeks < 0 || indeks >= TABBY_OLAY_KUYRUK) return;
  kuyruk[indeks].dolu = false;
  if (!kuyrukNvsVar) return;
  if (kuyrukDoluMu()) kuyrukKaydet();
  else kuyrukNvsTemizle();
}

static void olaySurdur() {
  if (!mqtt.connected()) return;
  uint32_t simdi = millis();
  for (Olay &o : kuyruk) {
    if (!o.dolu) continue;
    if (gecisAktif && strcmp(o.id, gecisId) == 0) continue;
    uint32_t bekle = 2000u << (o.deneme > 4 ? 4 : o.deneme);
    if (bekle > 30000u) bekle = 30000u;
    if (o.deneme != 0 && (int32_t)(simdi - o.sonMs) < (int32_t)bekle) continue;
    if (tabbyMqttYayin(topicOlay, o.payload, false)) {
      if (o.deneme < 250) o.deneme++;
      o.sonMs = simdi;
      Serial.printf("Olay yine %s deneme=%u\n", o.id, o.deneme);
    }
  }
}

static bool goruldu(const char *id) {
  uint8_t n = gorulenN < TABBY_KOMUT_TEKRAR ? gorulenN : TABBY_KOMUT_TEKRAR;
  for (uint8_t i = 0; i < n; i++) {
    if (strcmp(gorulen[i], id) == 0) return true;
  }
  return false;
}

static void gorulenEkle(const char *id) {
  strncpy(gorulen[gorulenYaz], id, sizeof(gorulen[0]) - 1);
  gorulen[gorulenYaz][sizeof(gorulen[0]) - 1] = 0;
  gorulenYaz = (uint8_t)((gorulenYaz + 1) % TABBY_KOMUT_TEKRAR);
  if (gorulenN < TABBY_KOMUT_TEKRAR) gorulenN++;
}

static void komutCevap(const char *id, const char *msgId, bool ok,
                       const char *mesaj) {
  char govde[240];
  snprintf(govde, sizeof govde,
           "{\"id\":\"%s\",\"msg_id\":\"%s\",\"ok\":%s,\"mesaj\":\"%s\"}",
           id ? id : "", msgId ? msgId : "", ok ? "true" : "false",
           mesaj ? mesaj : "");
  tabbyMqttYayin(topicKomutCevap, govde, false);
  Serial.printf("Komut cevap %s ok=%d %s\n", msgId ? msgId : "", ok ? 1 : 0,
                mesaj ? mesaj : "");
}

static bool kapiKomutuMu(const char *komut) {
  char k[40];
  size_t j = 0;
  for (size_t i = 0; komut && komut[i] && j + 1 < sizeof k; i++) {
    char c = komut[i];
    if (c == '_' || c == '-' || c == ' ') continue;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    k[j++] = c;
  }
  k[j] = 0;
  return strcmp(k, "ac") == 0 || strcmp(k, "open") == 0 ||
         strcmp(k, "serbest") == 0 || strcmp(k, "serbestbirak") == 0 ||
         strcmp(k, "pulse") == 0 || strcmp(k, "kapi") == 0 ||
         strcmp(k, "kapiac") == 0 || strcmp(k, "role") == 0 ||
         strcmp(k, "rolecek") == 0 || strcmp(k, "manuel") == 0 ||
         strcmp(k, "manuelac") == 0 || strcmp(k, "kirmizi") == 0 ||
         strcmp(k, "kirmizikod") == 0 || strcmp(k, "alarm") == 0 ||
         strcmp(k, "redcode") == 0;
}

static bool tsOku(JsonVariantConst v, uint32_t &ts) {
  if (v.isNull()) return false;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (!s || !s[0]) return false;
    char *son = nullptr;
    unsigned long n = strtoul(s, &son, 10);
    if (son == s) return false;
    ts = (uint32_t)n;
    return ts != 0;
  }
  if (v.is<bool>()) return false;
  long long n = v.as<long long>();
  if (n <= 0 || n > 4294967295LL) return false;
  ts = (uint32_t)n;
  return true;
}

static void komutIsle(const char *govde) {
  JsonDocument doc;
  if (deserializeJson(doc, govde)) {
    Serial.println("Komut JSON degil");
    return;
  }
  const char *msgId = doc["msg_id"] | "";
  const char *id = doc["id"] | "";
  if (!msgId[0]) msgId = id;
  if (!id[0]) id = msgId;

  if (!tabbySaatSenkron()) {
    komutCevap(id, msgId, false, "saat senkron degil");
    return;
  }
  if (!tabbyKriptoSaglam()) {
    komutCevap(id, msgId, false, "kripto oztest");
    return;
  }
  if (tabbyAnahtarYerTutucu()) {
    komutCevap(id, msgId, false, "acik anahtar yer tutucu");
    return;
  }
  if (!tabbyJeton(msgId, 8, 40)) {
    komutCevap(id, msgId, false, "msg_id yok");
    return;
  }
  const char *macAlan = doc["cihaz_mac"] | "";
  const char *komut = doc["komut"] | "";
  if (!tabbyJeton(komut, 1, 32)) {
    komutCevap(id, msgId, false, "komut yok");
    return;
  }
  char macNorm[16];
  tabbyMacNorm(macAlan, macNorm, sizeof macNorm);
  if (strcmp(macNorm, tabbyMac()) != 0) {
    komutCevap(id, msgId, false, "mac uyusmuyor");
    return;
  }
  uint32_t sonTs = 0;
  if (!tsOku(doc["son_gecerlilik_ts"], sonTs)) {
    komutCevap(id, msgId, false, "sure yok");
    return;
  }
  time_t simdi = time(nullptr);
  if ((uint64_t)simdi > (uint64_t)sonTs + TABBY_KOMUT_SAAT_TOLERANS_SN) {
    komutCevap(id, msgId, false, "suresi doldu");
    return;
  }
  if (goruldu(msgId)) {
    komutCevap(id, msgId, false, "msg_id tekrar");
    return;
  }
  char arg[320];
  if (!tabbyArgKanon(doc["arg"], arg, sizeof arg)) {
    komutCevap(id, msgId, false, "arg hatali");
    return;
  }
  char kanon[512];
  if (!tabbyKomutKanon(msgId, macAlan, komut, arg, sonTs, kanon, sizeof kanon)) {
    komutCevap(id, msgId, false, "kanon sigmadi");
    return;
  }
  uint8_t imza[64];
  const char *imzaMetin = doc["imza"] | "";
  int imzaN = tabbyBase64Coz(imzaMetin, imza, sizeof imza);
  if (imzaN != 64 ||
      !tabbyEd25519Dogrula(TABBY_ED25519_ACIK_ANAHTAR, imza,
                           (const uint8_t *)kanon, strlen(kanon))) {
    komutCevap(id, msgId, false, "imza gecersiz");
    return;
  }
  gorulenEkle(msgId);
  if (!kapiKomutuMu(komut)) {
    komutCevap(id, msgId, false, "bilinmeyen komut");
    return;
  }
  kapiIste();
  komutCevap(id, msgId, true, "uygulandi");
}

static void qrGovdeIsle(const char *govde) {
  if (!govde || !govde[0]) {
    qrYaz("", 0, true);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, govde)) return;
  JsonVariantConst v = doc.as<JsonVariantConst>();
  String kod = v["kod"].is<const char *>() ? String(v["kod"].as<const char *>())
                                           : String(v["kod"].as<String>());
  if (!kod.length() && v["qr"].is<const char *>()) kod = v["qr"].as<const char *>();
  kod.trim();
  if (!kod.length()) {
    if (!v["kod"].isNull()) qrYaz("", 0, true);
    return;
  }
  if (kod.length() < 16 || kod.length() > 30) {
    Serial.printf("MQTT QR uzunluk %d\n", kod.length());
    return;
  }
  int ttl = 10;
  if (!v["ttlSaniye"].isNull()) ttl = v["ttlSaniye"].as<int>();
  if (ttl < 0) ttl = 0;
  qrYaz(kod.c_str(), ttl, true);
}

static bool yetkiliMi(const ApiResult &r) { return r.yetkili || r.kayit; }

static void olayCevapIsle(const char *govde) {
  JsonDocument doc;
  if (deserializeJson(doc, govde)) return;
  const char *msgId = doc["msg_id"] | "";
  if (!msgId[0]) msgId = doc["id"] | "";
  if (!msgId[0]) return;
  int indeks = kuyrukBul(msgId);
  uint32_t ts = (indeks >= 0) ? kuyruk[indeks].ts : 0;
  if (indeks >= 0) kuyrukSil(indeks);

  ApiResult sonuc;
  bool disOk = doc["ok"].isNull() ? true : (doc["ok"].as<bool>() ||
                                            doc["ok"].as<int>() != 0);
  JsonVariantConst karar = doc["karar"];
  if (!disOk) {
    sonuc.ok = false;
    sonuc.httpCode = 200;
    const char *mesaj = doc["mesaj"] | doc["hata"] | doc["message"] | "";
    sonuc.message = mesaj[0] ? mesaj : "Reddedildi";
  } else if (karar.is<JsonObjectConst>()) {
    apiSonucDoldur(karar, sonuc);
    sonuc.httpCode = 200;
  } else {
    apiSonucDoldur(doc.as<JsonVariantConst>(), sonuc);
    sonuc.httpCode = 200;
  }

  if (gecisAktif && strcmp(gecisId, msgId) == 0) {
    gecisCevap = sonuc;
    gecisCevapHazir = true;
    return;
  }
  if (yetkiliMi(sonuc) && tabbySaatSenkron() && ts > 0) {
    time_t simdi = time(nullptr);
    if (simdi >= (time_t)ts && simdi - (time_t)ts <= (time_t)TABBY_OLAY_KAPI_SN) {
      Serial.printf("Gec cevap kapi %s\n", msgId);
      kapiIste();
    }
  }
}

static void httpOtaNot(JsonVariantConst doc) {
#if !TABBY_HTTP_FALLBACK
  (void)doc;
  return;
#else
  if (mqtt.connected() || tabbyOtaOnayBekliyor() || tabbyOtaSuruyor()) return;
  bool guncelle = false;
  JsonVariantConst g = doc["guncelle"];
  if (!g.isNull()) {
    guncelle = g.is<bool>() ? g.as<bool>() : g.as<int>() != 0;
  }
  if (!guncelle) {
    JsonVariantConst u = doc["update"];
    if (!u.isNull()) guncelle = u.as<bool>() || u.as<int>() != 0;
  }
  if (!guncelle) return;
  const char *surum = doc["surum"] | doc["version"] | "";
  const char *url = doc["url"] | doc["firmwareUrl"] | doc["otaUrl"] | "";
  if (!surum[0] || !url[0]) return;
  if (tabbySurumKarsilastir(surum, FIRMWARE_VERSION) <= 0) return;
  if (!tabbyUrlUygun(url) || strlen(url) >= sizeof httpOtaUrl) return;
  strncpy(httpOtaUrl, url, sizeof(httpOtaUrl) - 1);
  httpOtaUrl[sizeof(httpOtaUrl) - 1] = 0;
  httpOtaBekliyor = true;
  Serial.printf("HTTP OTA teklif %s\n", surum);
#endif
}

static void otaGovdeIsle(const char *govde) {
  if (tabbyOtaSuruyor() || tabbyOtaOnayBekliyor()) {
    Serial.println("OTA mesgul, teklif birakildi");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, govde)) return;
  memset(&otaBekleyen, 0, sizeof otaBekleyen);
  const char *id = doc["id"] | "";
  const char *surum = doc["surum"] | "";
  const char *url = doc["url"] | "";
  const char *sha = doc["sha256"] | "";
  const char *varyant = doc["varyant"] | "";
  const char *imza = doc["imza"] | "";
  strncpy(otaBekleyen.id, id, sizeof(otaBekleyen.id) - 1);
  strncpy(otaBekleyen.surum, surum, sizeof(otaBekleyen.surum) - 1);
  strncpy(otaBekleyen.url, url, sizeof(otaBekleyen.url) - 1);
  strncpy(otaBekleyen.sha256, sha, sizeof(otaBekleyen.sha256) - 1);
  strncpy(otaBekleyen.varyant, varyant, sizeof(otaBekleyen.varyant) - 1);
  strncpy(otaBekleyen.imza, imza, sizeof(otaBekleyen.imza) - 1);
  if (doc["boyut"].is<int>() || doc["boyut"].is<long>() ||
      doc["boyut"].is<unsigned long>()) {
    long long b = doc["boyut"].as<long long>();
    if (b > 0) otaBekleyen.boyut = (uint32_t)b;
  }
  otaBekliyor = true;
  Serial.printf("OTA teklif id=%s surum=%s\n", otaBekleyen.id, otaBekleyen.surum);
}

static void gelenIsle() {
  for (Gelen &g : gelen) {
    if (!g.dolu) continue;
    if (strcmp(g.konu, topicKomut) == 0) komutIsle(g.govde);
    else if (strcmp(g.konu, topicOlayCevap) == 0) olayCevapIsle(g.govde);
    else if (strcmp(g.konu, topicQr) == 0 || strcmp(g.konu, topicAyar) == 0)
      qrGovdeIsle(g.govde);
    else if (strcmp(g.konu, topicOta) == 0) otaGovdeIsle(g.govde);
    g.dolu = false;
  }
}

void tabbyMqttIsle() {
  if (!mqtt.connected()) return;
  mqtt.loop();
  gelenIsle();
}

static void sonucBildir(uint32_t nesil, const ApiResult &sonuc) {
  gecisSonuc = sonuc;
  asm volatile("" ::: "memory");
  gecisSonucNesil = nesil;
  if (gecisSem) xSemaphoreGive(gecisSem);
}

static bool sicilRakam(const char *regno) {
  if (!regno || !regno[0]) return false;
  size_t n = strlen(regno);
  if (n < 1 || n > 6) return false;
  for (size_t i = 0; i < n; i++) {
    if (regno[i] < '0' || regno[i] > '9') return false;
  }
  return true;
}

static void gecisBitir(uint32_t nesil, const ApiResult &sonuc, bool iptal) {
  gecisAktif = false;
  if (iptal) {
    if (yetkiliMi(sonuc)) kapiIste();
    return;
  }
  sonucBildir(nesil, sonuc);
}

static void gecisYurut() {
  if (!emirMux) return;
  char regno[8];
  uint32_t nesil = 0;
  uint32_t butce = 0;
  bool var = false;
  if (xSemaphoreTake(emirMux, 0) == pdTRUE) {
    if (emir.yeni) {
      strncpy(regno, emir.regno, sizeof regno);
      regno[sizeof(regno) - 1] = 0;
      nesil = emir.nesil;
      butce = emir.butce;
      emir.yeni = false;
      var = true;
    }
    xSemaphoreGive(emirMux);
  }
  if (!var) return;

  ApiResult sonuc;
  if (!sicilRakam(regno)) {
    sonuc.httpCode = -1;
    sonuc.message = "Sicil gecersiz";
    gecisBitir(nesil, sonuc, emir.iptal == nesil);
    return;
  }

  char id[40];
  tabbyUuid(id, sizeof id);
  uint32_t ts = tabbySaatSenkron() ? (uint32_t)time(nullptr) : 0;
  char payload[360];
  snprintf(payload, sizeof payload,
           "{\"msg_id\":\"%s\",\"ts\":%u,\"tip\":\"gecis\",\"kaynak\":\"sicil\","
           "\"regno\":\"%s\",\"sicil\":\"%s\",\"cihazId\":\"%s\",\"mac\":\"%s\"}",
           id, (unsigned)ts, regno, regno, CIHAZ_ID, tabbyMac());
  kuyrukEkle(id, payload, ts);
  strncpy(gecisId, id, sizeof gecisId - 1);
  gecisId[sizeof(gecisId) - 1] = 0;
  gecisAktif = true;
  gecisCevapHazir = false;

  bool mqttYolu = mqtt.connected();
  if (mqttYolu) {
    tabbyMqttYayin(topicOlay, payload, false);
    int idx = kuyrukBul(id);
    if (idx >= 0) {
      kuyruk[idx].deneme = 1;
      kuyruk[idx].sonMs = millis();
    }
  }

  uint32_t mqttSon = millis() + (mqttYolu ? TABBY_MQTT_CEVAP_MS : 0);
  if (!TABBY_HTTP_FALLBACK) mqttSon = millis() + butce;
  while (mqttYolu && (int32_t)(mqttSon - millis()) > 0) {
    if (emir.iptal == nesil) break;
    tabbyMqttIsle();
    if (gecisCevapHazir) {
      int idx = kuyrukBul(id);
      if (idx >= 0) kuyrukSil(idx);
      gecisBitir(nesil, gecisCevap, emir.iptal == nesil);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

#if TABBY_HTTP_FALLBACK
  if (emir.iptal != nesil) {
    tabbyMqttIsle();
    if (gecisCevapHazir) {
      int idx = kuyrukBul(id);
      if (idx >= 0) kuyrukSil(idx);
      gecisBitir(nesil, gecisCevap, false);
      return;
    }
    bool httpOk = tabbyHttpGecis(regno, id, sonuc);
    if (httpOk) {
      int idx = kuyrukBul(id);
      if (idx >= 0) kuyrukSil(idx);
    } else {
      if (!sonuc.message.length()) {
        sonuc.httpCode = -1;
        sonuc.message = "Baglanti hatasi";
      }
      if (kuyrukBul(id) >= 0) kuyrukKaydet();
    }
    gecisBitir(nesil, sonuc, emir.iptal == nesil);
    return;
  }
#endif
  sonuc.httpCode = -1;
  sonuc.message = "Sunucu yanit vermedi";
  if (kuyrukBul(id) >= 0) kuyrukKaydet();
  gecisBitir(nesil, sonuc, emir.iptal == nesil);
}

bool tabbyGecisSor(const char *regno, ApiResult &out, uint32_t bekleMs) {
  out = ApiResult();
  if (!gecisSem || !emirMux) {
    out.httpCode = -1;
    out.message = "Sunucu mesgul";
    return false;
  }
  uint32_t my = ++emirSayac;
  xSemaphoreTake(gecisSem, 0);
  if (xSemaphoreTake(emirMux, pdMS_TO_TICKS(500)) != pdTRUE) {
    out.httpCode = -1;
    out.message = "Sunucu mesgul";
    return false;
  }
  strncpy(emir.regno, regno ? regno : "", sizeof(emir.regno) - 1);
  emir.regno[sizeof(emir.regno) - 1] = 0;
  emir.butce = bekleMs;
  emir.nesil = my;
  emir.iptal = 0;
  emir.yeni = true;
  xSemaphoreGive(emirMux);

  uint32_t bas = millis();
  while ((int32_t)(millis() - bas) < (int32_t)bekleMs) {
    uint32_t kaldi = bekleMs - (millis() - bas);
    if (xSemaphoreTake(gecisSem, pdMS_TO_TICKS(kaldi)) != pdTRUE) break;
    if (gecisSonucNesil == my) {
      out = gecisSonuc;
      return true;
    }
  }
  emir.iptal = my;
  out.httpCode = -1;
  out.message = "Sunucu yanit vermedi";
  return false;
}

static void httpBakim() {
#if !TABBY_HTTP_FALLBACK
  return;
#else
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t simdi = millis();
  if (sonHeartbeat == 0 ||
      (int32_t)(simdi - sonHeartbeat) >= (int32_t)HEARTBEAT_MS) {
    sonHeartbeat = simdi;
    String yanit;
    int kod = 0;
    if (tabbyHttpHeartbeat(yanit, kod) && kod >= 200 && kod < 300) {
      JsonDocument doc;
      if (!deserializeJson(doc, yanit)) httpOtaNot(doc.as<JsonVariantConst>());
    }
  }
  if (!qrHttpAtla() &&
      (sonQr == 0 || (int32_t)(simdi - sonQr) >= (int32_t)QR_REFRESH_MS)) {
    sonQr = simdi;
    char kod[32];
    int ttl = 10;
    if (tabbyHttpQr(kod, sizeof kod, ttl)) qrYaz(kod, ttl, false);
  }
  uint32_t komutAralik = komutUcuVar ? KOMUT_POLL_MS : 8000;
  if (sonKomut != 0 && (int32_t)(simdi - sonKomut) < (int32_t)komutAralik) return;
  sonKomut = simdi;
  String yanit;
  int kod = 0;
  tabbyHttpKomutPoll(yanit, kod);
  if (kod == 404) {
    komutUcuVar = false;
    if (son404 == 0 || simdi - son404 > 60000) {
      son404 = simdi;
      Serial.println("Komut ucu yok: POST/GET /tabby-touch/komut");
    }
    return;
  }
  if (kod < 200) return;
  komutUcuVar = true;
  JsonDocument doc;
  if (deserializeJson(doc, yanit)) return;
  httpOtaNot(doc.as<JsonVariantConst>());
  if (!tabbyKomutKapiMi(doc.as<JsonVariantConst>())) return;
  kapiIste();
  int komutId = doc["komutId"] | 0;
  tabbyHttpKomutAlindi(komutId);
  Serial.printf("Uzaktan serbest komutId=%d\n", komutId);
#endif
}

static void otaBaslat() {
  if (gecisAktif || tabbyOtaSuruyor() || tabbyOtaOnayBekliyor()) return;
  if (otaBekliyor) {
    TabbyOtaIstek kopya = otaBekleyen;
    otaBekliyor = false;
    tabbyOtaImzali(kopya);
    return;
  }
#if TABBY_HTTP_FALLBACK
  if (httpOtaBekliyor && !mqtt.connected()) {
    httpOtaBekliyor = false;
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (next) tabbyOtaDenemeYaz((uint8_t)next->subtype);
    if (!tabbyHttpOtaEski(httpOtaUrl)) tabbyOtaDenemeSil();
  }
#endif
}

static void tabbyNetAdim() {
  if (WiFi.status() == WL_CONNECTED && !zamanKuruldu) {
    tabbyNtpKur();
    zamanKuruldu = true;
  }
  if (tabbyOtaSuruyor()) return;
  mqttBakim();
  tabbyMqttIsle();
  tabbyOtaGozet();
  if (tabbyOtaSuruyor()) return;
  olaySurdur();
  qrSureBak();
  httpBakim();
  gecisYurut();
  otaBaslat();
}

static void tabbyNetGorev(void *) {
  tabbyKriptoKendiniDene();
  kuyrukYukle();
  Serial.printf("MQTT broker %s:%u site=%s httpYedek=%d\n",
                TABBY_SITE_AKTIF.mqttHost, TABBY_SITE_AKTIF.mqttPort,
                TABBY_SITE_AKTIF.ad, TABBY_HTTP_FALLBACK);
  for (;;) {
    tabbyNetAdim();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void tabbyNetBaslat() {
  emirMux = xSemaphoreCreateMutex();
  gecisSem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(tabbyNetGorev, "tabby", 24576, nullptr, 1, nullptr, 0);
}
