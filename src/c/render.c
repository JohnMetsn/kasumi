// Kasumi renderer: the weather as a view of Mount Fuji in the manner of Hokusai's Thirty-six Views.
// Woodblock language: flat fields, a dark key line, carved rib lines, skies in bands with short
// dithered seams (bokashi). All colours are Pebble's 64; the frame buffer is written directly.
// Port of the browser concept (watchfaces/kasumi/concept/kasumi-concept.html).
#include "render.h"

#define K_BLACK    GColorBlackARGB8
#define K_OXFORD   GColorOxfordBlueARGB8
#define K_SUMI     GColorDarkGrayARGB8
#define K_GRAY     GColorLightGrayARGB8
#define K_WHITE    GColorWhiteARGB8
#define K_PRUSSIAN GColorCobaltBlueARGB8
#define K_PICTON   GColorPictonBlueARGB8
#define K_CELESTE  GColorCelesteARGB8
#define K_LIBERTY  GColorLibertyARGB8
#define K_BABYBLUE GColorBabyBlueEyesARGB8
#define K_MIDGREEN GColorMidnightGreenARGB8
#define K_ARMY     GColorArmyGreenARGB8
#define K_PASTEL   GColorPastelYellowARGB8
#define K_RAJAH    GColorRajahARGB8
#define K_CHROME   GColorChromeYellowARGB8
#define K_RED      GColorRedARGB8
#define K_WINDSOR  GColorWindsorTanARGB8
#define K_BULGAR   GColorBulgarianRoseARGB8
#define K_MELON    GColorMelonARGB8
#define K_ORANGE   GColorOrangeARGB8
#define K_NONE     0

#define MAXW 260
#define PI_F 3.14159265f

enum { PH_NIGHT, PH_BLUE, PH_DAWN, PH_DAY, PH_DUSK };
enum { MODE_BLUE, MODE_RED, MODE_BLACK, MODE_NIGHT };

// ------------------------------------------------------------------ small maths (no FPU on the watch)

static const uint8_t BAY[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline int qd(float t) { return t <= 0 ? 0 : (int)(t * 16.0f + 0.5f); }
static inline bool dq(int x, int y, int q) { return BAY[((y & 3) << 2) | (x & 3)] < q; }
static inline bool dq2(int x, int y, int q) { return BAY[(((y + 1) & 3) << 2) | ((x + 2) & 3)] < q; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline int ifloor(float x) { int i = (int)x; return (x < (float)i) ? i - 1 : i; }
static inline int iround(float x) { return ifloor(x + 0.5f); }
static inline float fabsf_(float x) { return x < 0 ? -x : x; }

static float fsin(float r) {
  int32_t a = (int32_t)(r * ((float)TRIG_MAX_ANGLE / (2.0f * PI_F)));
  a %= TRIG_MAX_ANGLE;
  if (a < 0) a += TRIG_MAX_ANGLE;
  return (float)sin_lookup(a) / (float)TRIG_MAX_RATIO;
}
static float fcos(float r) { return fsin(r + PI_F / 2.0f); }
static float fsqrt(float v) {
  if (v <= 0) return 0;
  float g = v > 1 ? v / 2 : 1;
  for (int i = 0; i < 6; i++) g = 0.5f * (g + v / g);
  return g;
}

typedef struct { uint32_t a; } Rng;
static float rnd(Rng *r) {
  r->a += 0x6D2B79F5u;
  uint32_t t = r->a;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;
}
static float hash(int i, int s) {
  uint32_t h = ((uint32_t)i ^ ((uint32_t)s * 0x9E3779B1u)) * 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return (float)(h >> 8) / 16777216.0f;
}
static float vnoise(float x, int s) {
  int i = ifloor(x);
  float f = x - i, u = (1 - fcos(f * PI_F)) / 2;
  return hash(i, s) * (1 - u) + hash(i + 1, s) * u;
}
static float fnoise(float x, int s) {
  return vnoise(x, s) * 0.6f + vnoise(x * 2.1f, s + 7) * 0.28f + vnoise(x * 4.3f, s + 13) * 0.12f;
}

// ------------------------------------------------------------------ frame buffer

static GBitmapDataRowInfo s_rows[MAXW];
static int s_w, s_h;

static inline void put(int x, int y, uint8_t c) {
  if (!c || y < 0 || y >= s_h) return;
  GBitmapDataRowInfo *r = &s_rows[y];
  if (x < r->min_x || x > r->max_x) return;
  r->data[x] = c;
}
static inline uint8_t get(int x, int y) {
  y = clampi(y, 0, s_h - 1);
  GBitmapDataRowInfo *r = &s_rows[y];
  if (x < r->min_x || x > r->max_x) return K_BLACK;
  return r->data[x];
}
static inline float lum(uint8_t c) {
  return (0.3f * ((c >> 4) & 3) + 0.59f * ((c >> 2) & 3) + 0.11f * (c & 3)) / 3.0f;
}
static bool capture(GContext *ctx, GBitmap **fb) {
  *fb = graphics_capture_frame_buffer(ctx);
  if (!*fb) return false;
  for (int y = 0; y < s_h; y++) s_rows[y] = gbitmap_get_data_row_info(*fb, y);
  return true;
}

typedef uint8_t (*ColorFn)(int x, int y);
static uint8_t s_pen;
static ColorFn s_penfn;
static inline void pen(uint8_t c) { s_pen = c; s_penfn = NULL; }
static inline void penfn(ColorFn f) { s_penfn = f; }
static inline void plot(int x, int y) { put(x, y, s_penfn ? s_penfn(x, y) : s_pen); }

static void line(float fx0, float fy0, float fx1, float fy1) {
  int x0 = iround(fx0), y0 = iround(fy0), x1 = iround(fx1), y1 = iround(fy1);
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int n = 0; n < 2000; n++) {
    plot(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
static void disc(float fcx, float fcy, float r) {
  int cx = iround(fcx), cy = iround(fcy), R = (int)r + 1;
  for (int y = -R; y <= R; y++)
    for (int x = -R; x <= R; x++)
      if (x * x + y * y <= r * r + r * 0.6f) plot(cx + x, cy + y);
}
static void ring(float fcx, float fcy, float r, bool dotted) {
  int cx = iround(fcx), cy = iround(fcy), R = (int)r + 2;
  float lo = (r - 0.5f) * (r - 0.5f), hi = (r + 0.5f) * (r + 0.5f);
  for (int y = -R; y <= R; y++)
    for (int x = -R; x <= R; x++) {
      int d = x * x + y * y;
      if (d >= lo && d <= hi && (!dotted || ((cx + x + cy + y) & 1))) plot(cx + x, cy + y);
    }
}
static void ellipse(float cx, float cy, float rx, float ry) {
  int n = (int)(rx * 7) + 1;
  for (int i = 0; i < n; i++) {
    float a = (float)i / n * 2 * PI_F;
    plot(iround(cx + fcos(a) * rx), iround(cy + fsin(a) * ry));
  }
}
static void fill_rect(int x0, int y0, int w, int h, uint8_t c) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) put(x, y, c);
}
static void frame_rect(int x0, int y0, int w, int h, uint8_t c) {
  for (int x = x0; x < x0 + w; x++) { put(x, y0, c); put(x, y0 + h - 1, c); }
  for (int y = y0; y < y0 + h; y++) { put(x0, y, c); put(x0 + w - 1, y, c); }
}

// ------------------------------------------------------------------ layout

typedef struct {
  int w, h; bool round; float sc;
  int fcx, ftop, fbase, fhb, fsw;                 // Fuji: centre, summit, foot, half base, summit half width
  int hill_top0, hill_top1, hill_base, water, row_gap;
  float sun_cx, sun_ax, sun_cy, sun_ay; int sun_r;
  GRect cart, dcart;
  int seal_right, seal_bottom, seal_w, seal_fc_right, seal_fc_bottom;
  int mack0, mack1;
  int fc_x, fc_y, fc_w, fc_ph, fc_gap, fc_lab;
} Layout;

#ifndef PBL_ROUND
static const Layout LAY = {200, 228, false, 1.0f, 116, 96, 170, 150, 9, 160, 180, 188, 188, 7,
                           100, 90, 150, 74, 9, {{4, 4}, {154, 60}}, {{161, 4}, {35, 60}},
                           195, 222, 30, 0, 0, 70, 112, 4, 192, 29, 18, 3, 11};
#else
static const Layout LAY = {260, 260, true, 1.3f, 140, 134, 198, 200, 11, 190, 210, 216, 216, 8,
                           130, 112, 150, 124, 11, {{46, 42}, {168, 60}}, {{90, 106}, {80, 20}},
                           190, 236, 32, 222, 194, 40, 120, 59, 200, 26, 16, 3, 11};
#endif
static const Layout *L = &LAY;

typedef struct { float x, y; } FPt;
static FPt sun_at(float p) {
  float a = (p - 0.5f) * PI_F;
  return (FPt){L->sun_cx + L->sun_ax * fsin(a), L->sun_cy - L->sun_ay * fcos(a)};
}

static const float COV[9] = {0, 0.25f, 0.7f, 0.6f, 0.85f, 0.6f, 0.8f, 0.7f, 0.3f};
static const int8_t FLAT12[12] = {10, 11, 13, 12, 10, 9, 10, 12, 14, 13, 11, 10};
static bool is_rain(uint8_t c) { return c == COND_LRAIN || c == COND_HRAIN || c == COND_MIX; }
static bool is_snow(uint8_t c) { return c == COND_LSNOW || c == COND_HSNOW || c == COND_MIX; }

bool render_is_storm(const Weather *w, const Settings *s) {
  if (!w->valid) return false;
  float wind = (s->layers & L_WIND) ? w->wind10 / 10.0f : 0;
  return w->cond == COND_HRAIN && (wind >= 10 || w->precip >= 90);
}

static float moon_phase(time_t now) {
  float k = (float)(now - 947182440) / 86400.0f / 29.530588f;  // since the new moon of 2000-01-06
  return k - ifloor(k);
}

// sky: flat bands with a short dithered seam between them
typedef struct { uint8_t end_pct; uint8_t c; } Band;
static void sky_bands(int y0, int y1, int hz, const Band *b, int n, int seam) {
  for (int y = y0; y < y1 && y < s_h; y++) {
    int k = 0;
    while (k < n - 1 && y >= hz * b[k].end_pct / 100) k++;
    uint8_t c = b[k].c, c0 = k > 0 ? b[k - 1].c : c;
    int d = k > 0 ? y - hz * b[k - 1].end_pct / 100 : seam;
    int q = d < seam ? (d * 16 + 8) / seam : 16;
    uint8_t *row = s_rows[y].data;
    for (int x = s_rows[y].min_x; x <= s_rows[y].max_x; x++) row[x] = (q >= 16 || dq(x, y, q)) ? c : c0;
  }
}

// Fuji's profile: a small flat summit, then concave flanks
static float fuji_top(float x, bool notch) {
  float d = fabsf_(x - L->fcx);
  if (d <= L->fsw) {
    int xi = iround(x - L->fcx);
    return L->ftop + ((notch && (xi == -3 || xi == 4)) ? 2 : 0);
  }
  float u = (d - L->fsw) / (L->fhb - L->fsw);
  if (u > 1) u = 1;
  return L->fbase - (L->fbase - L->ftop) * (1 - u) * (1 - u);
}

static void zig(float x, float y, int n, Rng *r, float sc) {
  float px = x, py = y;
  for (int i = 0; i < n; i++) {
    float nx = px + ((i & 1) ? 3 : -3) * sc + (rnd(r) - 0.5f) * 2, ny = py + (3 + rnd(r) * 3) * sc;
    line(px, py, nx, ny);
    line(px + 1, py, nx + 1, ny);
    px = nx; py = ny;
  }
}

static float interp(const int8_t *arr, int n, int x) {
  float f = (float)x / (s_w - 1) * (n - 1);
  int i0 = ifloor(f);
  float u = (1 - fcos((f - i0) * PI_F)) / 2;
  float va = arr[clampi(i0, 0, n - 1)], vb = arr[clampi(i0 + 1, 0, n - 1)];
  return va * (1 - u) + vb * u;
}

// pens needing scene state
static int16_t s_top[MAXW];
static uint8_t s_rib;
static uint8_t pen_rib(int x, int y) { return (x >= 0 && x < s_w && y >= s_top[x] + 1) ? s_rib : K_NONE; }
static uint8_t pen_rain(int x, int y) { return lum(get(x, y)) > 0.55f ? K_PRUSSIAN : K_CELESTE; }
static int s_mx0, s_mx1, s_my1;
static uint8_t s_mrain;
static uint8_t pen_mini_rain(int x, int y) { return (x >= s_mx0 && x < s_mx1 && y < s_my1) ? s_mrain : K_NONE; }

// One small print of a coming day: sky from the day's condition, a little Fuji, snowline from the day's high
static void mini_view(int x0, int y0, int w, int h, int hi_c, uint8_t dc, bool today) {
  bool grey = COV[dc] >= 0.55f, storm = dc == COND_HRAIN, sn = is_snow(dc), rn = is_rain(dc);
  uint8_t top = storm ? K_OXFORD : grey ? K_GRAY : K_PICTON, bot = storm ? K_SUMI : grey ? K_WHITE : K_CELESTE;
  int split = y0 + h * 45 / 100, x, y;
  for (y = y0; y < y0 + h; y++)
    for (x = x0; x < x0 + w; x++) put(x, y, (y < split || (y == split && dq(x, y, 8))) ? top : bot);
  if (dc == COND_PARTLY) {
    for (x = x0 + 3; x < x0 + 9; x++) put(x, y0 + 4, K_WHITE);
    for (x = x0 + w - 10; x < x0 + w - 4; x++) put(x, y0 + 6, K_WHITE);
  }
  if (grey && !storm) for (x = x0 + 1; x < x0 + w - 1; x++) put(x, y0 + h * 55 / 100, K_WHITE);
  float cx = x0 + w * 0.56f, ft = y0 + h * 0.3f, base = y0 + h - 3, hb = w * 0.52f;
  bool red = hi_c >= 22 && !grey && !rn && !sn;
  uint8_t col = storm ? K_BLACK : red ? K_WINDSOR : K_PRUSSIAN;
  float snow_f = clampf(0.40f - (hi_c - 5) * 0.016f, 0.10f, 0.55f);
  if (red && snow_f > 0.2f) snow_f = 0.2f;
  if (sn) snow_f = snow_f + 0.1f > 0.6f ? 0.6f : snow_f + 0.1f;
  for (x = x0; x < x0 + w; x++) {
    float d = fabsf_(x - cx);
    if (d > hb) continue;
    float u = d / hb;
    int ty = iround(base - (base - ft) * (1 - u) * (1 - u));
    for (y = ty; y < base; y++) put(x, y, y < ft + (base - ft) * snow_f + ((x % 3) == 0 ? 1 : 0) ? K_WHITE : col);
  }
  for (y = (int)base; y < y0 + h; y++)
    for (x = x0; x < x0 + w; x++) put(x, y, y == (int)base ? K_MIDGREEN : K_PRUSSIAN);
  if (rn) {
    s_mx0 = x0; s_mx1 = x0 + w; s_my1 = y0 + h - 3; s_mrain = storm ? K_CELESTE : K_PRUSSIAN;
    penfn(pen_mini_rain);
    for (int k = 0; k < w + h; k += 3) line(x0 + k, y0, x0 + k - 6, y0 + 6);
  }
  if (sn) {
    Rng rs = {(uint32_t)(w * h + x0)};
    for (int k = 0; k < 10; k++) put(x0 + 1 + (int)(rnd(&rs) * (w - 2)), y0 + 1 + (int)(rnd(&rs) * (h - 5)), K_WHITE);
  }
  if (storm) { pen(K_RED); line(x0 + w * 0.3f, y0 + 2, x0 + w * 0.24f, y0 + 6); line(x0 + w * 0.24f, y0 + 6, x0 + w * 0.32f, y0 + 9); }
  frame_rect(x0, y0, w, h, today ? K_RED : K_PRUSSIAN);
}

static void seal_box(int x0, int y0, int w, int h, uint8_t col, uint32_t seed) {
  Rng r = {seed};
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
      float v = rnd(&r);
      if (edge && v < 0.18f) continue;
      bool inner = ((x == 2 || x == w - 3) && y >= 2 && y <= h - 3) || ((y == 2 || y == h - 3) && x >= 2 && x <= w - 3);
      put(x0 + x, y0 + y, (inner && rnd(&r) < 0.82f) ? K_WHITE : col);
    }
}

// a print's title cartouche: fill, a double border
static void cartouche(GRect R, uint8_t fill, uint8_t border) {
  int x0 = R.origin.x, y0 = R.origin.y, w = R.size.w, h = R.size.h;
  fill_rect(x0, y0, w, h, fill);
  frame_rect(x0, y0, w, h, border);
  frame_rect(x0 + 2, y0 + 2, w - 4, h - 4, border);
}

// ------------------------------------------------------------------ text helpers

static void text_at(GContext *ctx, const char *str, GFont font, int x, int y, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, str, font, GRect(x, y, 260, 120), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}
static GSize text_size(const char *str, GFont font) {
  return graphics_text_layout_get_content_size(str, font, GRect(0, 0, 260, 120), GTextOverflowModeFill,
                                               GTextAlignmentLeft);
}
// Pebble text boxes start above the glyphs; centre on the visible part (roughly 62 % down the box)
static void text_center(GContext *ctx, const char *str, GFont font, int cx, int cy, GColor c) {
  GSize sz = text_size(str, font);
  text_at(ctx, str, font, cx - sz.w / 2, cy - sz.h * 62 / 100, c);
}

static const char *const WK_KANJI[7] = {"日", "月", "火", "水", "木", "金", "土"};
static const char *const WK_EN[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static bool s_hint;
static const char *const KANJI[9] = {"晴", "曇", "曇", "雨", "雨", "雪", "雪", "霙", "空"};

// ------------------------------------------------------------------ the scene

void render_scene(GContext *ctx, GRect bounds, const Weather *w, const Settings *s,
                  const Fonts *f, time_t now, float anim) {
  s_w = L->w; s_h = L->h;
  const int W = L->w, H = L->h;
  const float sc = L->sc;
  // Without weather the picture stays quiet, but the sky still follows the clock (default sun times).
  const uint16_t lay = w->valid ? s->layers : (s->layers & L_SKY);
  struct tm *lt = localtime(&now);
  int x, y, i;

  // --- inputs, with a quiet default picture when there is no data yet
  int32_t sr = w->sunrise, ss = w->sunset;
  if (!w->valid || !(lay & L_SKY) || w->sunrise == 0 || w->sunset <= w->sunrise) {
    time_t mid = now - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
    sr = mid + 6 * 3600 + 30 * 60;
    ss = mid + 18 * 3600;
  }
  float m = (float)(now - sr) / 60.0f, M = (float)(ss - sr) / 60.0f;
  int phase = PH_DAY;
  if (lay & L_SKY) {
    if (m >= 50 && m < M - 50) phase = PH_DAY;
    else if (m >= -20 && m < 50) phase = PH_DAWN;
    else if (m >= M - 50 && m < M + 20) phase = PH_DUSK;
    else if ((m >= -65 && m < -20) || (m >= M + 20 && m < M + 70)) phase = PH_BLUE;
    else phase = PH_NIGHT;
  }
  bool dark = phase == PH_NIGHT || phase == PH_BLUE;
  uint8_t cond = w->valid ? w->cond : COND_UNKNOWN;
  float temp = (lay & L_TEMP) ? w->temp_c10 / 10.0f : 12;
  float cov = (lay & L_CLOUDS) ? COV[cond] : 0;
  float wind = (lay & L_WIND) ? w->wind10 / 10.0f : 0;
  float dir_x = (lay & L_WIND) ? fsin((w->wind_dir + 180) * PI_F / 180.0f) : 0;
  uint32_t seed_min = (uint32_t)(now / 60), seed_hour = (uint32_t)(now / 3600);
  int minute_of_hour = (int)((now / 60) % 60);
  bool storm = render_is_storm(w, s) && (lay & L_PRECIP);
  bool rainy = (lay & L_PRECIP) && is_rain(cond), snowing = (lay & L_PRECIP) && is_snow(cond);
  bool use_f = s->units == UNITS_F || (s->units == UNITS_AUTO && w->unit_f);
  // which Fuji: black under a storm (Sanka hakuu), red on a clear dawn, dusk or hot day (Gaifu kaisei), else blue
  int mode = storm ? MODE_BLACK : dark ? MODE_NIGHT
           : (cov < 0.4f && (phase == PH_DAWN || phase == PH_DUSK || temp >= 22)) ? MODE_RED : MODE_BLUE;

  GBitmap *fb;
  if (!capture(ctx, &fb)) return;

  // --- sky
  static const Band SK_DAY[] = {{10, K_PRUSSIAN}, {24, K_PICTON}, {42, K_CELESTE}, {100, K_WHITE}};
  static const Band SK_RED[] = {{16, K_PRUSSIAN}, {40, K_PICTON}, {62, K_CELESTE}, {100, K_WHITE}};
  static const Band SK_DAWN[] = {{12, K_PICTON}, {34, K_CELESTE}, {62, K_WHITE}, {84, K_MELON}, {100, K_RAJAH}};
  static const Band SK_DUSK[] = {{14, K_LIBERTY}, {36, K_MELON}, {68, K_RAJAH}, {100, K_CHROME}};
  static const Band SK_BLUE[] = {{22, K_OXFORD}, {55, K_LIBERTY}, {100, K_BABYBLUE}};
  static const Band SK_NIGHT[] = {{30, K_BLACK}, {100, K_OXFORD}};
  static const Band SK_GREY[] = {{14, K_SUMI}, {40, K_GRAY}, {100, K_WHITE}};
  static const Band SK_STORM[] = {{30, K_OXFORD}, {62, K_SUMI}, {100, K_GRAY}};
  const Band *sk;
  int nsk;
#define PICK(a) do { sk = a; nsk = sizeof(a) / sizeof(a[0]); } while (0)
  if (phase == PH_NIGHT) PICK(SK_NIGHT);
  else if (phase == PH_BLUE) PICK(SK_BLUE);
  else if (storm) PICK(SK_STORM);
  else if (cov >= 0.55f) PICK(SK_GREY);
  else if (phase == PH_DAWN) PICK(SK_DAWN);
  else if (phase == PH_DUSK) PICK(SK_DUSK);
  else if (mode == MODE_RED) PICK(SK_RED);
  else PICK(SK_DAY);
#undef PICK
  sky_bands(0, L->water, L->fbase, sk, nsk, (int)(6 * sc));
  if (phase == PH_NIGHT && cov < 0.35f) {
    Rng rs = {(uint32_t)(now / 86400) * 31 + 7};
    for (i = 0; i < (int)(22 * sc); i++) {
      int sx = (int)(rnd(&rs) * W), sy = (int)(rnd(&rs) * (L->ftop - 6));
      put(sx, sy, rnd(&rs) < 0.3f ? K_BABYBLUE : K_WHITE);
    }
  }

  // --- sun or moon: flat discs, red when low
  FPt sun = {0, 0};
  bool sun_on = false;
  float light_x = W * 0.2f;
  if (lay & L_SKY) {
    if (!dark) {
      float p = m / M;
      if (p > -0.04f && p < 1.04f && cov < 0.75f) {
        sun = sun_at(clampf(p, 0, 1));
        sun_on = true;
        light_x = sun.x;
        bool low = phase == PH_DAWN || phase == PH_DUSK;
        if ((lay & L_UV) && phase == PH_DAY) {
          int nr = w->uv >= 8 ? 3 : w->uv >= 6 ? 2 : w->uv >= 3 ? 1 : 0;
          pen(w->uv >= 8 ? K_RED : K_ORANGE);
          for (i = 1; i <= nr; i++) ring(sun.x, sun.y, L->sun_r + 3 * i + 1, true);
        }
        pen(low ? K_RED : K_CHROME);  // a golden disc by day: a pale one vanished on the white sky
        disc(sun.x, sun.y, L->sun_r);
        if (!low) { pen(K_ORANGE); ring(sun.x, sun.y, L->sun_r, false); }
      }
    } else if (cov < 0.8f) {
      float pn = m >= M ? (m - M) / (1440.0f - M) : (m + 1440.0f - M) / (1440.0f - M);
      if (pn >= 0 && pn <= 1) {
        FPt mo = sun_at(pn);
        light_x = mo.x;
        float r = L->sun_r - 2, k = moon_phase(now), off = k < 0.5f ? -4 * r * k : 4 * r * (1 - k);
        int mx = iround(mo.x), my = iround(mo.y), R = (int)r + 1;
        for (y = -R; y <= R; y++)
          for (x = -R; x <= R; x++)
            if (x * x + y * y <= r * r + r * 0.6f && (x - off) * (x - off) + y * y > r * r) put(mx + x, my + y, K_PASTEL);
      }
    }
  }

  // --- mackerel sky (uroko-gumo) when partly cloudy: rows of small scales, stretched by the wind
  if ((lay & L_CLOUDS) && cond == COND_PARTLY) {
    Rng rm = {seed_hour * 3 + 1};
    uint8_t mc = dark ? K_LIBERTY : phase == PH_DUSK ? K_PASTEL : K_WHITE;
    float stretch = 1 + (wind / 6 > 2 ? 2 : wind / 6);
    int row = 0;
    for (int yy = L->mack0; yy < L->mack1; row++, yy += (int)(7 * sc)) {
      float step = 12 * sc * stretch, xs = (row & 1) * 6 * sc - (float)((int)(minute_of_hour * 0.2f * wind) % (int)(12 * sc));
      for (float xm = xs; xm < W + 10; xm += step) {
        if (rnd(&rm) < 0.35f) continue;
        float rx = (2 + rnd(&rm) * 1.5f) * sc * stretch, ry = 1.2f * sc;
        for (int ey = -(int)ry; ey <= (int)ry; ey++)
          for (int ex = -(int)rx; ex <= (int)rx; ex++)
            if (ex * ex / (rx * rx) + ey * ey / (ry * ry) <= 1) put(iround(xm) + ex, yy + ey, mc);
      }
    }
  }

  // --- Fuji
  bool shadow_right = light_x < L->fcx;
  float FH = L->fbase - L->ftop;
  float snow_frac = clampf(0.40f - (temp - 5) * 0.016f, 0.10f, 0.50f);
  if ((lay & L_PRECIP) && (cond == COND_LSNOW || cond == COND_HSNOW)) snow_frac = snow_frac + 0.1f > 0.55f ? 0.55f : snow_frac + 0.1f;
  if (mode == MODE_RED && snow_frac > 0.18f) snow_frac = 0.18f;
  uint8_t c_lit, c_rib, c_snow, c_snow_sh, c_key, c_forest;
  switch (mode) {
    case MODE_RED: c_lit = K_WINDSOR; c_rib = K_BULGAR; c_snow = K_WHITE; c_snow_sh = K_CELESTE; c_key = K_BULGAR; c_forest = K_ARMY; break;
    case MODE_BLACK: c_lit = K_OXFORD; c_rib = K_BLACK; c_snow = K_WHITE; c_snow_sh = K_BABYBLUE; c_key = K_BLACK; c_forest = K_BLACK; break;
    case MODE_NIGHT:
      if (phase == PH_BLUE) { c_lit = K_OXFORD; c_rib = K_BLACK; c_snow = K_WHITE; c_snow_sh = K_BABYBLUE; c_key = K_BLACK; }
      else { c_lit = K_BLACK; c_rib = K_OXFORD; c_snow = K_LIBERTY; c_snow_sh = K_OXFORD; c_key = K_LIBERTY; }
      c_forest = K_BLACK; break;
    default: c_lit = K_PRUSSIAN; c_rib = K_OXFORD; c_snow = K_WHITE; c_snow_sh = K_CELESTE; c_key = K_OXFORD; c_forest = K_MIDGREEN; break;
  }
  float rib_w = 5 * sc;
  int stripe = (int)(4 * sc);
  for (x = 0; x < W; x++) {
    float ty = fuji_top(x, true);
    s_top[x] = (int16_t)iround(ty);
    float rx = (x - L->fcx + 400) / rib_w;
    int rib = ifloor(rx);
    float loc = rx - rib, tp = 1 - fabsf_(2 * loc - 1);
    tp = tp * (0.6f + 0.4f * tp);
    float drip = (hash(rib, 77) * 0.55f + (rib % 3 == 0 ? 0.3f : 0)) * FH * snow_frac * tp + (fnoise(x / 3.0f, 41) - 0.5f) * 2 * sc;
    int snow_y = (int)(L->ftop + FH * snow_frac * 0.7f + drip);
    bool shade = shadow_right ? x > L->fcx : x < L->fcx;
    int forest_y = (int)(L->ftop + FH * 0.80f);
    for (y = s_top[x]; y < L->fbase; y++) {
      uint8_t c;
      if (y < snow_y) c = (shade && ((x + (y * 3) / 10) % stripe) == 0) ? c_snow_sh : c_snow;
      else if (y > forest_y && mode != MODE_BLACK) c = dq(x, y, 4) ? c_rib : c_forest;
      else c = c_lit;
      put(x, y, c);
    }
  }
  // ribs: carved lines down the flanks, dense on the side away from the light
  s_rib = c_rib;
  penfn(pen_rib);
  Rng rr = {4711};
  for (int j = -60; j <= 60; j++) {
    float xs0 = L->fcx + j * 2.5f * sc;
    if (xs0 < -20 || xs0 > W + 20 || (j < 2 && j > -2)) continue;
    bool shd = shadow_right ? j > 0 : j < 0;
    if (!shd && (j % 4) != 0) continue;
    float tyy = fuji_top(xs0, false);
    float sy0 = L->ftop + FH * snow_frac * 0.9f + hash(j, 5) * 6;
    if (sy0 < tyy + 2) sy0 = tyy + 2;
    float len = (10 + rnd(&rr) * 28) * sc, xe = xs0 + (xs0 - L->fcx) * 0.22f;
    float ye = sy0 + len, ymax = L->fbase * 0.8f + L->ftop * 0.2f;
    line(xs0, sy0, xe, ye < ymax ? ye : ymax);
  }
  // key line along the silhouette
  for (x = 1; x < W; x++) {
    int a = s_top[x - 1], b = s_top[x];
    for (y = a < b ? a : b; y <= (a > b ? a : b); y++) if (y < L->fbase) put(x, y, c_key);
  }

  // --- cloud banks across the mountain: flat fill, scalloped top, key line
  int nb = (lay & L_CLOUDS) ? (cov >= 0.55f ? iround(cov * 4) : cond == COND_UNKNOWN ? 1 : 0) : 0;
  if ((lay & L_MIST) && w->humidity > 70) nb += w->humidity > 88 ? 2 : 1;
  uint8_t cloud_c = dark ? K_LIBERTY : storm ? K_GRAY : K_WHITE, cloud_k = dark ? K_OXFORD : storm ? K_SUMI : K_PRUSSIAN;
  Rng rb = {seed_hour * 7 + 3};
  for (i = 0; i < nb; i++) {
    float yc = L->ftop + FH * (0.35f + rnd(&rb) * 0.5f), len = (70 + rnd(&rb) * 90) * sc, bh = (6 + rnd(&rb) * 4) * sc, r = bh * 0.55f;
    float drift = (minute_of_hour * (0.3f + wind * 0.35f) + anim * (12 + wind * 2) * sc) * (dir_x >= 0 ? 1 : -1);
    float x0 = rnd(&rb) * (W + len * 0.5f) - len * 0.5f + drift;
    while (x0 > W) x0 -= W + len;
    while (x0 + len < 0) x0 += W + len;
    float period = 2 * r;
    for (int xi = 0; xi <= (int)len; xi++) {
      int X = iround(x0 + xi);
      float ph = xi - ifloor(xi / period) * period - r;
      float top = yc - bh / 2 + r - fsqrt(r * r - ph * ph), bot = yc + bh / 2;
      float e = xi < len - xi ? xi : len - xi;
      if (e < r) { float k2 = (r - e) / r; top += k2 * k2 * bh * 0.6f; bot -= k2 * k2 * bh * 0.3f; }
      for (y = iround(top); y <= iround(bot); y++) put(X, y, cloud_c);
      put(X, iround(top), cloud_k);
      put(X, iround(bot), cloud_k);
    }
    if (wind > 8) {
      for (int k = 0; k < 2; k++) {
        float sy = yc - 1 + k * 3, sx = dir_x >= 0 ? x0 + len : x0, sl = wind * 1.6f * sc;
        for (int q = 0; q < sl; q += 3) {
          put(iround(sx + (dir_x >= 0 ? q : -q)), iround(sy), cloud_k);
          put(iround(sx + (dir_x >= 0 ? q + 1 : -q - 1)), iround(sy), cloud_k);
        }
      }
    }
  }
  // Sanka hakuu: red lightning at the foot of a black Fuji
  if (mode == MODE_BLACK) {
    Rng rz = {seed_hour * 11 + 3};
    pen(K_RED);
    zig(L->fcx + FH * 0.5f, L->ftop + FH * 0.42f, 6, &rz, sc * 1.15f);
    zig(L->fcx - FH * 0.45f, L->ftop + FH * 0.55f, 4, &rz, sc);
  }

  // --- distant showers over the plain, in the hours that will be wet
  const int8_t *hourly = (lay & L_RIDGES) ? w->hourly : FLAT12;
  if ((lay & L_SHOWERS) && (lay & L_PRECIP)) {
    uint8_t cc = dark ? K_LIBERTY : K_PRUSSIAN;
    float step = (W - 16) / 11.0f, lean = 0.35f + clampf(wind / 9, 0, 1.2f) * fabsf_(dir_x) * 0.4f;
    int sg = dir_x < 0 ? -1 : 1;
    for (i = 1; i < 12; i++) {
      uint8_t hc = w->hcond[i];
      if (!is_rain(hc) && !is_snow(hc)) continue;
      float xc = 8 + i * step;
      for (int hx = iround(xc - step * 0.8f); hx <= iround(xc + step * 0.8f); hx++) {
        int q = qd((1 - fabsf_(hx - xc) / (step * 0.8f)) * 0.8f);
        for (y = (int)(L->fbase - 26 * sc); y < L->hill_base; y++) {
          int ph = (hx + iround((y - L->fbase) * lean) * sg) % 3;
          if (ph < 0) ph += 3;
          if (ph == 0 && dq(hx, y, q)) put(hx, y, cc);
        }
      }
    }
  }

  // --- foreground hills: the next 12 hours of temperature, flat green with a key line and pines
  float lo = 999, hi = -999;
  for (i = 0; i < 12; i++) { if (hourly[i] < lo) lo = hourly[i]; if (hourly[i] > hi) hi = hourly[i]; }
  if (hi - lo < 8) { float mid = (hi + lo) / 2; lo = mid - 4; hi = mid + 4; }
  uint8_t hill_c = dark ? K_BLACK : temp < 0 ? K_OXFORD : temp > 24 ? K_ARMY : K_MIDGREEN, hill_k = dark ? K_OXFORD : K_BLACK;
  bool snowy = snowing || ((lay & L_TEMP) && temp <= -3);
  static int16_t hill_y[MAXW];
  for (x = 0; x < W; x++) {
    float nv = (interp(hourly, 12, x) - lo) / (hi - lo);
    hill_y[x] = (int16_t)iround(L->hill_top1 - nv * (L->hill_top1 - L->hill_top0) + (fnoise(x / 19.0f, 29) - 0.5f) * 4 * sc);
    for (y = hill_y[x]; y < L->hill_base; y++) put(x, y, (snowy && y - hill_y[x] < 3 * sc) ? K_WHITE : hill_c);
  }
  for (x = 1; x < W; x++) {
    int a = hill_y[x - 1], b = hill_y[x];
    for (y = a < b ? a : b; y <= (a > b ? a : b); y++) put(x, y, hill_k);
  }
  Rng rpn = {99};
  for (i = 0; i < 9; i++) {
    int px = (int)(rnd(&rpn) * W), py = hill_y[clampi(px, 0, W - 1)], ph = (int)((4 + rnd(&rpn) * 5) * sc);
    for (int k = 0; k < ph; k++) {
      int wd = (int)((ph - k) * 0.45f);
      for (int dx = -wd; dx <= wd; dx++) put(px + dx, py - k, hill_k);
    }
    put(px, py + 1, hill_k);
  }
  if (dark) {  // one warm window in the hills
    int hx = (int)(W * 0.3f), hy = hill_y[hx] + (int)(5 * sc);
    put(hx, hy, K_CHROME); put(hx + 1, hy, K_CHROME); put(hx, hy + 1, K_CHROME); put(hx + 1, hy + 1, K_CHROME);
  }

  // --- water: Prussian blue with white wave lines; straight in calm air, curling crests in wind
  uint8_t water_c = dark ? (phase == PH_NIGHT ? K_BLACK : K_OXFORD) : storm ? K_OXFORD : K_PRUSSIAN;
  uint8_t wave_c = dark ? K_LIBERTY : K_WHITE;
  for (y = L->water; y < H; y++)
    for (x = s_rows[y].min_x; x <= s_rows[y].max_x; x++) s_rows[y].data[x] = water_c;
  float A = clampf(wind / 3.2f, 0, 4.5f) * sc, lam = (46 - wind * 1.9f) * sc;
  if (lam < 12 * sc) lam = 12 * sc;
  int skw = dir_x >= 0 ? 1 : -1;
  pen(wave_c);
  for (int kr = 0, y0 = L->water + 4; y0 < H + 6; kr++, y0 += L->row_gap) {
    float phw = kr * 1.7f + (minute_of_hour * 0.08f * (1 + wind * 0.2f) + anim * (4 + wind * 0.5f)) * (-skw);
    float px = -1, py = 0;
    for (x = 0; x < W; x++) {
      float th = 2 * PI_F * x / lam + phw, sv = fsin(th + 0.6f * fsin(th) * skw), yy = y0 + A * sv;
      if (px >= 0) line(px, py, x, yy); else plot(x, iround(yy));
      if (wind >= 8 && sv < -0.97f) {
        int cr = (int)(2 * sc);
        for (int c2 = 0; c2 <= cr; c2++) plot(x + skw * c2, iround(yy) - c2);
        plot(x + skw * (cr + 1), iround(yy) - cr + 1);
      }
      px = x; py = yy;
    }
    if (sun_on && (phase == PH_DAWN || phase == PH_DUSK)) {
      float hl = (8 - kr * 1.2f) * sc;
      if (hl > 1) { pen(K_CHROME); line(sun.x - hl, y0 + 1, sun.x + hl, y0 + 1); pen(wave_c); }
    }
  }
  if (rainy) {
    Rng rp2 = {seed_min * 13 + 5};
    int nrip = (int)(w->precip / 100.0f * (cond == COND_HRAIN ? 10 : 5) * sc) + 1;
    for (i = 0; i < nrip; i++) {
      float cx = rnd(&rp2) * W, cy = L->water + 5 + rnd(&rp2) * (H - L->water - 9), rx = (2.5f + rnd(&rp2) * 3) * sc;
      ellipse(cx, cy, rx, rx * 0.35f);
    }
  }

  // --- the round window: frame and a bead where the wind comes from
  if (L->round) {
    pen(dark ? K_LIBERTY : K_OXFORD);
    ring(130, 130, 123, false);
    if ((lay & L_WIND) && wind >= 1) {
      float a = w->wind_dir * PI_F / 180.0f, bx = 130 + 123 * fsin(a), by = 130 - 123 * fcos(a);
      pen(K_WHITE); disc(bx, by, 3.5f);
      pen(K_OXFORD); ring(bx, by, 4, false);
      float tl = wind * 0.9f + 4;
      if (tl > 18) tl = 18;
      for (int st = 6; st < 6 + tl; st += 2) plot(iround(bx - fsin(a) * st), iround(by + fcos(a) * st));
    }
  }

  // --- rain: fine straight strokes; snow: white flakes
  if (lay & L_PRECIP) {
    Rng rp = {seed_min * 17 + 1};
    bool heavy = cond == COND_HRAIN || cond == COND_HSNOW;
    float pf = 0.35f + w->precip / 100.0f * 0.65f, slant = clampf(wind / 9, 0, 1.4f) * dir_x;
    if (is_rain(cond)) {
      int n = (int)((heavy ? 130 : 50) * pf * sc * sc * (cond == COND_MIX ? 0.5f : 1));
      float rl = (heavy ? 13 : 9) * sc, mv = anim * H * 1.6f, span_y = H + rl, span_x = W + 60;
      penfn(pen_rain);
      for (i = 0; i < n; i++) {
        float rx0 = rnd(&rp) * (W + 60) - 30, ry0 = rnd(&rp) * (H + rl) - rl;
        ry0 = ry0 + rl + mv; ry0 -= ifloor(ry0 / span_y) * span_y; ry0 -= rl;
        rx0 = rx0 + 30 + slant * mv; rx0 -= ifloor(rx0 / span_x) * span_x; rx0 -= 30;
        line(rx0, ry0, rx0 + slant * rl, ry0 + rl);
      }
    }
    if (is_snow(cond)) {
      int n = (int)((heavy ? 110 : 45) * pf * sc * sc * (cond == COND_MIX ? 0.5f : 1));
      float mv = anim * H * 0.5f;
      for (i = 0; i < n; i++) {
        float fx = rnd(&rp) * (W + 40) - 20, fy = rnd(&rp) * H;
        bool big = rnd(&rp) < 0.35f;
        fy += mv; fy -= ifloor(fy / H) * H;
        fx = fx + 20 + slant * mv + fsin(fy / 9) * 2; fx -= ifloor(fx / (W + 40)) * (W + 40); fx -= 20;
        int X = iround(fx), Y = iround(fy);
        if (lum(get(X, Y)) > 0.8f) put(X + 1, Y + 1, K_PRUSSIAN);
        put(X, Y, K_WHITE);
        if (big) { put(X - 1, Y, K_WHITE); put(X + 1, Y, K_WHITE); put(X, Y - 1, K_WHITE); put(X, Y + 1, K_WHITE); }
      }
    }
  }

  // --- lightning flash during the animation
  if (anim > 0 && storm && s->lightning) {
    static const float FL[3] = {0.16f, 0.23f, 0.6f};
    for (int fi = 0; fi < 3; fi++) {
      float dtf = anim - FL[fi];
      if (dtf < 0 || dtf > 0.06f) continue;
      int qf = qd(0.7f * (1 - dtf / 0.06f));
      for (y = 0; y < L->fbase; y++) {
        uint8_t *row = s_rows[y].data;
        for (x = s_rows[y].min_x; x <= s_rows[y].max_x; x++)
          if (y < s_top[x] && dq2(x, y, qf)) row[x] = K_WHITE;
      }
      Rng rl = {seed_min * 29 + (fi == 1 ? 0 : fi) * 7};  // flashes 0 and 1 are one double flash
      pen(K_RED);
      zig(W * (0.15f + rnd(&rl) * 0.7f), 6 * sc, 9, &rl, sc * 1.3f);
    }
  }

  // --- cartouches for the time and date (print titles), opaque over the rain
  uint8_t cart_fill = dark ? K_OXFORD : K_PASTEL, cart_border = dark ? K_BABYBLUE : K_PRUSSIAN;
  cartouche(L->cart, cart_fill, cart_border);
  if (s->date_style != DATE_OFF) cartouche(L->dcart, cart_fill, cart_border);

  // --- 5-day forecast: five small views, or five seals
  bool fc_on = w->valid && w->has_daily && s->forecast != FORECAST_OFF && (lay & L_RIDGES);
  if (fc_on) {
    for (i = 0; i < 5; i++) {
      int fx0 = L->fc_x + i * (L->fc_w + L->fc_gap);
      if (s->forecast == FORECAST_SERIES) {
        mini_view(fx0, L->fc_y, L->fc_w, L->fc_ph, w->daily[i], w->dcond[i], i == 0);
        fill_rect(fx0, L->fc_y + L->fc_ph, L->fc_w, L->fc_lab, dark ? K_OXFORD : K_PASTEL);
      } else {
        seal_box(fx0, L->fc_y, L->fc_w, L->fc_ph + L->fc_lab, w->daily[i] >= 15 ? K_RED : K_PRUSSIAN, 900 + i);
      }
    }
  }

  // --- main seal frame
  int sw = L->seal_w, sh = s->seal_kanji ? (sw * 162) / 100 : sw;
  int seal_r = (fc_on && L->seal_fc_right) ? L->seal_fc_right : L->seal_right;
  int seal_b = (fc_on && L->seal_fc_right) ? L->seal_fc_bottom : L->seal_bottom;
  int sx0 = seal_r - sw, sy0 = seal_b - sh;
  if (!s_hint) seal_box(sx0, sy0, sw, sh, K_RED, 4242);  // the hint box takes its place

  graphics_release_frame_buffer(ctx, fb);

  // --- text: time and date in the cartouches
  GColor ink = dark ? GColorWhite : GColorBlack;
  char tbuf[8];
  strftime(tbuf, sizeof(tbuf), clock_is_24h_style() ? "%H:%M" : "%I:%M", lt);
  const char *tstr = (!clock_is_24h_style() && tbuf[0] == '0') ? tbuf + 1 : tbuf;
  GRect C1 = L->cart, C2 = L->dcart;
  text_center(ctx, tstr, f->time, C1.origin.x + C1.size.w / 2, C1.origin.y + C1.size.h / 2, ink);
  char dnum[4];
  snprintf(dnum, sizeof(dnum), "%d", lt->tm_mday);
  if (s->date_style == DATE_KANJI) {
    if (L->round) {
      GSize ks = text_size(WK_KANJI[lt->tm_wday], f->week), ns = text_size(dnum, f->date);
      int total = ks.w + 5 + ns.w, dx = C2.origin.x + (C2.size.w - total) / 2, cy = C2.origin.y + C2.size.h / 2;
      text_center(ctx, WK_KANJI[lt->tm_wday], f->week, dx + ks.w / 2, cy, ink);
      text_center(ctx, dnum, f->date, dx + ks.w + 5 + ns.w / 2, cy, ink);
    } else {
      int cx = C2.origin.x + C2.size.w / 2;
      text_center(ctx, dnum, f->date, cx, C2.origin.y + 16, ink);
      text_center(ctx, WK_KANJI[lt->tm_wday], f->week, cx, C2.origin.y + 40, ink);
    }
  } else if (s->date_style == DATE_LATIN) {
    if (L->round) {
      char dl[12];
      snprintf(dl, sizeof(dl), "%s %d", WK_EN[lt->tm_wday], lt->tm_mday);
      text_center(ctx, dl, f->date, C2.origin.x + C2.size.w / 2, C2.origin.y + C2.size.h / 2, ink);
    } else {
      int cx = C2.origin.x + C2.size.w / 2;
      text_center(ctx, WK_EN[lt->tm_wday], f->small, cx, C2.origin.y + 18, ink);
      text_center(ctx, dnum, f->date, cx, C2.origin.y + 40, ink);
    }
  }

  // --- forecast labels: weekday and high when every one fits its cell, else the highs alone
  if (fc_on) {
    bool all_fit = true;
    for (i = 0; i < 5 && s->forecast == FORECAST_SERIES; i++) {
      int dd = (lt->tm_wday + i) % 7, hi_t = use_f ? (w->daily[i] * 9 + (w->daily[i] >= 0 ? 2 : -2)) / 5 + 32 : w->daily[i];
      char tb[8], d2[3] = {WK_EN[dd][0], WK_EN[dd][1], 0};
      snprintf(tb, sizeof(tb), "%d°", hi_t);
      bool latin = s->date_style == DATE_LATIN;
      GSize ds = text_size(latin ? d2 : WK_KANJI[dd], latin ? f->small : f->week_small), ts = text_size(tb, f->small);
      if (ds.w + 2 + ts.w > L->fc_w - 2) all_fit = false;
    }
    for (i = 0; i < 5; i++) {
      int fx0 = L->fc_x + i * (L->fc_w + L->fc_gap), dd = (lt->tm_wday + i) % 7;
      int hi_t = use_f ? (w->daily[i] * 9 + (w->daily[i] >= 0 ? 2 : -2)) / 5 + 32 : w->daily[i];
      char tb[8];
      snprintf(tb, sizeof(tb), "%d°", hi_t);
      bool latin = s->date_style == DATE_LATIN;
      if (s->forecast == FORECAST_SERIES) {
        GColor lc = dark ? GColorWhite : GColorBlack;
        char d2[3] = {WK_EN[dd][0], WK_EN[dd][1], 0};
        const char *dl = latin ? d2 : WK_KANJI[dd];
        GFont df = latin ? f->small : f->week_small;
        GSize ds = text_size(dl, df), ts = text_size(tb, f->small);
        int cy = L->fc_y + L->fc_ph + L->fc_lab / 2;
        if (all_fit) {
          int lx = fx0 + (L->fc_w - (ds.w + 2 + ts.w)) / 2;
          text_center(ctx, dl, df, lx + ds.w / 2, cy, lc);
          text_center(ctx, tb, f->small, lx + ds.w + 2 + ts.w / 2, cy, lc);
        } else {
          text_center(ctx, tb, f->small, fx0 + L->fc_w / 2, cy, lc);
        }
      } else {
        int h2 = L->fc_ph + L->fc_lab, cx = fx0 + L->fc_w / 2;
        text_center(ctx, latin ? WK_EN[dd] : WK_KANJI[dd], latin ? f->small : f->week_small, cx, L->fc_y + h2 * 32 / 100, GColorWhite);
        text_center(ctx, tb, f->seal_small, cx, L->fc_y + h2 * 72 / 100, GColorWhite);
      }
    }
  }

  // --- main seal: weather kanji over the temperature
  char sb[12];
  int tc10 = w->temp_c10, tdisp = use_f ? (tc10 * 9 / 5 + 320 + (tc10 >= 0 ? 5 : -5)) / 10 : (tc10 + (tc10 >= 0 ? 5 : -5)) / 10;
  if (w->valid) snprintf(sb, sizeof(sb), "%d°", tdisp);
  else snprintf(sb, sizeof(sb), "—");
  if (s_hint) {
    // no seal under the hint
  } else if (s->seal_kanji) {
    const char *kj = KANJI[cond];
    if (w->valid && wind >= 13) kj = (cond == COND_HRAIN || cond == COND_LRAIN) ? "嵐" : "風";
    text_center(ctx, kj, f->seal_kanji, sx0 + sw / 2, sy0 + sw / 2 + 1, GColorWhite);
    text_center(ctx, sb, f->seal_num, sx0 + sw / 2, sy0 + sw + (sh - sw) / 2 - 2, GColorWhite);
  } else {
    text_center(ctx, sb, f->seal_num, sx0 + sw / 2, sy0 + sh / 2, GColorWhite);
  }

  // --- old data: the colour drains out of the picture into grey ink
  float age_min = w->valid ? (float)(now - w->updated) / 60.0f : 0;
  float fade = (w->valid && (s->layers & L_STALE)) ? clampf((age_min - 60) / 120.0f, 0, 1) : 0;
  if (fade > 0 && capture(ctx, &fb)) {
    static const uint8_t G[4] = {K_BLACK, K_SUMI, K_GRAY, K_WHITE};
    int q = qd(fade);
    for (y = 0; y < H; y++)
      for (x = s_rows[y].min_x; x <= s_rows[y].max_x; x++) {
        if (!dq2(x, y, q)) continue;
        uint8_t c = s_rows[y].data[x];
        s_rows[y].data[x] = G[clampi(iround(lum(c) * 3), 0, 3)];
      }
    graphics_release_frame_buffer(ctx, fb);
  }

  // --- first-run hint while no weather has ever arrived (main.c decides when)
  if (s_hint) {
    static const char HINT[] = "No weather yet.\nTurn on plugins preview\nin the Pebble app\n(Settings > Plugins).\nShake to hide.";
    GFont hf = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int bw = L->round ? 176 : 184, bx = (W - bw) / 2, by = L->round ? 128 : 70;
    GRect tr = GRect(bx + 8, by + 3, bw - 16, 120);
    GSize ts = graphics_text_layout_get_content_size(HINT, hf, tr, GTextOverflowModeWordWrap, GTextAlignmentCenter);
    GRect box = GRect(bx, by, bw, ts.h + 12);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, box, 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, box);
    graphics_context_set_stroke_color(ctx, GColorCobaltBlue);
    graphics_draw_rect(ctx, grect_inset(box, GEdgeInsets(2)));
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, HINT, hf, tr, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  (void)bounds;
}

void render_set_hint(bool on) { s_hint = on; }
