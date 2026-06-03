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

console.log('ui-logic.test.mjs: OK');
