#pragma once

#include <Arduino.h>

// TFT varsayilan font Turkce glif icermez.
// UTF-8 (g, u, s, i, o, c ve noktali/kuyruklu harfler) -> ASCII.

inline char mapTrCodepoint(uint32_t cp) {
  switch (cp) {
    case 0x00E7: case 0x0107: return 'c'; // ç ć
    case 0x00C7: case 0x0106: return 'C'; // Ç Ć
    case 0x011F: return 'g';              // ğ
    case 0x011E: return 'G';              // Ğ
    case 0x0131: return 'i';              // ı
    case 0x0130: return 'I';              // İ
    case 0x00F6: case 0x0151: return 'o'; // ö ő
    case 0x00D6: case 0x0150: return 'O'; // Ö Ő
    case 0x015F: return 's';              // ş
    case 0x015E: return 'S';              // Ş
    case 0x00FC: case 0x0171: return 'u'; // ü ű
    case 0x00DC: case 0x0170: return 'U'; // Ü Ű
    case 0x00E2: return 'a';              // â
    case 0x00C2: return 'A';              // Â
    case 0x00EE: return 'i';              // î
    case 0x00CE: return 'I';              // Î
    case 0x00FB: return 'u';              // û
    case 0x00DB: return 'U';              // Û
    case 0x00A0: return ' ';              // nbsp
    default: return 0;
  }
}

inline String utf8ToAsciiTr(const String &in) {
  String out;
  out.reserve(in.length());
  const uint8_t *p = reinterpret_cast<const uint8_t *>(in.c_str());
  while (*p) {
    if (*p < 0x80) {
      out += static_cast<char>(*p++);
      continue;
    }

    uint32_t cp = 0;
    int n = 0;
    if ((*p & 0xE0) == 0xC0 && p[1]) {
      cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      n = 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      cp = ((uint32_t)(p[0] & 0x0F) << 12) |
           ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      n = 3;
    } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
      n = 4;
    } else {
      n = 1;
    }
    p += n;

    char mapped = mapTrCodepoint(cp);
    if (mapped) out += mapped;
    else if (cp >= 0x20 && cp < 0x7F) out += static_cast<char>(cp);
  }
  return out;
}
