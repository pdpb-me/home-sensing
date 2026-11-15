// Creates firmware/<folder>/secrets.h from the old node's secrets.h without printing any value:
//   node tools/make_device_secrets.js node bedroom
//   node tools/make_device_secrets.js hub living
// Copies the Wi-Fi and Supabase lines as they are and leaves the device key to be filled in by
// tools/set_device_key.js. secrets.h is git-ignored; this script never shows what it copies.
const fs = require("fs");
const path = require("path");

const [folder, deviceId] = process.argv.slice(2);
if (!folder || !deviceId) {
  console.error("usage: node tools/make_device_secrets.js <firmware folder> <device id>");
  process.exit(1);
}
const root = path.join(__dirname, "..");
const source = path.join(root, "firmware", "home_sensor", "secrets.h");
const target = path.join(root, "firmware", folder, "secrets.h");
if (fs.existsSync(target)) {
  console.log(`firmware/${folder}/secrets.h already exists: left untouched`);
  process.exit(0);
}

const lines = fs.readFileSync(source, "utf8").split(/\r?\n/);
const pick = (name) => {
  const line = lines.find((l) => l.trim().startsWith("#define " + name + " ") || l.trim().startsWith("#define " + name + "\t"));
  if (!line) throw new Error(name + " is not defined in firmware/home_sensor/secrets.h");
  return line.trim();
};

const out = [
  "#pragma once",
  "// Local settings for this device. This file is git-ignored: never commit it.",
  "",
  pick("WIFI_SSID"),
  pick("WIFI_PASSWORD"),
  "",
  pick("SUPABASE_URL"),
  pick("SUPABASE_ANON_KEY"),
  "",
  `#define DEVICE_ID          "${deviceId}"`,
  '#define DEVICE_KEY         "NOT_SET_YET"',
  "",
].join("\n");

fs.mkdirSync(path.dirname(target), { recursive: true });
fs.writeFileSync(target, out);
console.log(`wrote firmware/${folder}/secrets.h with 6 settings (values not shown)`);
