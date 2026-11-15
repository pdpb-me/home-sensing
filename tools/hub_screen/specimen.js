// Font comparison sheet: node specimen.js  ->  out/specimen-*.png
// Each line is one candidate at its real pixel size, with the width of a text that has to fit.
const path = require("path");
const { makeFont, Frame } = require("./raster");

const CAPS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ·";
const LOWER = "abcdefghijklmnopqrstuvwxyz0123456789 %°.";
const DIGITS = "0123456789.-° ";
const note = makeFont({ alias: "Inter500", size: 11, chars: CAPS + LOWER + ":()=,/x-+" });

function sheet(name, rows) {
  const f = new Frame(330, rows.length * 26 + 4);
  let y = 2;
  for (const r of rows) {
    const font = makeFont(r.spec);
    const w = f.textWidth(font, r.sample);
    f.text(note, `${r.spec.alias} ${r.spec.size}${r.spec.bold ? " bold" : ""}${r.spec.threshold ? " t" + r.spec.threshold : ""}  (${r.fit}: ${w} px)`, 2, y + 8);
    f.text(font, r.sample, 2, y + 23);
    y += 26;
  }
  f.savePng(path.join(__dirname, "out", `specimen-${name}.png`), 4);
}

const label = (alias, size, extra = {}) => ({ spec: { alias, size, chars: CAPS, ...extra }, sample: "APRI FINESTRA  CAMERA · 2 H FA  SALOTTO CO2", fit: "APRI FINESTRA must be <= 59" });
const labelRows = [
  label("Tiny5", 8), label("Tiny5", 8, { bold: true }), label("Micro5", 12), label("Micro5", 12, { bold: true }),
  label("Silkscreen400", 8), label("Pixelify500", 8), label("Pixelify700", 8), label("Jersey10", 10),
  label("Inter700", 7), label("Inter700", 8), label("Inter700", 9, { threshold: 110 }),
];
for (const r of labelRows) {
  const font = makeFont(r.spec);
  r.fit = `APRI FINESTRA = ${new Frame(1, 1).textWidth(font, "APRI FINESTRA")}, whole line`;
}
sheet("labels", labelRows);

const value = (alias, size, extra = {}) => ({ spec: { alias, size, chars: DIGITS, ...extra }, sample: "21.4° 19.8° 1480 870 -3.5°", fit: "21.4° width" });
const valueRows = [value("Inter700", 14), value("Inter700", 15), value("Inter700", 16), value("Inter600", 16), value("Jersey10", 20), value("Pixelify700", 16)];
for (const r of valueRows) r.fit = `21.4° = ${new Frame(1, 1).textWidth(makeFont(r.spec), "21.4°")}, whole line`;
sheet("values", valueRows);

const small = (alias, size, extra = {}) => ({ spec: { alias, size, chars: LOWER, ...extra }, sample: "48% 55% 61% ok dry wet ppm", fit: "" });
const smallRows = [
  small("Inter500", 10), small("Inter500", 11), small("Inter600", 11), small("Inter700", 10), small("Tiny5", 8), small("Tiny5", 8, { bold: true }),
  small("Pixelify500", 8), small("Pixelify700", 8), small("Jersey10", 10), small("Silkscreen400", 8), small("Micro5", 12),
];
for (const r of smallRows) {
  const font = makeFont(r.spec);
  const fw = new Frame(1, 1);
  r.fit = `48% = ${fw.textWidth(font, "48%")}, dry = ${fw.textWidth(font, "dry")}, whole line`;
}
sheet("small", smallRows);
console.log("wrote out/specimen-labels.png, specimen-values.png, specimen-small.png");
