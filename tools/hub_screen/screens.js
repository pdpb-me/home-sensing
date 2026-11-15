// The Hub screen (250x122, layout A of HANDOFF.md section 6) drawn on a PC:  node screens.js  ->  out/screen-*.png
// The firmware in firmware/hub draws the same layout with the same fonts; change both together.
const path = require("path");
const { makeFont, Frame, useFont, createCanvas } = require("./raster");

const W = 250, H = 122;
const BAR_H = 30;                       // includes the 1 px rule at its bottom
const COL_W = [95, 90, 65];              // the brief says about 93 / 88 / 69; the worst-case room values need 4 px more
const COL_X = [0, COL_W[0], COL_W[0] + COL_W[1]];  // left edge of each top-bar column
const COL_PAD = [10, 5, 5];             // 10 on the left edge: the printed case will have rounded corners
const LABEL_BASE = 8;
const VALUE_BASE = 24;
const AREA_Y = BAR_H, AREA_H = H - BAR_H;  // the big area: 250 x 92
const BAND_H = 12;                      // low-battery band

const HUMIDITY_DRY = 40, HUMIDITY_WET = 60;
const STALE_MINUTES = 90;

const CAPS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ·";
const FONTS = {
  label: makeFont({ alias: "Tiny5", size: 8, chars: CAPS }),
  labelBold: makeFont({ alias: "Tiny5", size: 8, chars: CAPS, bold: true }),
  word: makeFont({ alias: "Tiny5", size: 8, chars: "abcdefghijklmnopqrstuvwxyz " }),
  wordBold: makeFont({ alias: "Tiny5", size: 8, chars: "abcdefghijklmnopqrstuvwxyz ", bold: true }),
  value: makeFont({ alias: "Inter700", size: 14, chars: "0123456789.-°" }),
  percent: makeFont({ alias: "Inter600", size: 10, chars: "0123456789%-" }),
  row: makeFont({ alias: "Inter500", size: 13, chars: "MaxPiogg " }),
  rowBold: makeFont({ alias: "Inter700", size: 13, chars: "0123456789%°-" }),
  big: makeFont({ alias: "Inter700", size: 50, chars: "0123456789-°" }),
  bigSmall: makeFont({ alias: "Inter700", size: 42, chars: "0123456789-°" }),
};

// Same rounding as the firmware's lroundf(): halves go away from zero
const round = (v) => Math.sign(v) * Math.round(Math.abs(v));

// What is printed: whole percent, capped at 99 so the widest row still fits its column
const shownHumidity = (h) => Math.min(99, Math.max(0, round(h)));

// Decided on the printed value, so the word never contradicts the number next to it
function humidityWord(h) {
  if (h < HUMIDITY_DRY) return { text: "dry", bold: true };
  if (h > HUMIDITY_WET) return { text: "wet", bold: true };
  return { text: "ok", bold: false };
}

// Only called above STALE_MINUTES, so the smallest value is 2 H
function ageLabel(minutes) {
  const hours = Math.round(minutes / 60);
  return hours < 48 ? `${hours} H FA` : `${Math.min(99, Math.round(hours / 24))} GG FA`;
}

// Bold when it fits its column, regular otherwise
function drawLabelAt(f, text, x, baseline, maxWidth, ink = true) {
  const font = f.textWidth(FONTS.labelBold, text) <= maxWidth ? FONTS.labelBold : FONTS.label;
  f.text(font, text, x, baseline, ink);
}
const drawLabel = (f, text, x, maxWidth, ink = true) => drawLabelAt(f, text, x, LABEL_BASE, maxWidth, ink);

function drawRoom(f, col, label, tempC, humidity, showWord) {
  const x = COL_X[col] + COL_PAD[col];
  drawLabel(f, label, x, COL_W[col] - COL_PAD[col] - 3);
  if (tempC == null) {  // nothing received yet
    const pen = f.text(FONTS.value, "--.-°", x, VALUE_BASE) + 2;
    f.text(FONTS.percent, "--%", pen, VALUE_BASE);
    return;
  }
  let pen = f.text(FONTS.value, `${tempC.toFixed(1)}°`, x, VALUE_BASE) + 2;
  pen = f.text(FONTS.percent, `${shownHumidity(humidity)}%`, pen, VALUE_BASE) + 2;
  if (showWord) {
    const word = humidityWord(shownHumidity(humidity));
    f.text(word.bold ? FONTS.wordBold : FONTS.word, word.text, pen, VALUE_BASE);
  }
}

function drawCo2(f, label, ppm, alarm) {
  const x0 = COL_X[2];
  if (alarm) f.fillRect(x0, 0, COL_W[2], BAR_H, true);
  const ink = !alarm;
  const x = x0 + COL_PAD[2];
  drawLabel(f, label, x, COL_W[2] - COL_PAD[2] - 3, ink);
  const pen = f.text(FONTS.value, ppm == null ? "----" : String(round(ppm)), x, VALUE_BASE, ink) + 3;
  f.text(FONTS.word, "ppm", pen, VALUE_BASE, ink);
}

function drawTopBar(f, s) {
  const stale = s.bedroomAgeMin > STALE_MINUTES;
  drawRoom(f, 0, "SALOTTO", s.living.t, s.living.h, true);
  drawRoom(f, 1, stale ? `CAMERA · ${ageLabel(s.bedroomAgeMin)}` : "CAMERA", s.bedroom.t, s.bedroom.h, !stale);
  drawCo2(f, s.co2Alarm ? "APRI FINESTRA" : stale ? `CO2 · ${ageLabel(s.bedroomAgeMin)}` : "CO2 CAMERA", s.bedroom.co2, s.co2Alarm);
  f.fillRect(COL_X[1] - 1, 0, 1, BAR_H, true);
  f.fillRect(COL_X[2] - 1, 0, 1, BAR_H, true);
  f.fillRect(0, BAR_H - 1, W, 1, true);
}

// ---------- Bitmaps made on a canvas, the way the web app will make the message ----------

function canvasToFrame(canvas) {
  const ctx = canvas.getContext("2d");
  const d = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
  const f = new Frame(canvas.width, canvas.height);
  for (let i = 0; i < f.px.length; i++) f.px[i] = d[i * 4 + 3] >= 128 ? 1 : 0;
  return f;
}

function sampleMessage() {
  useFont("Caveat700");
  useFont("Inter600");
  const canvas = createCanvas(W, AREA_H);
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "#000";
  ctx.textBaseline = "alphabetic";
  ctx.font = "30px Caveat700";
  ctx.fillText("Torno alle 19.", 10, 30);
  ctx.fillText("Porto io la cena", 10, 62);
  const heartX = 10 + ctx.measureText("Porto io la cena").width + 6;
  ctx.save();
  ctx.translate(heartX, 43);
  ctx.scale(18 / 24, 18 / 24);
  ctx.fill(new (require("@napi-rs/canvas").Path2D)("M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"));
  ctx.restore();
  ctx.font = "10px Inter600";
  ctx.textAlign = "right";
  ctx.fillText("Pedro · 18:02", W - 10, AREA_H - 6);
  return canvasToFrame(canvas);
}

// Weather icons as stroked paths on a 24 x 24 grid. The rain icon is the one in the approved mockup;
// the others follow the same style (shapes after the Lucide icon set, ISC licence).
// The order is the firmware's WeatherIcon enum.
const CLOUD_OPEN = "M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242";
const ICON_PATHS = {
  clear: ["M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8z", "M12 2v2", "M12 20v2", "M4.93 4.93l1.41 1.41", "M17.66 17.66l1.41 1.41",
          "M2 12h2", "M20 12h2", "M6.34 17.66l-1.41 1.41", "M19.07 4.93l-1.41 1.41"],
  partly: ["M12 2v2", "M4.93 4.93l1.41 1.41", "M20 12h2", "M19.07 4.93l-1.41 1.41", "M15.947 12.65a4 4 0 0 0-5.925-4.128",
           "M13 22H7a5 5 0 1 1 4.9-6H13a3 3 0 0 1 0 6Z"],
  cloud: ["M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9Z"],
  fog: [CLOUD_OPEN, "M16 17H7", "M17 21H9"],
  rain: ["M7 16a4 4 0 0 1 .6-7.95A5.5 5.5 0 0 1 18 9.5a3.25 3.25 0 0 1-.5 6.5", "M8.5 19l-1 2M12.5 19l-1 2M16.5 19l-1 2"],
  snow: [CLOUD_OPEN, "M8 15h.01", "M8 19h.01", "M12 17h.01", "M12 21h.01", "M16 15h.01", "M16 19h.01"],
  thunder: ["M6 16.326A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 .5 8.973", "M13 12l-3 5h4l-3 5"],
};
const ICON_NAMES = Object.keys(ICON_PATHS);

function weatherIcon(name, size) {
  const { Path2D } = require("@napi-rs/canvas");
  const canvas = createCanvas(size, size);
  const ctx = canvas.getContext("2d");
  ctx.scale(size / 24, size / 24);
  ctx.lineWidth = 1.8;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = "#000";
  for (const d of ICON_PATHS[name]) ctx.stroke(new Path2D(d));
  return canvasToFrame(canvas);
}

// WMO weather code (as Open-Meteo reports it) -> icon name. Same table as weatherIconFor() in screen.h.
function iconForCode(code) {
  if (code === 0) return "clear";
  if (code === 1 || code === 2) return "partly";
  if (code === 45 || code === 48) return "fog";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "snow";
  if (code >= 95) return "thunder";
  return "cloud";
}

function blit(f, src, x, y, ink = true) {
  for (let j = 0; j < src.h; j++) for (let i = 0; i < src.w; i++) if (src.px[j * src.w + i]) f.set(x + i, y + j, ink);
}

const MESSAGE = sampleMessage();
const ICONS = Object.fromEntries(ICON_NAMES.map((n) => [n, weatherIcon(n, 36)]));
const ICONS_SMALL = Object.fromEntries(ICON_NAMES.map((n) => [n, weatherIcon(n, 32)]));

function drawWeather(f, w, areaH) {
  const compact = areaH < AREA_H;
  const known = w && w.now != null;
  const icon = (compact ? ICONS_SMALL : ICONS)[known ? iconForCode(w.code ?? 61) : "cloud"];
  const now = known ? `${round(w.now)}°` : "--°";
  const big = !compact && f.textWidth(FONTS.big, now) <= 142 - 4 - 50 ? FONTS.big : FONTS.bigSmall;
  const midY = AREA_Y + Math.floor(areaH / 2);
  blit(f, icon, 8, midY - Math.floor(icon.h / 2));
  const textX = 50;
  const ruleX = 142;
  const labelBase = AREA_Y + (compact ? 12 : 14);
  f.text(FONTS.labelBold, "FUORI ADESSO", textX, labelBase);
  f.text(big, now, textX - 2, labelBase + (big === FONTS.big ? 45 : compact ? 38 : 42));

  f.fillRect(ruleX, AREA_Y + (compact ? 8 : 10), 1, areaH - (compact ? 16 : 20), true);
  const left = ruleX + 10, right = W - 10;
  drawLabelAt(f, `OGGI A ${(w && w.place) || "MILANO"}`, left, labelBase, right - left);
  const rows = [["Max", known && w.max != null ? `${round(w.max)}°` : "--°"], ["Pioggia", known && w.rain != null ? `${round(w.rain)}%` : "--%"]];
  rows.forEach(([name, value], i) => {
    const base = labelBase + (compact ? 20 : 24) + i * (compact ? 18 : 20);
    f.text(FONTS.row, name, left, base);
    f.text(FONTS.rowBold, value, right - f.textWidth(FONTS.rowBold, value), base);
  });
}

function drawBatteryBand(f) {
  const y = H - BAND_H;
  f.fillRect(0, y, W, BAND_H, true);
  // battery outline with one bar left, in white
  f.fillRect(10, y + 3, 12, 1, false); f.fillRect(10, y + 8, 12, 1, false);
  f.fillRect(10, y + 3, 1, 6, false); f.fillRect(21, y + 3, 1, 6, false);
  f.fillRect(22, y + 5, 1, 2, false);
  f.fillRect(12, y + 5, 2, 2, false);
  f.text(FONTS.labelBold, "BATTERIA HUB BASSA", 28, y + 8, false);
}

function drawScreen(s) {
  const f = new Frame(W, H);
  drawTopBar(f, s);
  const areaH = s.batteryLow ? AREA_H - BAND_H : AREA_H;
  if (s.view === "message") blit(f, MESSAGE, 0, AREA_Y);
  else drawWeather(f, s.weather, areaH);
  if (s.batteryLow) drawBatteryBand(f);
  return f;
}

const WEATHER = { now: 9, max: 13, rain: 70, code: 61 };
const STATES = {
  "1-message": { living: { t: 21.4, h: 48 }, bedroom: { t: 19.8, h: 55, co2: 870 }, bedroomAgeMin: 4, co2Alarm: false, view: "message", weather: WEATHER, batteryLow: false },
  "2-weather": { living: { t: 21.4, h: 48 }, bedroom: { t: 19.8, h: 55, co2: 870 }, bedroomAgeMin: 4, co2Alarm: false, view: "weather", weather: WEATHER, batteryLow: false },
  "3-open-window": { living: { t: 21.6, h: 47 }, bedroom: { t: 20.9, h: 61, co2: 1480 }, bedroomAgeMin: 4, co2Alarm: true, view: "message", weather: WEATHER, batteryLow: false },
  "4-stale-dry-battery": { living: { t: 20.8, h: 34 }, bedroom: { t: 19.2, h: 57, co2: 790 }, bedroomAgeMin: 120, co2Alarm: false, view: "weather", weather: WEATHER, batteryLow: true },
  "5-message-battery": { living: { t: 21.4, h: 48 }, bedroom: { t: 19.8, h: 55, co2: 870 }, bedroomAgeMin: 4, co2Alarm: false, view: "message", weather: WEATHER, batteryLow: true },
  "6-cold-wide": { living: { t: 20.0, h: 100 }, bedroom: { t: 28.8, h: 38, co2: 2888 }, bedroomAgeMin: 4, co2Alarm: true, view: "weather", weather: { now: -8, max: 28, rain: 100 }, batteryLow: false },
  "7-very-stale": { living: { t: 20.0, h: 40 }, bedroom: { t: 19.2, h: 57, co2: 790 }, bedroomAgeMin: 60 * 26, co2Alarm: false, view: "weather", weather: { now: -10, max: -2, rain: 5, code: 71 }, batteryLow: false },
  "8-nothing-yet": { living: { t: 21.2, h: 45 }, bedroom: { t: null, h: null, co2: null }, bedroomAgeMin: 0, co2Alarm: false, view: "weather", weather: null, batteryLow: false },
  ...Object.fromEntries([[0, "clear"], [2, "partly"], [3, "cloud"], [45, "fog"], [95, "thunder"]].map(([code, name]) => [`9-icon-${name}`,
    { living: { t: 21.2, h: 45 }, bedroom: { t: 19.8, h: 55, co2: 640 }, bedroomAgeMin: 4, co2Alarm: false, view: "weather", weather: { now: 18, max: 22, rain: 10, code, place: "MILANO" }, batteryLow: code === 45 }])),
};

if (require.main === module) {
  for (const [name, state] of Object.entries(STATES)) drawScreen(state).savePng(path.join(__dirname, "out", `screen-${name}.png`), 4);
  console.log("wrote", Object.keys(STATES).length, "screens to out/");
}

module.exports = { FONTS, MESSAGE, ICONS, ICONS_SMALL, ICON_NAMES, STATES, drawScreen, W, H };
