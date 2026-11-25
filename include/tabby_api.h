#pragma once

#include <Arduino.h>

struct ApiResult {
  int httpCode = 0;
  bool ok = false;
  bool success = false;
  bool found = false;
  bool verified = false;
  bool pinBekleniyor = false;
  bool kayit = false;
  bool yetkili = false;
  int oturumId = 0;
  int ttlSaniye = 0;
  String regno;
  String isim;
  String soyisim;
  String adSoyad;
  String message;
  String yon;
};
