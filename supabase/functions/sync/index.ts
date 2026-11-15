// sync: the one HTTPS call the Hub makes each time it wakes.
//
// Deploy with JWT verification off: the Hub is not a logged-in user. It proves who it is with its own
// device key, which hub_sync() checks against the hash in `devices`. This function holds the project's
// secret key (from its environment, never in the repo) and is the only caller of hub_sync().
//
// The weather is not fetched here: Open-Meteo rejects requests from edge functions (shared, rate-limited
// addresses). The database fetches it every 15 minutes (see the weather_timer migration) and this
// function passes on what is stored.
//
// Request (POST, JSON):
//   { "id": "living", "key": "<device key>",
//     "t": 21.4, "h": 48.1, "bv": 3.95, "rssi": -58, "fw": "hub 0.3", "ms": 3120,   // all optional
//     "have": 12,     // id of the message the Hub already stores, or omitted
//     "seen": 12 }    // id of the message that was showing when a key was pressed, or omitted
//
// Response, kept flat and short so the firmware can parse it cheaply:
//   { "t": 19.8, "h": 55, "c": 870, "age": 240,          // bedroom; all null when there is no reading yet
//     "msg": 12, "bmp": "<base64, 3928 chars>",           // bmp only when msg differs from "have"; msg null = none
//     "wn": 9.4, "wx": 9.5, "wr": 60, "wc": 2,            // outside now, today's max, rain %, WMO code; null if unknown or too old
//     "place": "MILANO", "sleep": 300 }                   // seconds to sleep before the next sync
import { createClient } from "npm:@supabase/supabase-js@2";

function secretKey(): string {
  const named = Deno.env.get("SUPABASE_SECRET_KEYS");
  if (named) {
    const key = JSON.parse(named)["default"];
    if (key) return key;
  }
  return Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
}

const db = createClient(Deno.env.get("SUPABASE_URL")!, secretKey(), { auth: { persistSession: false } });

const json = (body: unknown, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } });

const num = (v: unknown) => (typeof v === "number" && Number.isFinite(v) ? v : null);
const int = (v: unknown) => (typeof v === "number" && Number.isInteger(v) ? v : null);

type Settings = {
  place_name: string; timezone: string;
  day_starts: string; night_starts: string; day_sleep_s: number; night_sleep_s: number; weather_max_age_s: number;
};
type Weather = { fetched_at: string | null; now_c: number | null; max_c: number | null; rain_pct: number | null; weather_code: number | null };

// "HH:MM:SS" -> minutes since midnight
const minutes = (hms: string) => Number(hms.slice(0, 2)) * 60 + Number(hms.slice(3, 5));

function sleepSeconds(cfg: Settings, now: Date): number {
  const local = new Intl.DateTimeFormat("en-GB", { timeZone: cfg.timezone, hour: "2-digit", minute: "2-digit", hourCycle: "h23" })
    .formatToParts(now);
  const at = Number(local.find((p) => p.type === "hour")!.value) * 60 + Number(local.find((p) => p.type === "minute")!.value);
  const day = minutes(cfg.day_starts), night = minutes(cfg.night_starts);
  const isDay = day <= night ? at >= day && at < night : at >= day || at < night;
  return isDay ? cfg.day_sleep_s : cfg.night_sleep_s;
}

Deno.serve(async (req) => {
  if (req.method !== "POST") return json({ error: "POST only" }, 405);
  let body: Record<string, unknown>;
  try {
    body = await req.json();
  } catch {
    return json({ error: "body must be JSON" }, 400);
  }
  if (typeof body?.id !== "string" || typeof body?.key !== "string") return json({ error: "id and key are required" }, 401);

  const { data, error } = await db.rpc("hub_sync", {
    p_device_id: body.id,
    p_key: body.key,
    p_reading: {
      t: num(body.t), h: num(body.h), bv: num(body.bv), rssi: int(body.rssi), ms: int(body.ms),
      fw: typeof body.fw === "string" ? body.fw : null,
    },
    p_have_message: int(body.have),
    p_seen_message: int(body.seen),
  });
  if (error) {
    if (error.code === "28000") return json({ error: "invalid device credentials" }, 401);
    console.error("hub_sync failed:", error.code, error.message);
    return json({ error: "database error" }, 500);
  }

  const cfg = data.settings as Settings;
  const stored = data.weather as Weather | null;
  // Yesterday's weather shown as "now" would be worse than none
  const fresh = stored?.fetched_at && Date.now() - Date.parse(stored.fetched_at) <= cfg.weather_max_age_s * 1000;
  const weather = fresh ? stored : null;

  const room = data.room as { t: number | null; h: number | null; c: number | null; age: number } | null;
  return json({
    t: room?.t ?? null, h: room?.h ?? null, c: room?.c ?? null, age: room?.age ?? null,
    msg: data.msg ?? null,
    ...(data.bitmap ? { bmp: data.bitmap } : {}),
    wn: weather?.now_c ?? null, wx: weather?.max_c ?? null, wr: weather?.rain_pct ?? null, wc: weather?.weather_code ?? null,
    place: cfg.place_name,
    sleep: sleepSeconds(cfg, new Date()),
  });
});
