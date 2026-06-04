/**
 * Перевірка базової UX-логіки (node ui-logic.test.mjs)
 */
import assert from 'assert';

function showTabOnly(currentTab, next) {
  return next;
}

function tabChangesMode() {
  return false;
}

function shouldSyncSlidersFromState(currentTab, uiLock) {
  return currentTab === 'manual' && !uiLock;
}

function masterToChannel(activeCh, masterVal, sliders) {
  const s = { ...sliders };
  if (activeCh === 'all') {
    ['r', 'g', 'b', 'w'].forEach((c) => { s[c] = masterVal; });
    s.brightness = masterVal;
  } else {
    s[activeCh] = masterVal;
  }
  return s;
}

function channelToMaster(activeCh, sliders) {
  if (activeCh === 'all') return sliders.brightness;
  return sliders[activeCh];
}

function hourFloatToTimeString(v) {
  if (!Number.isFinite(v)) return '00:00';
  let total = Math.round(v * 60);
  if (total < 0) total = 0;
  if (total > 24 * 60) total = 24 * 60;
  if (total === 24 * 60) return '24:00';
  const h = Math.floor(total / 60);
  const m = total % 60;
  return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0');
}

function timeStringToHourFloat(v, fallback) {
  if (typeof v !== 'string' || !/^\d{2}:\d{2}$/.test(v)) {
    return timeStringToHourFloat(fallback, '08:00');
  }
  const [hh, mm] = v.split(':').map((x) => parseInt(x, 10));
  if (!Number.isFinite(hh) || !Number.isFinite(mm)) {
    return timeStringToHourFloat(fallback, '08:00');
  }
  return Math.min(24, Math.max(0, hh + mm / 60));
}

function modeInfoFromState(d) {
  if (d.scene_mode === 6) return { text: 'Гості / 100%', cls: 'special' };
  if (d.scene_mode === 3) return { text: 'Кормлення 15 хв', cls: 'special' };
  if (d.scene_mode === 4) return { text: 'Нічне світло', cls: 'special' };
  if (d.scene_mode === 2) return { text: 'Шторм', cls: 'special' };
  if (d.scene_mode === 1 || d.operation_mode === 'manual') return { text: 'Ручний', cls: 'manual' };
  return { text: 'Авто', cls: 'auto' };
}

function clamp(v, lo, hi) {
  return Math.min(hi, Math.max(lo, v));
}

const TIMEZONE_PRESETS = [
  { id: 'ua', label: 'Україна', tz: 'EET-2EEST,M3.5.0/3,M10.5.0/4' },
  { id: 'pl', label: 'Польща', tz: 'CET-1CEST,M3.5.0/2,M10.5.0/3' }
];

function timezonePresetId(countryId, posix) {
  const byCountry = TIMEZONE_PRESETS.find((x) => x.id === countryId);
  if (byCountry) return byCountry.id;
  const byTz = TIMEZONE_PRESETS.find((x) => x.tz === posix);
  return byTz ? byTz.id : 'ua';
}

function formatClock(h, m, s) {
  return String(h ?? 0).padStart(2, '0') + ':' + String(m ?? 0).padStart(2, '0') + ':' + String(s ?? 0).padStart(2, '0');
}

assert.strictEqual(showTabOnly('settings', 'settings'), 'settings');
assert.strictEqual(tabChangesMode(), false);
assert.strictEqual(shouldSyncSlidersFromState('settings', false), false);
assert.strictEqual(shouldSyncSlidersFromState('manual', false), true);
assert.deepStrictEqual(masterToChannel('r', 50, { r: 100, g: 10, b: 10, w: 10, brightness: 80 }), {
  r: 50, g: 10, b: 10, w: 10, brightness: 80
});
assert.deepStrictEqual(masterToChannel('all', 60, { r: 1, g: 2, b: 3, w: 4, brightness: 5 }), {
  r: 60, g: 60, b: 60, w: 60, brightness: 60
});
assert.strictEqual(channelToMaster('b', { r: 1, g: 2, b: 77, w: 4, brightness: 99 }), 77);
assert.strictEqual(channelToMaster('all', { r: 1, g: 2, b: 3, w: 4, brightness: 99 }), 99);
assert.strictEqual(hourFloatToTimeString(5), '05:00');
assert.strictEqual(hourFloatToTimeString(21.5), '21:30');
assert.strictEqual(timeStringToHourFloat('08:00', '07:00'), 8);
assert.strictEqual(timeStringToHourFloat('05:30', '07:00'), 5.5);
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 6, operation_mode: 'auto_24h' }), { text: 'Гості / 100%', cls: 'special' });
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 3, operation_mode: 'manual' }), { text: 'Кормлення 15 хв', cls: 'special' });
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 4, operation_mode: 'manual' }), { text: 'Нічне світло', cls: 'special' });
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 2, operation_mode: 'auto_24h' }), { text: 'Шторм', cls: 'special' });
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 0, operation_mode: 'manual' }), { text: 'Ручний', cls: 'manual' });
assert.deepStrictEqual(modeInfoFromState({ scene_mode: 0, operation_mode: 'auto_24h' }), { text: 'Авто', cls: 'auto' });
assert.strictEqual(timezonePresetId('ua', ''), 'ua');
assert.strictEqual(timezonePresetId('missing', 'CET-1CEST,M3.5.0/2,M10.5.0/3'), 'pl');
assert.strictEqual(timezonePresetId('missing', 'missing'), 'ua');
assert.strictEqual(formatClock(7, 5, 9), '07:05:09');
assert.strictEqual(clamp(18, 0, 8), 8);
assert.strictEqual(clamp(5, 10, 100), 10);

// ── Нічне світло: вікно з переходом через північ (дзеркало schedule_engine.cpp) ──
function nightLightActive(hour, hs, he, hm, moon) {
  const hm2 = hm > he ? hm : hm + 24;
  const hr = hour < hs ? hour + 24 : hour;
  const inDay = hour >= hs && hour < he;
  return !inDay && hr >= he && hr < hm2 && moon > 0.001;
}
// день 9-21, місяць до 07:00 наступного ранку
assert.strictEqual(nightLightActive(23, 9, 21, 7, 0.2), true,  'місяць увечері');
assert.strictEqual(nightLightActive(2,  9, 21, 7, 0.2), true,  'місяць після опівночі');
assert.strictEqual(nightLightActive(8,  9, 21, 7, 0.2), false, 'після moon_end — ніч');
assert.strictEqual(nightLightActive(12, 9, 21, 7, 0.2), false, 'удень — не місяць');
assert.strictEqual(nightLightActive(23, 9, 21, 7, 0.0), false, 'moon=0 — без нічного');

// ── Ліміт нічного світла піднято з 8% до 30% ──
const clampMoon = (v) => clamp(v, 0, 30);
assert.strictEqual(clampMoon(20), 20, '20% тримається');
assert.strictEqual(clampMoon(50), 30, '50% затиснуто до 30');

// ── MQTT: поки редагуєш — не заповнювати поля зі стану ──
const shouldFillMqttFields = (editing) => !editing;
assert.strictEqual(shouldFillMqttFields(true), false, 'під час набору IP не перезаписувати');
assert.strictEqual(shouldFillMqttFields(false), true, 'після збереження — заповнити');

// ── Пилюля погоди ──
function weatherPillText(d) {
  if (!d || !d.weather_valid) return '⛅ —';
  return '⛅ ' + Math.round(d.weather_temp_c) + '° ' + (d.weather_desc || '') +
    ' · хмари ' + Math.round(d.weather_cloud) + '%';
}
assert.strictEqual(weatherPillText({ weather_valid: false }), '⛅ —');
assert.strictEqual(
  weatherPillText({ weather_valid: true, weather_temp_c: 17.3, weather_desc: 'Мінлива хмарність', weather_cloud: 31 }),
  '⛅ 17° Мінлива хмарність · хмари 31%');

// ── Множник яскравості від хмарності (дзеркало apply_weather_to_target) ──
const cloudDim = (cloud) => 1 - 0.55 * clamp(cloud, 0, 100) / 100;
assert.ok(Math.abs(cloudDim(0) - 1.0) < 1e-9, 'ясно — без приглушення');
assert.ok(Math.abs(cloudDim(100) - 0.45) < 1e-9, 'суцільна хмарність — 45%');
assert.ok(Math.abs(cloudDim(31) - 0.8295) < 1e-9, '31% хмар → 0.83');

console.log('ui-logic.test.mjs: OK');
