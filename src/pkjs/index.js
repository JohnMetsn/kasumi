// Kasumi PKJS: reads the weather plugin (weather/location) and sends one compact message to the watch.
// Only the properties the enabled layers need are requested.

var COND = ['sun', 'partly_cloudy', 'cloudy', 'light_rain', 'heavy_rain',
  'light_snow', 'heavy_snow', 'rain_and_snow', 'unknown'];
var L = { sky: 1, temp: 2, clouds: 4, precip: 8, wind: 16, mist: 32, uv: 64, ridges: 128, showers: 256, stale: 512 };
var DEFAULTS = { layers: 1023, digits: 0, date: 0, kanji: 1, trigger: 1, lightning: 1, forecast: 2, units: 0, location: '' };
var STORE = 'kasumi.settings';

var subscription = null;
var latest = null;       // last message sent to the watch, resent on HELLO
var pending = null;

function log(m) { console.log('[Kasumi] ' + m); }

function settings() {
  var s = {};
  var k;
  for (k in DEFAULTS) s[k] = DEFAULTS[k];
  try {
    var saved = JSON.parse(localStorage.getItem(STORE) || '{}');
    for (k in saved) if (k in DEFAULTS) s[k] = saved[k];
  } catch (e) { /* defaults */ }
  return s;
}

function saveSettings(s) { localStorage.setItem(STORE, JSON.stringify(s)); }

function settingsMessage(s) {
  return {
    S_LAYERS: s.layers, S_DIGITS: s.digits, S_DATE: s.date,
    S_KANJI: s.kanji, S_TRIGGER: s.trigger, S_LIGHTNING: s.lightning,
    S_FORECAST: s.forecast, S_UNITS: s.units
  };
}

function send(message, label) {
  Pebble.sendAppMessage(message, function () {}, function (e) {
    log('sending ' + label + ' failed: ' + JSON.stringify(e && e.error ? e.error : e));
  });
}

// ---------------------------------------------------------------- readings

function num(p) { return p && p.numericValue ? p.numericValue : null; }

function toC(value, unit) { return unit === '°F' ? (value - 32) * 5 / 9 : value; }

function toMs(value, unit) {
  if (unit === 'km/h') return value / 3.6;
  if (unit === 'mph') return value * 0.44704;
  if (unit === 'knots') return value * 0.514444;
  return value;
}

function byte(v) { return Math.max(0, Math.min(255, Math.round(v) + 128)); }

// A series starts at local midnight; find "now" with start and stepSec, never assume values[0].
function fromNow(series, shape, count, step) {
  if (!series) return null;
  if (!series[shape] && shape === 'shortText') shape = 'longText';
  if (!series[shape]) return null;
  var values = series[shape].values;
  var at = series.start !== undefined ? Math.floor((Date.now() / 1000 - series.start) / (series.stepSec || step)) : 0;
  at = Math.max(0, at);
  var out = [];
  var last = null;
  for (var i = 0; i < count; i++) {
    var v = values[at + i];
    if (v === null || v === undefined) v = last;
    out.push(v);
    last = v;
  }
  return out;
}

// Updates may carry only the places that changed, so keep every place seen, merged by instanceId,
// and pick the chosen one (else the first in the user's order from preferences/weather_location).
var places = {};      // instanceId -> { name, properties }
var arrival = [];     // instanceIds in the order first seen
var order = [];       // instanceIds in the user's order
var prefNames = {};
var prefSub = null;

function nameOf(inst) {
  var n = inst.properties && inst.properties.name;
  return n && n.shortText ? n.shortText.text : '';
}

function onData(envelope) {
  log('update: ' + envelope.instances.map(function (i) { return nameOf(i) || i.instanceId; }).join(', '));
  envelope.instances.forEach(function (inst) {
    var id = inst.instanceId;
    var cur = places[id] || (places[id] = { name: '', properties: {} });
    if (arrival.indexOf(id) < 0) arrival.push(id);
    for (var k in inst.properties) cur.properties[k] = inst.properties[k];
    cur.name = nameOf(inst) || cur.name;
  });
  publish();
}

function chosenId() {
  var s = settings();
  if (s.location && places[s.location]) return s.location;
  for (var i = 0; i < order.length; i++) if (places[order[i]]) return order[i];
  return arrival[0];
}

function listLocations() {
  var ids = order.slice();
  arrival.forEach(function (id) { if (ids.indexOf(id) < 0) ids.push(id); });
  return ids.map(function (id) {
    return { id: id, name: prefNames[id] || (places[id] && places[id].name) || id };
  });
}

function publish() {
  var id = chosenId();
  if (!id) return;
  var p = places[id].properties;
  var placeName = prefNames[id] || places[id].name || id;
  var msg = { UPDATED: Math.floor(Date.now() / 1000) };

  var t = num(p.temperature);
  msg.TEMP_C10 = t ? Math.round(toC(t.value, t.unit) * 10) : 0;
  msg.UNIT_F = t && t.unit === '°F' ? 1 : 0;
  var cc = p.condition_code || {};
  var code = (cc.shortText && cc.shortText.text) || (cc.longText && cc.longText.text) || 'unknown';
  msg.COND = Math.max(0, COND.indexOf(code));
  var ws = num(p.wind_speed);
  msg.WIND10 = ws ? Math.round(toMs(ws.value, ws.unit) * 10) : 0;
  var wd = num(p.wind_direction);
  msg.WIND_DIR = wd ? Math.round(wd.value) % 360 : 0;
  var pr = num(p.precipitation);
  msg.PRECIP = pr ? Math.round(pr.value) : 0;
  var hu = num(p.humidity);
  msg.HUMIDITY = hu ? Math.round(hu.value) : 50;
  var uv = num(p.uv_index);
  msg.UV = uv ? Math.round(uv.value) : 0;
  if (p.sunrise && p.sunrise.timestamp) msg.SUNRISE = p.sunrise.timestamp.value;
  if (p.sunset && p.sunset.timestamp) msg.SUNSET = p.sunset.timestamp.value;

  var ht = p.hourly_temperature && p.hourly_temperature.series;
  var hours = fromNow(ht, 'numericValue', 12, 3600);
  if (hours) {
    var hu2 = ht.numericValue.unit;
    msg.H_TEMP = hours.map(function (v) { return byte(v === null ? msg.TEMP_C10 / 10 : toC(v, hu2)); });
  }
  var dh = p.daily_high && p.daily_high.series;
  var days = fromNow(dh, 'numericValue', 5, 86400);
  if (days) {
    var du = dh.numericValue.unit;
    msg.D_HIGH = days.map(function (v) { return byte(v === null ? msg.TEMP_C10 / 10 : toC(v, du)); });
  }
  var dc = fromNow(p.daily_condition_code && p.daily_condition_code.series, 'shortText', 5, 86400);
  if (dc) msg.D_COND = dc.map(function (c) { var i3 = COND.indexOf(c); return i3 < 0 ? 8 : i3; });
  var hc = fromNow(p.hourly_condition_code && p.hourly_condition_code.series, 'shortText', 12, 3600);
  if (hc) msg.H_COND = hc.map(function (c) { var i2 = COND.indexOf(c); return i2 < 0 ? 8 : i2; });

  latest = msg;
  if (pending) return;
  pending = setTimeout(function () {
    pending = null;
    log('weather for ' + placeName + ': ' + code + ', ' + (msg.TEMP_C10 / 10) + ' C');
    send(latest, 'weather');
  }, 200);
}

function propertiesFor(layers) {
  var props = ['name', 'temperature', 'condition_code'];
  if (layers & L.sky) props.push('sunrise', 'sunset');
  if (layers & L.wind) props.push('wind_speed', 'wind_direction');
  if (layers & L.precip) props.push('precipitation');
  if (layers & L.mist) props.push('humidity');
  if (layers & L.uv) props.push('uv_index');
  if (layers & L.ridges) props.push('hourly_temperature', 'daily_high', 'daily_condition_code');
  if ((layers & L.showers) && (layers & L.precip)) props.push('hourly_condition_code');
  return props;
}

function subscribe() {
  if (subscription) subscription.unsubscribe();
  var s = settings();
  subscription = Pebble.subscribeToSource({
    category: 'weather',
    item: 'location',
    properties: propertiesFor(s.layers),
    onData: onData,
    onError: function (err) { log('weather/location: ' + err.code + ' ' + (err.message || '')); }
  });
  if (!prefSub) {
    prefSub = Pebble.subscribeToSource({
      category: 'preferences',
      item: 'weather_location',
      properties: ['name'],
      onData: function (env) {
        order = env.instances.map(function (i) { return i.instanceId; });
        env.instances.forEach(function (i) { prefNames[i.instanceId] = nameOf(i); });
        log('places: ' + env.instances.map(function (i) { return nameOf(i) || i.instanceId; }).join(', '));
        publish();
      },
      onError: function (err) { log('preferences/weather_location: ' + err.code); }
    });
  }
}

// ---------------------------------------------------------------- settings page

Pebble.addEventListener('configmessage', function (e) {
  var m = e.data || {};
  if (m.type === 'get') {
    e.respond({ settings: settings(), locations: listLocations() });
  } else if (m.type === 'set') {
    var before = settings();
    var s = settings();
    for (var k in m.settings) if (k in DEFAULTS) s[k] = m.settings[k];
    saveSettings(s);
    send(settingsMessage(s), 'settings');
    if (s.location !== before.location || s.layers !== before.layers) subscribe();
    e.respond({ ok: true });
  } else if (m.type === 'preview') {
    send({ DEMO: 1 }, 'preview');
    e.respond({ ok: true });
  } else {
    e.respond({ error: 'unknown message' });
  }
});

// ---------------------------------------------------------------- lifecycle

Pebble.addEventListener('ready', function () {
  log('ready');
  send(settingsMessage(settings()), 'settings');
  subscribe();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.HELLO !== undefined) {
    send(settingsMessage(settings()), 'settings');
    if (latest) setTimeout(function () { send(latest, 'weather'); }, 300);
  }
});
