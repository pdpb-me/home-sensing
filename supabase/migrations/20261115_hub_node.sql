-- Hub + Node: device status, messages for the Hub, weather cache, settings, and the two device endpoints.
-- Nothing that exists is dropped or rewritten: the old `entrance` node and its ingest() keep working.
--
-- Security model, unchanged in spirit:
--   * Devices never touch tables. Each one proves who it is with its own secret key, checked against the
--     SHA-256 hash in `devices.key_hash`. The Node calls ingest_node() directly; the Hub calls the `sync`
--     edge function, which calls hub_sync() with the project's secret key.
--   * People read and send through the dashboard after login, and only if their email is in `viewers`.

-- ---------- Devices: what they are and when they were last heard ----------

alter table public.devices
  add column kind         text not null default 'node' check (kind in ('node', 'hub')),
  add column last_seen_at timestamptz,
  add column battery_v    real,        -- null where the device has no battery sense
  add column rssi_dbm     smallint,    -- Wi-Fi signal at the last contact
  add column fw_version   text,
  add column awake_ms     integer;     -- how long the last wake lasted: the number to watch for battery life

-- The key hashes were readable by any logged-in viewer. The dashboard only ever asks for id and name.
revoke select on public.devices from anon, authenticated;
grant select (id, name, kind, created_at, last_seen_at, battery_v, rssi_dbm, fw_version, awake_ms)
  on public.devices to authenticated;

-- 'unset' can never equal a SHA-256 hash, so these two cannot log in until their real hashes are stored.
insert into public.devices (id, name, key_hash, kind) values
  ('bedroom', 'Camera',  'unset', 'node'),
  ('living',  'Salotto', 'unset', 'hub');

create function public.device_key_ok(p_device_id text, p_key text)
returns boolean
language sql stable security definer set search_path = ''
as $$
  select exists (
    select 1 from public.devices d
    where d.id = p_device_id
      and d.key_hash = encode(sha256(convert_to(p_key, 'UTF8')), 'hex')
  );
$$;

revoke execute on function public.device_key_ok(text, text) from public, anon, authenticated;

-- ---------- Messages shown on the Hub ----------

create table public.messages (
  id           bigint generated always as identity primary key,
  created_at   timestamptz not null default now(),
  author_email text not null default (auth.jwt() ->> 'email'),
  -- 250 x 92 px, 1 bit per pixel, rows padded to 32 bytes, most significant bit first, 1 = black: 2944 bytes,
  -- stored as base64 so the web app, the edge function and the Hub all handle plain text
  bitmap       text not null check (length(bitmap) = 3928 and bitmap ~ '^[A-Za-z0-9+/]+={0,2}$'),
  read_at      timestamptz,   -- when a key was pressed on the Hub while this message was showing
  cleared_at   timestamptz    -- set from the web app to take the message off the Hub
);

alter table public.messages enable row level security;

create policy "viewers can read messages" on public.messages
  for select to authenticated using ((select public.is_viewer()));

create policy "viewers can send messages" on public.messages
  for insert to authenticated
  with check ((select public.is_viewer()) and author_email = (auth.jwt() ->> 'email'));

-- Viewers may write only the two columns a new message needs; read_at and cleared_at change through functions.
revoke insert, update, delete on public.messages from anon, authenticated;
grant insert (bitmap, author_email) on public.messages to authenticated;

create function public.clear_message(p_id bigint)
returns void
language plpgsql security definer set search_path = ''
as $$
begin
  if not public.is_viewer() then
    raise exception 'not allowed' using errcode = '42501';
  end if;
  update public.messages set cleared_at = now() where id = p_id and cleared_at is null;
end;
$$;

revoke execute on function public.clear_message(bigint) from public, anon;
grant  execute on function public.clear_message(bigint) to authenticated;

-- ---------- Hub settings and weather cache (one row each) ----------

create table public.hub_settings (
  id                smallint primary key default 1 check (id = 1),
  bedroom_device_id text not null default 'bedroom' references public.devices (id),
  place_name        text not null default 'MILANO',      -- shown as "OGGI A <place>"
  latitude          real not null default 45.46,          -- city centre, not a home address
  longitude         real not null default 9.19,
  timezone          text not null default 'Europe/Rome',
  day_starts        time not null default '07:00',
  night_starts      time not null default '23:30',
  day_sleep_s       integer not null default 300  check (day_sleep_s between 60 and 86400),
  night_sleep_s     integer not null default 1800 check (night_sleep_s between 60 and 86400),
  weather_max_age_s integer not null default 900  check (weather_max_age_s >= 60)
);

insert into public.hub_settings default values;

create table public.weather_cache (
  id           smallint primary key default 1 check (id = 1),
  fetched_at   timestamptz not null,
  now_c        real,
  max_c        real,
  rain_pct     smallint,
  weather_code smallint      -- WMO code from Open-Meteo, for choosing the icon
);

alter table public.hub_settings  enable row level security;
alter table public.weather_cache enable row level security;

create policy "viewers can read hub settings" on public.hub_settings
  for select to authenticated using ((select public.is_viewer()));

-- weather_cache has no policy on purpose: only the edge function reads and writes it.

-- ---------- Node: batch upload ----------

-- p_samples: [{"age": 1500, "t": 19.8, "h": 55.2, "c": 870}, ...]  age = seconds since the sample was taken
--            (the Node has no clock, so the server works out the time). Optional "bv" for battery volts.
-- p_status:  {"bv": 3.92, "rssi": -61, "fw": "node 0.1", "ms": 2140}  all optional
-- Returns how many new rows were stored.
create function public.ingest_node(p_device_id text, p_key text, p_samples jsonb, p_status jsonb default '{}'::jsonb)
returns integer
language plpgsql security definer set search_path = ''
as $$
declare
  stored   integer;
  received timestamptz := date_trunc('second', now());
begin
  if not public.device_key_ok(p_device_id, p_key) then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;
  if jsonb_typeof(p_samples) <> 'array' or jsonb_array_length(p_samples) > 300 then
    raise exception 'p_samples must be an array of at most 300 samples' using errcode = '22023';
  end if;

  insert into public.readings (device_id, ts, temp_c, humidity, co2_ppm, battery_v)
  select p_device_id, s.ts, s.t, s.h, s.c, s.bv
  from (
    select received - make_interval(secs => least(greatest((e ->> 'age')::integer, 0), 172800)) as ts,
           (e ->> 't')::real as t, (e ->> 'h')::real as h, (e ->> 'c')::real as c, (e ->> 'bv')::real as bv
    from jsonb_array_elements(p_samples) as e
  ) as s
  -- If the reply to an upload is lost the Node sends the same samples again a little later, and their
  -- times come out a few seconds apart. Samples are 5 minutes apart, so anything this close is a repeat.
  where not exists (
    select 1 from public.readings r
    where r.device_id = p_device_id
      and r.ts > s.ts - interval '90 seconds'
      and r.ts < s.ts + interval '90 seconds'
  )
  on conflict (device_id, ts) do nothing;
  get diagnostics stored = row_count;

  update public.devices
  set last_seen_at = now(),
      battery_v    = coalesce((p_status ->> 'bv')::real, battery_v),
      rssi_dbm     = coalesce((p_status ->> 'rssi')::smallint, rssi_dbm),
      fw_version   = coalesce(left(p_status ->> 'fw', 40), fw_version),
      awake_ms     = coalesce((p_status ->> 'ms')::integer, awake_ms)
  where id = p_device_id;

  return stored;
end;
$$;

revoke execute on function public.ingest_node(text, text, jsonb, jsonb) from public, authenticated;
grant  execute on function public.ingest_node(text, text, jsonb, jsonb) to anon;

-- ---------- Hub: everything one wake needs, in one database call ----------

-- Called only by the `sync` edge function. Stores the Hub's own reading, records a "seen" message,
-- and returns the bedroom's latest reading with its age, the current message, the cached weather and the settings.
-- p_reading:      {"t": 21.4, "h": 48.1, "bv": 3.95, "rssi": -58, "fw": "hub 0.3", "ms": 3120}  all optional
-- p_have_message: id of the message the Hub already holds, so its bitmap is sent only when it changes
-- p_seen_message: id of the message that was on screen when a key was pressed, or null
create function public.hub_sync(p_device_id text, p_key text, p_reading jsonb, p_have_message bigint, p_seen_message bigint)
returns jsonb
language plpgsql security definer set search_path = ''
as $$
declare
  cfg     public.hub_settings;
  room    record;
  msg     record;
  weather public.weather_cache;
begin
  if not public.device_key_ok(p_device_id, p_key) then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;

  if (p_reading ->> 't') is not null then
    insert into public.readings (device_id, ts, temp_c, humidity, battery_v)
    values (p_device_id, date_trunc('second', now()),
            (p_reading ->> 't')::real, (p_reading ->> 'h')::real, (p_reading ->> 'bv')::real)
    on conflict (device_id, ts) do nothing;
  end if;

  update public.devices
  set last_seen_at = now(),
      battery_v    = coalesce((p_reading ->> 'bv')::real, battery_v),
      rssi_dbm     = coalesce((p_reading ->> 'rssi')::smallint, rssi_dbm),
      fw_version   = coalesce(left(p_reading ->> 'fw', 40), fw_version),
      awake_ms     = coalesce((p_reading ->> 'ms')::integer, awake_ms)
  where id = p_device_id;

  if p_seen_message is not null then
    update public.messages set read_at = now() where id = p_seen_message and read_at is null;
  end if;

  select * into cfg from public.hub_settings where id = 1;
  select r.temp_c, r.humidity, r.co2_ppm, extract(epoch from now() - r.ts)::integer as age_s
    into room
    from public.readings r where r.device_id = cfg.bedroom_device_id order by r.ts desc limit 1;
  select m.id, m.bitmap into msg
    from public.messages m where m.cleared_at is null order by m.id desc limit 1;
  select * into weather from public.weather_cache where id = 1;

  return jsonb_build_object(
    'room',     case when room.age_s is null then null
                     else jsonb_build_object('t', room.temp_c, 'h', room.humidity, 'c', room.co2_ppm, 'age', room.age_s) end,
    'msg',      msg.id,
    'bitmap',   case when msg.id is distinct from p_have_message then msg.bitmap end,
    'weather',  case when weather.id is null then null else to_jsonb(weather) end,
    'settings', to_jsonb(cfg)
  );
end;
$$;

revoke execute on function public.hub_sync(text, text, jsonb, bigint, bigint) from public, anon, authenticated;
grant  execute on function public.hub_sync(text, text, jsonb, bigint, bigint) to service_role;
