#pragma once
#include <pebble.h>

// condition_code from the weather plugin, in the order PKJS sends it
enum { COND_SUN, COND_PARTLY, COND_CLOUDY, COND_LRAIN, COND_HRAIN,
       COND_LSNOW, COND_HSNOW, COND_MIX, COND_UNKNOWN };

// Settings bits: which weather inputs may change the picture
enum {
  L_SKY = 1, L_TEMP = 2, L_CLOUDS = 4, L_PRECIP = 8, L_WIND = 16,
  L_MIST = 32, L_UV = 64, L_RIDGES = 128, L_SHOWERS = 256, L_STALE = 512
};
#define L_ALL 1023

enum { TRIG_TAP, TRIG_LOOK, TRIG_LIGHT, TRIG_OFF };
enum { DATE_KANJI, DATE_LATIN, DATE_OFF };
enum { DIGITS_MINCHO, DIGITS_JOST };
enum { FORECAST_SERIES, FORECAST_SEALS, FORECAST_OFF };
enum { UNITS_AUTO, UNITS_C, UNITS_F };

typedef struct {
  bool valid;
  int16_t temp_c10;   // °C x10
  uint8_t unit_f;     // the Pebble app shows °F
  uint8_t cond;
  uint16_t wind10;    // m/s x10
  uint16_t wind_dir;  // degrees the wind blows from
  uint8_t precip;     // chance, %
  uint8_t humidity;   // %
  uint8_t uv;
  int32_t sunrise;    // unix seconds, today at the place
  int32_t sunset;
  int8_t hourly[12];  // °C from the current hour on
  int8_t daily[5];    // daily highs °C from today on
  uint8_t dcond[5];   // condition per day
  uint8_t has_daily;  // the plugin sent a daily series (else no forecast is drawn)
  uint8_t hcond[12];  // condition per hour
  int32_t updated;    // unix seconds of the reading
} Weather;

typedef struct {
  uint16_t layers;
  uint8_t digits;
  uint8_t date_style;
  uint8_t seal_kanji;
  uint8_t trigger;
  uint8_t lightning;
  uint8_t forecast;
  uint8_t units;
} Settings;

typedef struct {
  GFont time;
  GFont date;        // Jost 15: day number, Latin weekday
  GFont small;       // Jost 10: forecast labels
  GFont week;        // Mincho: weekday kanji in the date cartouche
  GFont week_small;  // Mincho 11: weekday kanji in the forecast
  GFont seal_kanji;
  GFont seal_num;
  GFont seal_small;  // forecast seals
} Fonts;

// anim: 0 for the still frame, 0..1 while the shake / glance animation runs
void render_scene(GContext *ctx, GRect bounds, const Weather *w, const Settings *s,
                  const Fonts *f, time_t now, float anim);
bool render_is_storm(const Weather *w, const Settings *s);
// Draw the "turn on the plugins preview" hint over the picture.
void render_set_hint(bool on);
