// Creates firmware/node/secrets.h from the old node's secrets.h without printing any value:
//   node tools/make_node_secrets.js
// Copies the Wi-Fi and Supabase lines as they are and leaves the device key to be filled in.
// secrets.h is git-ignored; this script never shows what it copies.
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const source = path.join(root, "firmware", "home_sensor", "secrets.h");
const target = path.join(root, "firmware", "node", "secrets.h");
if (fs.existsSync(target)) {
  console.log("firmware/node/secrets.h already exists: left untouched");
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
  "// Local settings for the Node. This file is git-ignored: never commit it.",
  "",
  pick("WIFI_SSID"),
  pick("WIFI_PASSWORD"),
  "",
  pick("SUPABASE_URL"),
  pick("SUPABASE_ANON_KEY"),
  "",
  '#define DEVICE_ID          "bedroom"',
  '#define DEVICE_KEY         "NOT_SET_YET"',
  "",
].join("\n");

fs.mkdirSync(path.dirname(target), { recursive: true });
fs.writeFileSync(target, out);
console.log("wrote firmware/node/secrets.h with 6 settings (values not shown)");
