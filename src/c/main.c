// Kasumi: a weather watchface painted in ink. Data comes from the Pebble app's weather plugin via PKJS.
#include <pebble.h>
#include "render.h"

#define PK_WEATHER 1
#define PK_SETTINGS 2
#define PK_FORMAT 3
#define PK_FIRST_RUN 4   // time of the first launch (for the plugin hint)
#define PK_HINT_OFF 5    // the user shook the hint away
#define FORMAT_VERSION 3

#define ANIM_FRAME_MS 66
#define LOOK_COOLDOWN_S 3
#define HINT_DAYS 2

static Window *s_window;
static Layer *s_canvas;
static Weather s_wx;
static Settings s_set;
static Fonts s_fonts;
static GFont s_time_mincho, s_time_jost;

static float s_anim;
static AppTimer *s_anim_timer;
static uint64_t s_anim_start;
static uint32_t s_anim_dur;

static int s_look_run;
static bool s_look_armed = true;
static time_t s_last_trigger;
static uint8_t s_subscribed_trigger = 0xFF;
static bool s_tap_on;

// The plugin hint shows only while no weather has ever arrived, for the first HINT_DAYS after the
// first launch, until a shake hides it.
static bool hint_wanted(void) {
#if defined(KASUMI_DEMO) || defined(KASUMI_REEL)
  return false;
#else
  if (s_wx.valid || persist_read_bool(PK_HINT_OFF)) return false;
  return time(NULL) - (time_t)persist_read_int(PK_FIRST_RUN) < HINT_DAYS * 24 * 3600;
#endif
}

#if defined(KASUMI_DEMO) || defined(KASUMI_REEL)
#include "demo.h"
#endif

// ------------------------------------------------------------------ helpers

static uint64_t now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint64_t)s * 1000 + ms;
}

static void apply_fonts(void) {
  s_fonts.time = s_set.digits == DIGITS_JOST ? s_time_jost : s_time_mincho;
}

static void save(void) {
  persist_write_int(PK_FORMAT, FORMAT_VERSION);
  persist_write_data(PK_WEATHER, &s_wx, sizeof(s_wx));
  persist_write_data(PK_SETTINGS, &s_set, sizeof(s_set));
}

static void load(void) {
  s_set = (Settings){L_ALL, DIGITS_MINCHO, DATE_KANJI, 1, TRIG_LOOK, 1, FORECAST_OFF, UNITS_AUTO};
  memset(&s_wx, 0, sizeof(s_wx));
  if (persist_read_int(PK_FORMAT) != FORMAT_VERSION) return;
  if (persist_exists(PK_SETTINGS)) persist_read_data(PK_SETTINGS, &s_set, sizeof(s_set));
  if (persist_exists(PK_WEATHER)) persist_read_data(PK_WEATHER, &s_wx, sizeof(s_wx));
}

// ------------------------------------------------------------------ animation

static void anim_step(void *data) {
  uint64_t t = now_ms() - s_anim_start;
  if (t >= s_anim_dur) {
    s_anim = 0;
    s_anim_timer = NULL;
  } else {
    s_anim = (float)t / (float)s_anim_dur;
    if (s_anim <= 0) s_anim = 0.001f;
    s_anim_timer = app_timer_register(ANIM_FRAME_MS, anim_step, NULL);
  }
  layer_mark_dirty(s_canvas);
}

static void start_animation(void) {
  if (s_set.trigger == TRIG_OFF || s_anim_timer) return;
  s_anim_dur = (render_is_storm(&s_wx, &s_set) && s_set.lightning) ? 2600 : 1800;
  s_anim_start = now_ms();
  s_anim = 0.001f;
  s_anim_timer = app_timer_register(ANIM_FRAME_MS, anim_step, NULL);
  layer_mark_dirty(s_canvas);
}

// ------------------------------------------------------------------ triggers

#ifdef KASUMI_REEL
static int s_reel;
static time_t s_fake_now;
static void reel_apply(void) {
  const ReelFrame *r = &REEL[s_reel];
  s_fake_now = demo_clock(r->scene, time(NULL));
  demo_scene(&s_wx, r->scene, s_fake_now);
  s_anim = r->anim;
  s_set.forecast = r->forecast;
  if (s_canvas) layer_mark_dirty(s_canvas);
}
static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_reel = (s_reel + 1) % REEL_COUNT;
  reel_apply();
}
#else
static void update_tap(void);
static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (hint_wanted()) {
    persist_write_bool(PK_HINT_OFF, true);
    render_set_hint(false);
    update_tap();
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }
  if (s_set.trigger == TRIG_TAP) start_animation();
}
#endif

static void backlight_handler(bool on) {
  if (on) start_animation();
}

// Looking at the watch: the face turns up towards you and holds still for a moment.
// Heuristic thresholds (milli-g); still to be tuned on a real wrist.
static void accel_handler(AccelData *data, uint32_t num) {
  for (uint32_t i = 0; i < num; i++) {
    if (data[i].did_vibrate) continue;
    int x = data[i].x, z = data[i].z;
    bool facing = z < -550 && x > -450 && x < 450;
    if (!facing) {
      s_look_run = 0;
      s_look_armed = true;
      continue;
    }
    if (++s_look_run >= 3 && s_look_armed) {
      s_look_armed = false;
      time_t now = time(NULL);
      if (now - s_last_trigger >= LOOK_COOLDOWN_S) {
        s_last_trigger = now;
        start_animation();
      }
    }
  }
}

static void subscribe_trigger(void) {
  if (s_subscribed_trigger == s_set.trigger) return;
  switch (s_subscribed_trigger) {
    case TRIG_LOOK: accel_data_service_unsubscribe(); break;
    case TRIG_LIGHT: backlight_service_unsubscribe(); break;
    default: break;
  }
  s_subscribed_trigger = s_set.trigger;
  switch (s_set.trigger) {
    case TRIG_LOOK:
      accel_data_service_subscribe(5, accel_handler);
      accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
      break;
    case TRIG_LIGHT: backlight_service_subscribe(backlight_handler); break;
    default: break;
  }
}

// The tap service serves both the shake trigger and hiding the plugin hint.
static void update_tap(void) {
  bool want = s_set.trigger == TRIG_TAP || hint_wanted();
  if (want == s_tap_on) return;
  s_tap_on = want;
  if (want) accel_tap_service_subscribe(tap_handler);
  else accel_tap_service_unsubscribe();
}

// ------------------------------------------------------------------ messages

static void copy_bytes(DictionaryIterator *it, uint32_t key, int8_t *dst, int n, bool is_signed) {
  Tuple *t = dict_find(it, key);
  if (!t || t->type != TUPLE_BYTE_ARRAY) return;
  const uint8_t *data = (const uint8_t *)t->value;
  for (int i = 0; i < n && i < t->length; i++) {
    int v = data[i];
    dst[i] = (int8_t)(is_signed ? v - 128 : v);
  }
}

static int32_t tint(DictionaryIterator *it, uint32_t key, int32_t fallback) {
  Tuple *t = dict_find(it, key);
  return t ? t->value->int32 : fallback;
}

static void inbox(DictionaryIterator *it, void *ctx) {
  bool changed = false;
#if defined(KASUMI_DEMO) || defined(KASUMI_REEL) || defined(KASUMI_NO_WEATHER)
  if (false) {  // a demo build keeps its fixed scene; KASUMI_NO_WEATHER acts as if the plugin were off
#else
  if (dict_find(it, MESSAGE_KEY_TEMP_C10)) {
#endif
    s_wx.valid = true;
    render_set_hint(false);
    update_tap();
    s_wx.temp_c10 = tint(it, MESSAGE_KEY_TEMP_C10, 0);
    s_wx.unit_f = tint(it, MESSAGE_KEY_UNIT_F, 0);
    s_wx.cond = tint(it, MESSAGE_KEY_COND, COND_UNKNOWN);
    s_wx.wind10 = tint(it, MESSAGE_KEY_WIND10, 0);
    s_wx.wind_dir = tint(it, MESSAGE_KEY_WIND_DIR, 0);
    s_wx.precip = tint(it, MESSAGE_KEY_PRECIP, 0);
    s_wx.humidity = tint(it, MESSAGE_KEY_HUMIDITY, 50);
    s_wx.uv = tint(it, MESSAGE_KEY_UV, 0);
    s_wx.sunrise = tint(it, MESSAGE_KEY_SUNRISE, 0);
    s_wx.sunset = tint(it, MESSAGE_KEY_SUNSET, 0);
    s_wx.updated = tint(it, MESSAGE_KEY_UPDATED, (int32_t)time(NULL));
    // Series the plugin did not send: quiet hills around the current temperature, no showers
    static const int8_t HILLS12[12] = {0, 1, 3, 2, 0, -1, 0, 2, 4, 3, 1, 0};
    static const int8_t HILLS5[5] = {0, 3, 1, 4, 2};
    int base = s_wx.temp_c10 / 10;
    for (int i = 0; i < 12; i++) { s_wx.hourly[i] = base + HILLS12[i]; s_wx.hcond[i] = COND_UNKNOWN; }
    for (int i = 0; i < 5; i++) { s_wx.daily[i] = base + HILLS5[i]; s_wx.dcond[i] = COND_UNKNOWN; }
    copy_bytes(it, MESSAGE_KEY_H_TEMP, s_wx.hourly, 12, true);
    s_wx.has_daily = dict_find(it, MESSAGE_KEY_D_HIGH) != NULL;
    copy_bytes(it, MESSAGE_KEY_D_HIGH, s_wx.daily, 5, true);
    copy_bytes(it, MESSAGE_KEY_H_COND, (int8_t *)s_wx.hcond, 12, false);
    copy_bytes(it, MESSAGE_KEY_D_COND, (int8_t *)s_wx.dcond, 5, false);
    changed = true;
  }
#ifdef KASUMI_REEL
  if (false) {
#else
  if (dict_find(it, MESSAGE_KEY_S_LAYERS)) {
#endif
    s_set.layers = tint(it, MESSAGE_KEY_S_LAYERS, L_ALL);
    s_set.digits = tint(it, MESSAGE_KEY_S_DIGITS, s_set.digits);
    s_set.date_style = tint(it, MESSAGE_KEY_S_DATE, s_set.date_style);
    s_set.seal_kanji = tint(it, MESSAGE_KEY_S_KANJI, s_set.seal_kanji);
    s_set.trigger = tint(it, MESSAGE_KEY_S_TRIGGER, s_set.trigger);
    s_set.lightning = tint(it, MESSAGE_KEY_S_LIGHTNING, s_set.lightning);
    s_set.forecast = tint(it, MESSAGE_KEY_S_FORECAST, s_set.forecast);
    s_set.units = tint(it, MESSAGE_KEY_S_UNITS, s_set.units);
#ifdef KASUMI_DEMO_JOST
    s_set.digits = DIGITS_JOST;
#endif
#ifdef KASUMI_DEMO_SEALS
    s_set.forecast = FORECAST_SEALS;
#endif
    apply_fonts();
    subscribe_trigger();
    update_tap();
    changed = true;
  }
  if (dict_find(it, MESSAGE_KEY_DEMO)) start_animation();
  if (changed) {
    save();
    layer_mark_dirty(s_canvas);
  }
}

static void say_hello(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int8(out, MESSAGE_KEY_HELLO, 1);
  app_message_outbox_send();
}

// ------------------------------------------------------------------ window

static uint32_t s_frames, s_frame_ms;

static void update_proc(Layer *layer, GContext *ctx) {
  uint64_t t0 = now_ms();
#ifdef KASUMI_REEL
  time_t now = s_fake_now;
#else
  time_t now = time(NULL);
#endif
  render_scene(ctx, layer_get_bounds(layer), &s_wx, &s_set, &s_fonts, now, s_anim);
  int32_t dt = (int32_t)(now_ms() - t0);
  if (dt < 0) dt = 0;
  if (s_anim > 0) { s_frames++; s_frame_ms += dt; }
  else if (s_frames) {
    APP_LOG(APP_LOG_LEVEL_INFO, "animation: %lu frames, %lu ms per frame", (unsigned long)s_frames,
            (unsigned long)(s_frame_ms / s_frames));
    s_frames = s_frame_ms = 0;
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "still frame %lu ms", (unsigned long)dt);
  }
}

static void tick(struct tm *t, TimeUnits changed) {
  render_set_hint(hint_wanted());  // the hint expires after HINT_DAYS
  update_tap();
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) { layer_destroy(s_canvas); }

static void init(void) {
  load();
#ifdef KASUMI_DEMO
  demo_fill(&s_wx, &s_set);
#endif
#define FONT(id) fonts_load_custom_font(resource_get_handle(id))
  s_time_mincho = FONT(PBL_IF_ROUND_ELSE(RESOURCE_ID_FONT_MINCHO_TIME_54, RESOURCE_ID_FONT_MINCHO_TIME_47));
  s_time_jost = FONT(PBL_IF_ROUND_ELSE(RESOURCE_ID_FONT_JOST_TIME_54, RESOURCE_ID_FONT_JOST_TIME_47));
  s_fonts.date = FONT(RESOURCE_ID_FONT_JOST_DATE_15);
  s_fonts.small = FONT(RESOURCE_ID_FONT_JOST_SMALL_10);
  s_fonts.week = FONT(PBL_IF_ROUND_ELSE(RESOURCE_ID_FONT_MINCHO_WEEK_14, RESOURCE_ID_FONT_MINCHO_WEEK_18));
  s_fonts.week_small = FONT(RESOURCE_ID_FONT_MINCHO_WEEK_11);
  s_fonts.seal_kanji = FONT(PBL_IF_ROUND_ELSE(RESOURCE_ID_FONT_SEAL_KANJI_23, RESOURCE_ID_FONT_SEAL_KANJI_21));
  // seal numbers in Jost: the Gothic 7 reads like a katakana ア at this size
  s_fonts.seal_num = s_fonts.date;
  s_fonts.seal_small = s_fonts.small;
#undef FONT
  apply_fonts();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_stack_push(s_window, false);

  tick_timer_service_subscribe(MINUTE_UNIT, tick);
#ifdef KASUMI_REEL
  s_set = (Settings){L_ALL, DIGITS_MINCHO, DATE_KANJI, 1, TRIG_TAP, 1, FORECAST_OFF, UNITS_C};
  apply_fonts();
  reel_apply();
#endif
  subscribe_trigger();
  if (!persist_exists(PK_FIRST_RUN)) persist_write_int(PK_FIRST_RUN, (int32_t)time(NULL));
  render_set_hint(hint_wanted());
  update_tap();

  app_message_register_inbox_received(inbox);
  app_message_open(512, 64);
  say_hello();

#ifdef KASUMI_DEMO_ANIM
  s_anim = KASUMI_DEMO_ANIM;
#endif
}

static void deinit(void) {
  if (s_anim_timer) app_timer_cancel(s_anim_timer);
  window_destroy(s_window);
  fonts_unload_custom_font(s_time_mincho);
  fonts_unload_custom_font(s_time_jost);
  fonts_unload_custom_font(s_fonts.date);
  fonts_unload_custom_font(s_fonts.small);
  fonts_unload_custom_font(s_fonts.week_small);
  fonts_unload_custom_font(s_fonts.week);
  fonts_unload_custom_font(s_fonts.seal_kanji);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
