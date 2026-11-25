#include "tabby_ota.h"

#include "api_config.h"
#include "tabby_config.h"
#include "tabby_crypto.h"
#include "tabby_http.h"
#include "tabby_net.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "tabby_mmap.h"

#include <esp_ota_ops.h>
#include <stdio.h>
#include <string.h>

static bool deneme = false;
static bool onayli = false;
static uint32_t baslangicMs = 0;

static portMUX_TYPE ekranMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t ekranTur = 0;
static uint32_t ekranNesil = 0;
static char ekranBaslik[24];
static char ekranDetay[48];
static volatile bool suruyor = false;

static uint8_t indirmeTampon[1024];

void tabbyEkranAyarla(uint8_t tur, const char *baslik, const char *detay) {
  portENTER_CRITICAL(&ekranMux);
  ekranTur = tur;
  strncpy(ekranBaslik, baslik ? baslik : "", sizeof(ekranBaslik) - 1);
  ekranBaslik[sizeof(ekranBaslik) - 1] = 0;
  strncpy(ekranDetay, detay ? detay : "", sizeof(ekranDetay) - 1);
  ekranDetay[sizeof(ekranDetay) - 1] = 0;
  ekranNesil++;
  if (ekranNesil == 0) ekranNesil = 1;
  portEXIT_CRITICAL(&ekranMux);
}

bool tabbyEkranOku(char *baslik, size_t nb, char *detay, size_t nd, uint8_t *tur,
                   uint32_t *nesil) {
  if (!baslik || !detay || !tur || !nesil || nb == 0 || nd == 0) return false;
  portENTER_CRITICAL(&ekranMux);
  strncpy(baslik, ekranBaslik, nb - 1);
  strncpy(detay, ekranDetay, nd - 1);
  *tur = ekranTur;
  *nesil = ekranNesil;
  portEXIT_CRITICAL(&ekranMux);
  baslik[nb - 1] = 0;
  detay[nd - 1] = 0;
  return true;
}

void tabbyOtaSuruyorAyar(bool v) { suruyor = v; }
bool tabbyOtaSuruyor() { return suruyor; }

static void nvsYaz(bool dene, uint8_t slot) {
  Preferences p;
  if (!p.begin("tabby", false)) return;
  p.putBool("ota_dene", dene);
  if (dene) p.putUChar("ota_slot", slot);
  p.end();
}

void tabbyOtaDenemeYaz(uint8_t slot) { nvsYaz(true, slot); }

void tabbyOtaDenemeSil() {
  Preferences p;
  if (!p.begin("tabby", false)) return;
  p.putBool("ota_dene", false);
  p.end();
}

void tabbyOtaBoot() {
  baslangicMs = millis();
  const esp_partition_t *running = esp_ota_get_running_partition();
  Preferences p;
  bool bayrak = false;
  uint8_t slot = 0xff;
  if (p.begin("tabby", false)) {
    bayrak = p.getBool("ota_dene", false);
    slot = p.getUChar("ota_slot", 0xff);
    if (bayrak && running && running->subtype != (esp_partition_subtype_t)slot) {
      p.putBool("ota_dene", false);
      bayrak = false;
      Serial.println("OTA deneme bayragi bu slota ait degil, temizlendi");
    }
    p.end();
  }
  bool pending = false;
  if (running) {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
      pending = true;
    }
  }
  deneme = bayrak || pending;
  onayli = !deneme;
  Serial.printf("OTA deneme=%d pending=%d slot=%u\n", deneme ? 1 : 0,
                pending ? 1 : 0, running ? (unsigned)running->subtype : 0);
}

void tabbyOtaOnayla() {
  if (onayli) return;
  esp_ota_mark_app_valid_cancel_rollback();
  tabbyOtaDenemeSil();
  onayli = true;
  deneme = false;
  Serial.println("OTA imaj onaylandi (MQTT)");
}

bool tabbyOtaOnayBekliyor() { return deneme && !onayli; }

static void geriAl() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_partition_subtype_t digerTip =
      (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)
          ? ESP_PARTITION_SUBTYPE_APP_OTA_0
          : ESP_PARTITION_SUBTYPE_APP_OTA_1;
  const esp_partition_t *diger = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, digerTip, nullptr);
  esp_app_desc_t desc;
  if (!diger || esp_ota_get_partition_description(diger, &desc) != ESP_OK) {
    Serial.println("OTA geri alinacak imaj yok, bu imaj onaylandi");
    tabbyOtaOnayla();
    return;
  }
  Serial.printf("OTA geri aliniyor -> %s\n", diger->label);
  tabbyOtaDenemeSil();
  esp_ota_set_boot_partition(diger);
  delay(100);
  esp_restart();
}

void tabbyOtaGozet() {
  if (!deneme || onayli || suruyor) return;
  if (baslangicMs == 0) baslangicMs = millis();
  if ((int32_t)(millis() - baslangicMs) < (int32_t)TABBY_OTA_ONAY_MS) return;
  Serial.println("OTA MQTT onayi gelmedi, geri aliniyor");
  geriAl();
}

static void otaDurumYayin(const char *id, const char *asama, int yuzde,
                          const char *hata, const char *surum) {
  char govde[320];
  snprintf(govde, sizeof govde,
           "{\"id\":\"%s\",\"asama\":\"%s\",\"yuzde\":%d,\"hata\":\"%s\","
           "\"surum\":\"%s\"}",
           id ? id : "", asama ? asama : "", yuzde, hata ? hata : "",
           surum ? surum : "");
  tabbyMqttYayin(tabbyKonuOtaDurum(), govde, false);
  if (asama && strcmp(asama, "hata") == 0) {
    tabbyEkranAyarla(2, "Guncelleme hata", (hata && hata[0]) ? hata : asama);
  } else if (asama && strcmp(asama, "tamam") == 0) {
    tabbyEkranAyarla(1, "Guncelleniyor", "yeniden basliyor");
  } else {
    char detay[48];
    snprintf(detay, sizeof detay, "%s %d%%", asama ? asama : "", yuzde);
    tabbyEkranAyarla(1, "Guncelleniyor", detay);
  }
}

static bool imzaCoz(const char *b64, uint8_t imza[64]) {
  if (!b64 || !b64[0]) return false;
  uint8_t buf[96];
  int n = tabbyBase64Coz(b64, buf, sizeof buf);
  if (n != 64) return false;
  memcpy(imza, buf, 64);
  return true;
}

bool tabbyOtaImzali(const TabbyOtaIstek &istek) {
  const char *id = istek.id;
  const char *surum = istek.surum;
  if (!tabbyKriptoSaglam()) {
    otaDurumYayin(id, "hata", 0, "kripto oztest", surum);
    return false;
  }
  if (tabbyAnahtarYerTutucu()) {
    otaDurumYayin(id, "hata", 0, "acik anahtar yer tutucu", surum);
    return false;
  }
  if (strcmp(istek.varyant, TABBY_VARYANT) != 0) {
    otaDurumYayin(id, "hata", 0, "varyant uyusmuyor", surum);
    return false;
  }
  if (!tabbyUrlUygun(istek.url)) {
    otaDurumYayin(id, "hata", 0, "url host reddedildi", surum);
    return false;
  }
  if (!istek.surum[0] || !tabbyJeton(istek.surum, 1, 20)) {
    otaDurumYayin(id, "hata", 0, "surum yok", surum);
    return false;
  }
  int karsilastirma = tabbySurumKarsilastir(istek.surum, FIRMWARE_VERSION);
  if (karsilastirma == 0) {
    otaDurumYayin(id, "hata", 0, "ayni surum", surum);
    return false;
  }
  if (karsilastirma < 0) {
    otaDurumYayin(id, "hata", 0, "surum eski", surum);
    return false;
  }
  if (strlen(istek.sha256) < 64) {
    otaDurumYayin(id, "hata", 0, "sha256 yok", surum);
    return false;
  }
  if (istek.boyut > TABBY_OTA_SLOT_BOYUT) {
    otaDurumYayin(id, "hata", 0, "boyut sigmiyor", surum);
    return false;
  }

  const esp_partition_t *hedef = esp_ota_get_next_update_partition(nullptr);
  if (!hedef) {
    otaDurumYayin(id, "hata", 0, "ota slot yok", surum);
    return false;
  }

  tabbyOtaSuruyorAyar(true);
  otaDurumYayin(id, "indiriliyor", 0, "", surum);

  TabbyTlsIstemci istemci(true);
  istemci.setTimeout(30);

  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char *basliklar[] = {"X-Tabby-Sha256", "X-Tabby-Imza",
                             "X-Tabby-Signature", "X-Tabby-Ed25519"};
  http.collectHeaders(basliklar, 4);
  if (!http.begin(istemci, istek.url)) {
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 0, "indirilemedi", surum);
    return false;
  }
  http.addHeader("X-Tabby-Cihaz", tabbyMac());
  if (TABBY_CIHAZ_GIZLI && TABBY_CIHAZ_GIZLI[0]) {
    http.addHeader("X-Tabby-Gizli", TABBY_CIHAZ_GIZLI);
  }

  int httpKod = http.GET();
  if (httpKod != 200) {
    Serial.printf("OTA HTTP %d\n", httpKod);
    http.end();
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 0, "indirilemedi", surum);
    return false;
  }

  String baslikSha = http.header("X-Tabby-Sha256");
  String baslikImza = http.header("X-Tabby-Imza");
  if (!baslikImza.length()) baslikImza = http.header("X-Tabby-Signature");
  if (!baslikImza.length()) baslikImza = http.header("X-Tabby-Ed25519");
  if (!baslikImza.length()) baslikImza = istek.imza;

  int uzunluk = http.getSize();
  if (istek.boyut > 0 && uzunluk > 0 && (uint32_t)uzunluk != istek.boyut) {
    http.end();
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 0, "boyut uyusmuyor", surum);
    return false;
  }
  uint32_t beklenen = istek.boyut;
  if (beklenen == 0 && uzunluk > 0) beklenen = (uint32_t)uzunluk;
  if (beklenen > TABBY_OTA_SLOT_BOYUT) {
    http.end();
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 0, "boyut sigmiyor", surum);
    return false;
  }

  esp_ota_handle_t ota = 0;
  esp_err_t err = esp_ota_begin(
      hedef, beklenen ? beklenen : OTA_SIZE_UNKNOWN, &ota);
  if (err != ESP_OK) {
    http.end();
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 0, "yazilamadi", surum);
    return false;
  }

  tabbySha256Baslat();
  WiFiClient *akim = http.getStreamPtr();
  uint32_t toplam = 0;
  int sonYuzde = -1;
  uint32_t sonVeri = millis();
  bool yazmaHatasi = false;
  while (http.connected() || (akim && akim->available())) {
    size_t hazir = akim ? (size_t)akim->available() : 0;
    if (hazir) {
      size_t n = hazir > sizeof indirmeTampon ? sizeof indirmeTampon : hazir;
      int okunan = akim->readBytes(indirmeTampon, n);
      if (okunan <= 0) break;
      if (esp_ota_write(ota, indirmeTampon, (size_t)okunan) != ESP_OK) {
        yazmaHatasi = true;
        break;
      }
      tabbySha256Ekle(indirmeTampon, (size_t)okunan);
      toplam += (uint32_t)okunan;
      if (toplam > TABBY_OTA_SLOT_BOYUT) {
        yazmaHatasi = true;
        break;
      }
      sonVeri = millis();
      int yuzde = beklenen ? (int)((toplam * 100ull) / beklenen) : 0;
      if (yuzde > 100) yuzde = 100;
      if (yuzde >= sonYuzde + 5) {
        sonYuzde = yuzde;
        otaDurumYayin(id, "indiriliyor", yuzde, "", surum);
      }
    } else if (uzunluk >= 0 && (int)toplam >= uzunluk) {
      break;
    } else if (istek.boyut > 0 && toplam >= istek.boyut) {
      break;
    } else if (!http.connected() && (!akim || !akim->available())) {
      break;
    } else if ((int32_t)(millis() - sonVeri) > 20000) {
      yazmaHatasi = true;
      break;
    } else {
      tabbyMqttIsle();
      delay(10);
    }
    if (beklenen && toplam >= beklenen) break;
  }
  http.end();

  if (yazmaHatasi || toplam == 0 ||
      (istek.boyut > 0 && toplam != istek.boyut) ||
      (uzunluk > 0 && toplam != (uint32_t)uzunluk)) {
    esp_ota_abort(ota);
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", sonYuzde < 0 ? 0 : sonYuzde,
                  yazmaHatasi ? "yazilamadi" : "boyut uyusmuyor", surum);
    return false;
  }

  otaDurumYayin(id, "dogrulaniyor", 100, "", surum);
  tabbyMqttIsle();

  uint8_t ozet[32];
  tabbySha256Bitir(ozet);
  if (!tabbyHexEsit(istek.sha256, ozet)) {
    esp_ota_abort(ota);
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "sha256 uyusmuyor", surum);
    return false;
  }
  if (baslikSha.length() && !tabbyHexEsit(baslikSha.c_str(), ozet)) {
    esp_ota_abort(ota);
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "sha256 uyusmuyor", surum);
    return false;
  }

  uint8_t imza[64];
  if (!imzaCoz(baslikImza.c_str(), imza)) {
    esp_ota_abort(ota);
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "imza yok", surum);
    return false;
  }

  bool imzaOk = false;
  const void *harita = nullptr;
  esp_partition_mmap_handle_t haritaTuta = 0;
  if (esp_partition_mmap(hedef, 0, toplam, ESP_PARTITION_MMAP_DATA, &harita,
                         &haritaTuta) == ESP_OK &&
      harita) {
    imzaOk = tabbyEd25519Dogrula(TABBY_ED25519_ACIK_ANAHTAR, imza,
                                 (const uint8_t *)harita, toplam);
    esp_partition_munmap(haritaTuta);
    if (imzaOk) Serial.println("OTA imza: ham");
  }
  if (!imzaOk) {
    char hexK[65];
    char hexB[65];
    tabbyHexYaz(ozet, 32, hexK, false);
    tabbyHexYaz(ozet, 32, hexB, true);
    imzaOk = tabbyEd25519Dogrula(TABBY_ED25519_ACIK_ANAHTAR, imza,
                                 (const uint8_t *)hexK, 64) ||
             tabbyEd25519Dogrula(TABBY_ED25519_ACIK_ANAHTAR, imza,
                                 (const uint8_t *)hexB, 64);
    if (imzaOk) Serial.println("OTA imza: sha256 hex");
  }
  if (!imzaOk) {
    esp_ota_abort(ota);
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "imza gecersiz", surum);
    return false;
  }

  otaDurumYayin(id, "yaziliyor", 100, "", surum);
  err = esp_ota_end(ota);
  if (err != ESP_OK) {
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "imaj gecersiz", surum);
    return false;
  }
  tabbyOtaDenemeYaz((uint8_t)hedef->subtype);
  if (esp_ota_set_boot_partition(hedef) != ESP_OK) {
    tabbyOtaDenemeSil();
    tabbyOtaSuruyorAyar(false);
    otaDurumYayin(id, "hata", 100, "slot secilemedi", surum);
    return false;
  }
  otaDurumYayin(id, "tamam", 100, "", surum);
  tabbyMqttIsle();
  delay(400);
  Serial.printf("OTA tamam surum=%s bayt=%u\n", surum, (unsigned)toplam);
  esp_restart();
  return true;
}
