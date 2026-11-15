// One image with the main screen states side by side:  node sheet.js [output.png]
const fs = require("fs");
const path = require("path");
const { createCanvas } = require("@napi-rs/canvas");
const { STATES, drawScreen, W, H } = require("./screens");

const SCALE = 3, GAP = 24, COLS = 2, CAPTION = 22;
const names = Object.keys(STATES);
const rows = Math.ceil(names.length / COLS);
const canvas = createCanvas(GAP + COLS * (W * SCALE + GAP), GAP + rows * (H * SCALE + CAPTION + GAP));
const ctx = canvas.getContext("2d");
ctx.fillStyle = "#FFFFFF";
ctx.fillRect(0, 0, canvas.width, canvas.height);
names.forEach((name, n) => {
  const f = drawScreen(STATES[name]);
  const x0 = GAP + (n % COLS) * (W * SCALE + GAP), y0 = GAP + Math.floor(n / COLS) * (H * SCALE + CAPTION + GAP);
  ctx.fillStyle = "#555";
  ctx.font = "14px sans-serif";
  ctx.fillText(name, x0, y0 + 14);
  ctx.fillStyle = "#E6E4DC";
  ctx.fillRect(x0, y0 + CAPTION, W * SCALE, H * SCALE);
  ctx.fillStyle = "#1C1C1A";
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (f.px[y * W + x]) ctx.fillRect(x0 + x * SCALE, y0 + CAPTION + y * SCALE, SCALE, SCALE);
});
const out = process.argv[2] || path.join(__dirname, "out", "sheet.png");
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, canvas.toBuffer("image/png"));
console.log("wrote", out);
