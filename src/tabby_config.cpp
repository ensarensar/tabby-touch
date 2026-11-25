#include "tabby_config.h"

#if TABBY_SITE == 1
const TabbySite TABBY_SITE_AKTIF = {
    "kocaeli",
    "10.241.1.77",
    "",
    8000,
    "https://10.241.1.77:8000/tabby-touch",
    "https://10.241.1.77:8000/tabby-touch/gecis",
    "https://10.241.1.77:8000/tabby-touch/kod/",
    "https://10.241.1.77:8000/tabby-touch/komut",
    "https://10.241.1.77:8000/tabby-touch/qr",
    "https://10.241.1.77:8000/tabby-touch-hardbeard",
    "10.241.1.77",
    1883,
    "10.241.1.77",
};
#else
const TabbySite TABBY_SITE_AKTIF = {
    "izmir",
    "10.85.1.77",
    "tabbby.izmirsh.com",
    8000,
    "https://10.85.1.77:8000/tabby-touch",
    "https://10.85.1.77:8000/tabby-touch/gecis",
    "https://10.85.1.77:8000/tabby-touch/kod/",
    "https://10.85.1.77:8000/tabby-touch/komut",
    "https://10.85.1.77:8000/tabby-touch/qr",
    "https://tabbby.izmirsh.com:8000/tabby-touch-hardbeard",
    "10.85.1.77",
    1883,
    "10.85.1.77",
};
#endif

// YER TUTUCU. Gercek anahtar gelince bu diziyi degistir ve
// TABBY_ED25519_YER_TUTUCU degerini false yap.
// openssl pkey -in sunucu.pem -pubout -outform DER | tail -c 32 | xxd -i
const uint8_t TABBY_ED25519_ACIK_ANAHTAR[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
const bool TABBY_ED25519_YER_TUTUCU = true;

const char *const TABBY_CIHAZ_GIZLI = "";

const char TABBY_SERVER_CA_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBYerTutucuTabbyTouchBuSertifikaGecerliDegildir\n"
    "-----END CERTIFICATE-----\n";
