// Gives a device a new random key:  node tools/set_device_key.js node   (or: hub)
// Writes the key into firmware/<folder>/secrets.h (git-ignored) and prints only its SHA-256 hash,
// which is what goes into devices.key_hash on Supabase. The key itself is never shown.
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const folder = process.argv[2];
if (!folder) {
  console.error("usage: node tools/set_device_key.js <firmware folder, e.g. node or hub>");
  process.exit(1);
}
const file = path.join(__dirname, "..", "firmware", folder, "secrets.h");
const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
const at = lines.findIndex((l) => l.trim().startsWith("#define DEVICE_KEY"));
if (at < 0) throw new Error("no DEVICE_KEY line in " + file);

const key = crypto.randomBytes(32).toString("base64url");  // 256 bits
lines[at] = `#define DEVICE_KEY         "${key}"`;
fs.writeFileSync(file, lines.join("\n"));
console.log("key written to firmware/" + folder + "/secrets.h");
console.log("key_hash " + crypto.createHash("sha256").update(key, "utf8").digest("hex"));
