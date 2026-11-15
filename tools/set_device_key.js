// Gives a device a new random key:  node tools/set_device_key.js node   (or: hub)
// Writes the key into firmware/<folder>/secrets.h (git-ignored) and prints only its SHA-256 hash,
// which is what goes into devices.key_hash on Supabase. The key itself is never shown.
//
// A device that already has a key keeps it unless --force is given: replacing the key locks the device
// out until the new hash is stored on Supabase and the firmware is flashed again.
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const args = process.argv.slice(2);
const force = args.includes("--force");
const folder = args.find((a) => !a.startsWith("--"));
if (!folder) {
  console.error("usage: node tools/set_device_key.js <firmware folder, e.g. node or hub> [--force]");
  process.exit(1);
}
const file = path.join(__dirname, "..", "firmware", folder, "secrets.h");
const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
const at = lines.findIndex((l) => /^\s*#define\s+DEVICE_KEY\s/.test(l));
if (at < 0) throw new Error("no DEVICE_KEY line in " + file);

const current = (lines[at].match(/"([^"]*)"/) || [])[1] || "";
const placeholder = current === "" || current === "NOT_SET_YET" || current === "GENERATE_A_RANDOM_KEY";
if (!placeholder && !force) {
  console.error(`firmware/${folder}/secrets.h already has a device key: left untouched (use --force to replace it)`);
  process.exit(1);
}

const key = crypto.randomBytes(32).toString("base64url");  // 256 bits
lines[at] = `#define DEVICE_KEY         "${key}"`;
fs.writeFileSync(file, lines.join("\n"));
console.log("key written to firmware/" + folder + "/secrets.h");
console.log("key_hash " + crypto.createHash("sha256").update(key, "utf8").digest("hex"));
