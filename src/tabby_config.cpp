#include "tabby_config.h"

#if TABBY_SITE == 1
const TabbySite TABBY_SITE_AKTIF = {
    "kocaeli",
    "10.241.1.77",
    8000,
    "https://10.241.1.77:8000/tabby-touch",
    "https://10.241.1.77:8000/tabby-touch/gecis",
    "https://10.241.1.77:8000/tabby-touch/kod/",
    "https://10.241.1.77:8000/tabby-touch/komut",
    "https://10.241.1.77:8000/tabby-touch/qr",
    "https://10.241.1.77:8000/tabby-touch-hardbeard",
    "10.241.1.77",
    1883,
    "10.241.1.254",
    "10.241.1.30",
    "10.241.1.130",
};
#else
const TabbySite TABBY_SITE_AKTIF = {
    "izmir",
    "10.85.1.77",
    8000,
    "https://10.85.1.77:8000/tabby-touch",
    "https://10.85.1.77:8000/tabby-touch/gecis",
    "https://10.85.1.77:8000/tabby-touch/kod/",
    "https://10.85.1.77:8000/tabby-touch/komut",
    "https://10.85.1.77:8000/tabby-touch/qr",
    "https://10.85.1.77:8000/tabby-touch-hardbeard",
    "10.85.1.77",
    1883,
    "10.85.1.254",
    "10.85.1.30",
    "",
};
#endif

// Ham 32 bayt. base64: iyEM1ZtMKRr67ZZlk5CtgXzUuNcGMEIOjQq6eDVhWWk=
// SHA-256(anahtar) on 16 hex: 7cb98f722c2fd74b
const uint8_t TABBY_ED25519_ACIK_ANAHTAR[32] = {
    0x8b, 0x21, 0x0c, 0xd5, 0x9b, 0x4c, 0x29, 0x1a, 0xfa, 0xed, 0x96,
    0x65, 0x93, 0x90, 0xad, 0x81, 0x7c, 0xd4, 0xb8, 0xd7, 0x06, 0x30,
    0x42, 0x0e, 0x8d, 0x0a, 0xba, 0x78, 0x35, 0x61, 0x59, 0x69,
};
const bool TABBY_ED25519_YER_TUTUCU = false;

const char *const TABBY_CIHAZ_GIZLI = "";

// CN=Tabby Local, SAN IP icermez. Kolonlu yazim:
// 4A:8E:E2:2D:D1:91:2A:81:19:10:80:A6:08:68:D8:A3:F8:57:A6:01:F1:D1:E7:F7:86:0B:BC:E5:81:CD:2D:A9
const uint8_t TABBY_TLS_PARMAK_IZI[32] = {
    0x4a, 0x8e, 0xe2, 0x2d, 0xd1, 0x91, 0x2a, 0x81, 0x19, 0x10, 0x80,
    0xa6, 0x08, 0x68, 0xd8, 0xa3, 0xf8, 0x57, 0xa6, 0x01, 0xf1, 0xd1,
    0xe7, 0xf7, 0x86, 0x0b, 0xbc, 0xe5, 0x81, 0xcd, 0x2d, 0xa9,
};
