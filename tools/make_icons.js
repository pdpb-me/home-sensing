// Generates the dashboard app icons (PNG) with no dependencies: node tools/make_icons.js
// Design: white thermometer on coral. Shapes are defined in 0..1 coordinates and anti-aliased by supersampling.
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");

const OUT = path.join(__dirname, "..", "dashboard", "icons");
const CORAL = [216, 90, 48];
const WHITE = [255, 255, 255];
const SUPERSAMPLE = 4;

const inCircle = (x, y, cx, cy, r) => (x - cx) ** 2 + (y - cy) ** 2 <= r * r;

// Vertical capsule: a rectangle with fully rounded ends
function inCapsule(x, y, cx, top, bottom, halfWidth) {
  if (y < top) return inCircle(x, y, cx, top, halfWidth);
  if (y > bottom) return inCircle(x, y, cx, bottom, halfWidth);
  return Math.abs(x - cx) <= halfWidth;
}

function inRoundedSquare(x, y, r) {
  const cx = Math.min(Math.max(x, r), 1 - r);
  const cy = Math.min(Math.max(y, r), 1 - r);
  return x >= 0 && x <= 1 && y >= 0 && y <= 1 && inCircle(x, y, cx, cy, r);
}

// Returns [r, g, b, a] for one sample point. `fullBleed` fills the whole square (maskable / Apple icons).
function sample(x, y, fullBleed) {
  if (!fullBleed && !inRoundedSquare(x, y, 0.22)) return [0, 0, 0, 0];
  const outer = inCapsule(x, y, 0.5, 0.22, 0.6, 0.075) || inCircle(x, y, 0.5, 0.68, 0.13);
  const inner = inCapsule(x, y, 0.5, 0.36, 0.66, 0.03) || inCircle(x, y, 0.5, 0.68, 0.08);
  const color = outer && !inner ? WHITE : CORAL;
  return [...color, 255];
}

function render(size, fullBleed) {
  const pixels = Buffer.alloc(size * size * 4);
  const n = SUPERSAMPLE * SUPERSAMPLE;
  for (let py = 0; py < size; py++) {
    for (let px = 0; px < size; px++) {
      const acc = [0, 0, 0, 0];
      for (let sy = 0; sy < SUPERSAMPLE; sy++) {
        for (let sx = 0; sx < SUPERSAMPLE; sx++) {
          const s = sample((px + (sx + 0.5) / SUPERSAMPLE) / size, (py + (sy + 0.5) / SUPERSAMPLE) / size, fullBleed);
          acc[0] += s[0] * s[3]; acc[1] += s[1] * s[3]; acc[2] += s[2] * s[3]; acc[3] += s[3];
        }
      }
      const i = (py * size + px) * 4;
      const a = acc[3] / n;
      pixels[i] = a ? Math.round(acc[0] / acc[3]) : 0;
      pixels[i + 1] = a ? Math.round(acc[1] / acc[3]) : 0;
      pixels[i + 2] = a ? Math.round(acc[2] / acc[3]) : 0;
      pixels[i + 3] = Math.round(a);
    }
  }
  return pixels;
}

// ---------- Minimal PNG encoder (RGBA, 8 bit) ----------

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

function encodePng(size, rgba) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0);
  header.writeUInt32BE(size, 4);
  header[8] = 8;  // bit depth
  header[9] = 6;  // colour type RGBA
  const raw = Buffer.alloc(size * (size * 4 + 1));
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0;  // filter: none
    rgba.copy(raw, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", header),
    chunk("IDAT", zlib.deflateSync(raw, { level: 9 })),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

// ---------- Output ----------

const ICONS = [
  { file: "icon-192.png", size: 192, fullBleed: false },
  { file: "icon-512.png", size: 512, fullBleed: false },
  { file: "icon-maskable-512.png", size: 512, fullBleed: true },
  { file: "apple-touch-icon.png", size: 180, fullBleed: true },
];

fs.mkdirSync(OUT, { recursive: true });
for (const icon of ICONS) {
  fs.writeFileSync(path.join(OUT, icon.file), encodePng(icon.size, render(icon.size, icon.fullBleed)));
  console.log("wrote", icon.file);
}
