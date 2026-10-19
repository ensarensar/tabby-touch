#pragma once

/*
 * ESP32-2432S028 (CYD) — tabby-touch
 * Kart okuyucu yok. Touch dahili: 32 / 39 / 25 / 33
 */

static const int TOUCH_MOSI = 32;
static const int TOUCH_MISO = 39;
static const int TOUCH_CLK  = 25;
static const int TOUCH_CS   = 33;
static const int TOUCH_IRQ  = 36;

// Role SYN: IO22. U=3.3V, GND=GND. HIGH = cek, 10 sn LOW.
// Sicil, buton ve sunucu komutu ayni rolePulse() ile acar.
static const int RELAY_PIN = 22;
static const uint32_t RELAY_ON_MS = 10000;
static const bool RELAY_ACTIVE_HIGH = true;

// 10k pull-up: IO35 -- 10k -- IO21 (IO21 zaten 3.3V, ekran isigi).
// Buton: IO35 -- GND. Basinca IO27 tetik + IO22 role.
// P3: GND | IO35 | IO22 | IO21
static const int BUTTON_PIN = 35;
static const int TRIGGER_PIN = 27;
static const uint32_t BUTTON_DEBOUNCE_MS = 40;
static const uint32_t BUTTON_COOLDOWN_MS = 800;
