// Development only: fixed weather scenes for emulator screenshots and the store / Reddit reel.
// KASUMI_DEMO=<1..9> paints one scene; KASUMI_REEL=1 steps through REEL[] on every tap
// (`pebble emu-tap`), with a plausible clock per scene. Release builds never include this file.
#pragma once
#include "render.h"

typedef struct {
  int16_t clock;       // displayed time, minutes after midnight (reel only)
  int16_t m;           // minutes since sunrise "now"
  int16_t day_len;     // sunset - sunrise, minutes
  int16_t temp;        // °C
  uint8_t cond, wind10, precip, humidity, uv;
  uint16_t dir;
  int8_t amp10, trend10;
  int8_t daily[5];
  uint8_t hc[12];
  uint8_t dc[5];
} DemoScene;

#define S COND_SUN
#define P COND_PARTLY
#define C COND_CLOUDY
#define R COND_LRAIN
#define HR COND_HRAIN
#define SN COND_LSNOW
#define HS COND_HSNOW
static const DemoScene DEMO[] = {
  {460, 68, 617, 9, S, 10, 5, 84, 2, 20, 50, 0, {17, 15, 12, 14, 16}, {S, S, S, P, P, C, R, R, C, P, S, S}, {S, P, R, C, S}},           // 1 Kyoto morning
  {880, 444, 537, 11, R, 80, 80, 92, 1, 260, 30, -3, {12, 9, 7, 8, 10}, {R, R, HR, HR, R, C, C, P, P, C, R, R}, {R, HR, C, P, R}},     // 2 Prague rain
  {835, 193, 159, -6, SN, 50, 60, 86, 0, 200, 15, -2, {-4, -7, -9, -6, -3}, {SN, SN, C, C, SN, HS, HS, SN, C, C, P, P}, {SN, HS, C, SN, P}}, // 3 Tromso snow
  {750, 475, 863, 33, S, 30, 10, 68, 9, 160, 50, 0, {34, 33, 35, 31, 30}, {S, S, S, P, P, HR, R, P, S, S, S, S}, {S, S, P, HR, S}},      // 4 Tokyo noon
  {982, 590, 617, 12, P, 25, 10, 66, 0, 300, 40, -3, {15, 14, 12, 13, 16}, {P, P, S, S, S, S, S, S, P, P, C, C}, {P, S, S, C, R}},       // 5 Kyoto dusk
  {1025, 589, 537, 14, HR, 170, 95, 94, 0, 250, 20, -4, {15, 10, 8, 9, 11}, {HR, HR, HR, R, R, C, C, P, P, S, S, S}, {HR, R, C, P, S}},  // 6 storm
  {1395, 959, 537, 3, S, 5, 0, 70, 0, 90, 30, -1, {9, 11, 8, 6, 10}, {S, S, S, S, S, S, P, P, C, C, R, R}, {S, P, C, R, S}},            // 7 clear night
  {400, 8, 617, 7, S, 10, 5, 62, 1, 40, 50, 0, {16, 15, 13, 14, 15}, {S, S, S, S, P, P, S, S, S, P, P, C}, {S, S, P, C, S}},             // 8 clear dawn
  {620, 228, 617, 14, P, 115, 20, 55, 4, 270, 30, 0, {16, 14, 15, 12, 13}, {P, P, P, P, C, C, P, P, S, S, S, S}, {P, C, P, R, S}},      // 9 windy day
};
#undef S
#undef P
#undef C
#undef R
#undef HR
#undef SN
#undef HS
#define DEMO_COUNT ((int)(sizeof(DEMO) / sizeof(DEMO[0])))

// Fill the weather for scene idx as if it were `now`.
static void demo_scene(Weather *w, int idx, time_t now) {
  const DemoScene *d = &DEMO[idx % DEMO_COUNT];
  struct tm *lt = localtime(&now);
  float h0 = lt->tm_hour + lt->tm_min / 60.0f;
  w->valid = true;
  w->temp_c10 = d->temp * 10;
  w->unit_f = 0;
  w->has_daily = 1;
  w->cond = d->cond;
  w->wind10 = d->wind10;
  w->wind_dir = d->dir;
  w->precip = d->precip;
  w->humidity = d->humidity;
  w->uv = d->uv;
  w->sunrise = (int32_t)now - d->m * 60;
  w->sunset = w->sunrise + d->day_len * 60;
  for (int i = 0; i < 12; i++) {
    int32_t a = (int32_t)((h0 + i - 9) / 24.0f * TRIG_MAX_ANGLE), a0 = (int32_t)((h0 - 9) / 24.0f * TRIG_MAX_ANGLE);
    float v = d->temp + d->amp10 / 10.0f * ((float)(sin_lookup(a) - sin_lookup(a0)) / TRIG_MAX_RATIO) + d->trend10 / 10.0f * i;
    w->hourly[i] = (int8_t)(v + (v < 0 ? -0.5f : 0.5f));
    w->hcond[i] = d->hc[i];
  }
  for (int i = 0; i < 5; i++) { w->daily[i] = d->daily[i]; w->dcond[i] = d->dc[i]; }
  w->updated = (int32_t)now;
}

// A scene's own clock, today
static time_t demo_clock(int idx, time_t real) {
  struct tm *lt = localtime(&real);
  time_t mid = real - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
  return mid + DEMO[idx % DEMO_COUNT].clock * 60;
}

#ifdef KASUMI_DEMO
static void demo_fill(Weather *w, Settings *s) {
  demo_scene(w, KASUMI_DEMO - 1, time(NULL));
#ifdef KASUMI_DEMO_JOST
  s->digits = DIGITS_JOST;
#endif
#ifdef KASUMI_DEMO_SEALS
  s->forecast = FORECAST_SEALS;
#endif
}
#endif

#ifdef KASUMI_REEL
// scene (0-based), animation point, forecast style
typedef struct { uint8_t scene; float anim; uint8_t forecast; } ReelFrame;
static const ReelFrame REEL[] = {
  {7, 0, FORECAST_OFF},                                                         // dawn, red Fuji
  {0, 0, FORECAST_OFF},                                                         // morning
  {8, 0.05f, FORECAST_OFF}, {8, 0.25f, FORECAST_OFF}, {8, 0.45f, FORECAST_OFF}, {8, 0.65f, FORECAST_OFF}, {8, 0.85f, FORECAST_OFF},  // wind
  {3, 0, FORECAST_OFF},                                                         // hot noon, red Fuji
  {4, 0, FORECAST_OFF},                                                         // dusk
  {2, 0.1f, FORECAST_OFF}, {2, 0.3f, FORECAST_OFF}, {2, 0.5f, FORECAST_OFF}, {2, 0.7f, FORECAST_OFF},                               // snow, blue hour
  {6, 0, FORECAST_OFF},                                                         // night
  {1, 0.1f, FORECAST_OFF}, {1, 0.3f, FORECAST_OFF}, {1, 0.5f, FORECAST_OFF}, {1, 0.7f, FORECAST_OFF}, {1, 0.9f, FORECAST_OFF},       // rain
  {5, 0.05f, FORECAST_OFF}, {5, 0.16f, FORECAST_OFF}, {5, 0.19f, FORECAST_OFF}, {5, 0.23f, FORECAST_OFF}, {5, 0.32f, FORECAST_OFF},
  {5, 0.45f, FORECAST_OFF}, {5, 0.6f, FORECAST_OFF}, {5, 0.63f, FORECAST_OFF}, {5, 0.8f, FORECAST_OFF},                              // storm
  {0, 0, FORECAST_SERIES},                                                      // extras for the store: forecast views
  {1, 0.3f, FORECAST_SEALS},                                                    // and seals
};
#define REEL_COUNT ((int)(sizeof(REEL) / sizeof(REEL[0])))
#endif
