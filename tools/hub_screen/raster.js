// Shared helpers: turn TrueType fonts into 1-bit glyphs and draw them into a 1-bit frame buffer,
// exactly as the Hub firmware does, so a PNG from here shows what the e-paper will show.
const fs = require("fs");
const path = require("path");
const { createCanvas, GlobalFonts } = require("@napi-rs/canvas");

const FONT_FILES = {
  Inter500: "inter/files/inter-latin-500-normal.woff2",
  Inter600: "inter/files/inter-latin-600-normal.woff2",
  Inter700: "inter/files/inter-latin-700-normal.woff2",
  Caveat700: "caveat/files/caveat-latin-700-normal.woff2",
  Tiny5: "tiny5/files/tiny5-latin-400-normal.woff2",
  Micro5: "micro-5/files/micro-5-latin-400-normal.woff2",
  Silkscreen400: "silkscreen/files/silkscreen-latin-400-normal.woff2",
  Silkscreen700: "silkscreen/files/silkscreen-latin-700-normal.woff2",
  Pixelify500: "pixelify-sans/files/pixelify-sans-latin-500-normal.woff2",
  Pixelify700: "pixelify-sans/files/pixelify-sans-latin-700-normal.woff2",
  Jersey10: "jersey-10/files/jersey-10-latin-400-normal.woff2",
};

const registered = new Set();
function useFont(alias) {
  if (registered.has(alias)) return;
  const file = path.join(__dirname, "node_modules", "@fontsource", FONT_FILES[alias]);
  if (!fs.existsSync(file)) throw new Error("font file missing: " + file);
  GlobalFonts.registerFromPath(file, alias);
  registered.add(alias);
}

// One glyph: bitmap rows (strings of 0/1), its box relative to the pen position and baseline, and the advance
function rasterGlyph(alias, sizePx, ch, threshold) {
  const pad = Math.ceil(sizePx);
  const side = pad * 4;
  const canvas = createCanvas(side, side);
  const ctx = canvas.getContext("2d");
  ctx.font = `${sizePx}px ${alias}`;
  ctx.textBaseline = "alphabetic";
  ctx.fillStyle = "#000";
  const penX = pad, baseline = pad * 2;
  ctx.fillText(ch, penX, baseline);
  const data = ctx.getImageData(0, 0, side, side).data;
  let x0 = side, y0 = side, x1 = -1, y1 = -1;
  const on = (x, y) => data[(y * side + x) * 4 + 3] >= threshold;
  for (let y = 0; y < side; y++) for (let x = 0; x < side; x++) if (on(x, y)) {
    if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
  }
  const adv = Math.round(ctx.measureText(ch).width);
  if (x1 < 0) return { ch, w: 0, h: 0, xo: 0, yo: 0, adv, rows: [] };
  const rows = [];
  for (let y = y0; y <= y1; y++) {
    let row = "";
    for (let x = x0; x <= x1; x++) row += on(x, y) ? "1" : "0";
    rows.push(row);
  }
  return { ch, w: x1 - x0 + 1, h: y1 - y0 + 1, xo: x0 - penX, yo: y0 - baseline, adv, rows };
}

// spec: { alias, size, chars, threshold = 128, spacing = 0, bold = false }
// bold smears each glyph one pixel to the right, for pixel fonts that have no bold cut
function makeFont(spec) {
  useFont(spec.alias);
  const glyphs = {};
  for (const ch of new Set(spec.chars)) {
    const g = rasterGlyph(spec.alias, spec.size, ch, spec.threshold ?? 128);
    if (spec.bold && g.w) {
      g.rows = g.rows.map((r) => {
        const wide = r + "0";
        let out = "";
        for (let i = 0; i < wide.length; i++) out += wide[i] === "1" || (i > 0 && wide[i - 1] === "1") ? "1" : "0";
        return out;
      });
      g.w += 1;
      g.adv += 1;
    }
    g.adv += spec.spacing ?? 0;
    glyphs[ch] = g;
  }
  return { ...spec, glyphs };
}

class Frame {
  constructor(w, h) { this.w = w; this.h = h; this.px = new Uint8Array(w * h); }
  set(x, y, ink) { if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.px[y * this.w + x] = ink ? 1 : 0; }
  fillRect(x, y, w, h, ink) { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) this.set(x + i, y + j, ink); }
  textWidth(font, text) {
    let w = 0;
    for (const ch of text) { const g = font.glyphs[ch]; if (!g) throw new Error(`no glyph '${ch}' in ${font.alias} ${font.size}`); w += g.adv; }
    return w;
  }
  // Draws with the pen at (x, baseline); returns the pen position after the text
  text(font, text, x, baseline, ink = true) {
    for (const ch of text) {
      const g = font.glyphs[ch];
      if (!g) throw new Error(`no glyph '${ch}' in ${font.alias} ${font.size}`);
      for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) if (g.rows[j][i] === "1") this.set(x + g.xo + i, baseline + g.yo + j, ink);
      x += g.adv;
    }
    return x;
  }
  // Row-major, 8 pixels per byte, most significant bit first, rows padded to a whole byte; 1 = ink
  packed() {
    const stride = Math.ceil(this.w / 8);
    const out = new Uint8Array(stride * this.h);
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) if (this.px[y * this.w + x]) out[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    return out;
  }
  savePng(file, scale = 4, grid = false) {
    const canvas = createCanvas(this.w * scale, this.h * scale);
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#E6E4DC";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#1C1C1A";
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) if (this.px[y * this.w + x]) ctx.fillRect(x * scale, y * scale, scale, scale);
    if (grid) {
      ctx.fillStyle = "rgba(200,60,60,0.25)";
      for (let x = 0; x <= this.w; x += 10) ctx.fillRect(x * scale, 0, 1, canvas.height);
      for (let y = 0; y <= this.h; y += 10) ctx.fillRect(0, y * scale, canvas.width, 1);
    }
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, canvas.toBuffer("image/png"));
  }
}

module.exports = { makeFont, Frame, useFont, createCanvas };
