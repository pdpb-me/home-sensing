-- Follow-up to hub_node and weather_timer, from the reviewer's pass. No data is removed.
--   1. A device key now works only on its own kind of endpoint.
--   2. Values from devices are checked before they are stored, and one bad field no longer aborts a whole upload.
--   3. A message bitmap must be exactly 2944 bytes.
--   4. Clearing the newest message no longer brings an older one back onto the Hub.
--   5. A weather answer without a temperature no longer overwrites the stored weather.

-- ---------- 1. One kind of device per endpoint ----------
-- 'legacy' is the old entrance node: it has its own clock and uploads through ingest().

alter table public.devices drop constraint devices_kind_check;
alter table public.devices add constraint devices_kind_check check (kind in ('legacy', 'node', 'hub'));
update public.devices set kind = 'legacy' where id = 'entrance';

drop function public.device_key_ok(text, text);

create function public.device_key_ok(p_device_id text, p_key text, p_kind text)
returns boolean
language sql stable security definer set search_path = ''
as $$
  select exists (
    select 1 from public.devices d
    where d.id = p_device_id
      and d.kind = p_kind
      and d.key_hash = encode(sha256(convert_to(p_key, 'UTF8')), 'hex')
  );
$$;

revoke execute on function public.device_key_ok(text, text, text) from public, anon, authenticated;

-- ---------- 2. Checked values ----------

-- A plain decimal number, or null: 'NaN', 'Infinity', exponents and anything else are not stored
create function public.plain_real(p text)
returns real
language sql immutable set search_path = ''
as $$
  select case when p ~ '^-?[0-9]{1,6}(\.[0-9]{1,6})?$' then p::real end;
$$;

-- A whole number inside a range, or null
create function public.bounded_int(p text, p_min integer, p_max integer)
returns integer
language sql immutable set search_path = ''
as $$
  select case when p ~ '^-?[0-9]{1,9}(\.[0-9]{1,6})?$' and round(p::numeric) between p_min and p_max
              then round(p::numeric)::integer end;
$$;

revoke execute on function public.plain_real(text) from public, anon, authenticated;
revoke execute on function public.bounded_int(text, integer, integer) from public, anon, authenticated;

-- The old node's endpoint: unchanged except that only a 'legacy' device may use it
create or replace function public.ingest(p_device_id text, p_key text, p_readings jsonb)
returns integer
language plpgsql security definer set search_path = ''
as $$
declare
  stored integer;
begin
  if not public.device_key_ok(p_device_id, p_key, 'legacy') then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;

  insert into public.readings (device_id, ts, temp_c, humidity, co2_ppm, battery_v, battery_pct, charging)
  select p_device_id,
         to_timestamp((r ->> 'ts')::double precision),
         (r ->> 't')::real,
         (r ->> 'h')::real,
         (r ->> 'c')::real,
         (r ->> 'bv')::real,
         (r ->> 'bp')::real,
         (r ->> 'chg')::boolean
  from jsonb_array_elements(p_readings) as r
  on conflict (device_id, ts) do nothing;

  get diagnostics stored = row_count;
  return stored;
end;
$$;

create or replace function public.ingest_node(p_device_id text, p_key text, p_samples jsonb, p_status jsonb default '{}'::jsonb)
returns integer
language plpgsql security definer set search_path = ''
as $$
declare
  stored   integer;
  received timestamptz := date_trunc('second', now());
begin
  if not public.device_key_ok(p_device_id, p_key, 'node') then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;
  if jsonb_typeof(p_samples) <> 'array' or jsonb_array_length(p_samples) > 300 then
    raise exception 'p_samples must be an array of at most 300 samples' using errcode = '22023';
  end if;

  insert into public.readings (device_id, ts, temp_c, humidity, co2_ppm, battery_v)
  select p_device_id, received - make_interval(secs => s.age), s.t, s.h, s.c, s.bv
  from (
    select public.bounded_int(e ->> 'age', 0, 172800) as age,   -- at most 48 hours old
           public.plain_real(e ->> 't')  as t,
           public.plain_real(e ->> 'h')  as h,
           public.plain_real(e ->> 'c')  as c,
           public.plain_real(e ->> 'bv') as bv
    from jsonb_array_elements(p_samples) as e
  ) as s
  -- A sample needs an age and at least one measurement
  where s.age is not null and (s.t is not null or s.h is not null or s.c is not null)
    -- If the reply to an upload is lost the Node sends the same samples again a little later, and their
    -- times come out a few seconds apart. Samples are 5 minutes apart, so anything this close is a repeat.
    and not exists (
      select 1 from public.readings r
      where r.device_id = p_device_id
        and r.ts > received - make_interval(secs => s.age) - interval '90 seconds'
        and r.ts < received - make_interval(secs => s.age) + interval '90 seconds'
    )
  on conflict (device_id, ts) do nothing;
  get diagnostics stored = row_count;

  update public.devices
  set last_seen_at = now(),
      battery_v    = coalesce(public.plain_real(p_status ->> 'bv'), battery_v),
      rssi_dbm     = coalesce(public.bounded_int(p_status ->> 'rssi', -150, 0)::smallint, rssi_dbm),
      fw_version   = coalesce(left(p_status ->> 'fw', 40), fw_version),
      awake_ms     = coalesce(public.bounded_int(p_status ->> 'ms', 0, 3600000), awake_ms)
  where id = p_device_id;

  return stored;
end;
$$;

create or replace function public.hub_sync(p_device_id text, p_key text, p_reading jsonb, p_have_message bigint, p_seen_message bigint)
returns jsonb
language plpgsql security definer set search_path = ''
as $$
declare
  cfg     public.hub_settings;
  room    record;
  msg     record;
  weather public.weather_cache;
begin
  if not public.device_key_ok(p_device_id, p_key, 'hub') then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;

  if public.plain_real(p_reading ->> 't') is not null then
    insert into public.readings (device_id, ts, temp_c, humidity, battery_v)
    values (p_device_id, date_trunc('second', now()),
            public.plain_real(p_reading ->> 't'), public.plain_real(p_reading ->> 'h'), public.plain_real(p_reading ->> 'bv'))
    on conflict (device_id, ts) do nothing;
  end if;

  update public.devices
  set last_seen_at = now(),
      battery_v    = coalesce(public.plain_real(p_reading ->> 'bv'), battery_v),
      rssi_dbm     = coalesce(public.bounded_int(p_reading ->> 'rssi', -150, 0)::smallint, rssi_dbm),
      fw_version   = coalesce(left(p_reading ->> 'fw', 40), fw_version),
      awake_ms     = coalesce(public.bounded_int(p_reading ->> 'ms', 0, 3600000), awake_ms)
  where id = p_device_id;

  if p_seen_message is not null then
    update public.messages set read_at = now() where id = p_seen_message and read_at is null;
  end if;

  select * into cfg from public.hub_settings where id = 1;
  -- Readings dated in the future (a device with a wrong clock) never count as the latest
  select r.temp_c, r.humidity, r.co2_ppm, extract(epoch from now() - r.ts)::integer as age_s
    into room
    from public.readings r
    where r.device_id = cfg.bedroom_device_id and r.ts <= now()
    order by r.ts desc limit 1;
  -- Only the newest message counts: clearing it leaves the Hub with none, it does not bring an older one back
  select m.id, m.bitmap into msg
    from public.messages m
    where m.id = (select max(id) from public.messages) and m.cleared_at is null;
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

-- ---------- 3. Exact bitmap size ----------
-- 2944 bytes always encode to 3926 characters plus "==". The old check also let 2945 and 2946 bytes through.

alter table public.messages drop constraint messages_bitmap_check;
alter table public.messages add constraint messages_bitmap_check check (bitmap ~ '^[A-Za-z0-9+/]{3926}==$');

-- ---------- 5. Weather: keep the last good answer ----------

create or replace function public.weather_store()
returns void
language plpgsql security definer set search_path = ''
as $$
declare
  pending bigint;
  answer  record;
  body    jsonb;
begin
  select request_id into pending from public.weather_cache where id = 1;
  if pending is null then
    return;
  end if;
  select r.status_code, r.content, r.error_msg into answer from net._http_response r where r.id = pending;
  if not found then
    return;  -- no answer yet; the next request replaces this one
  end if;

  if answer.status_code = 200 then
    body := answer.content::jsonb;
    if public.plain_real(body #>> '{current,temperature_2m}') is null then
      update public.weather_cache
      set request_id = null, last_error = 'answer had no current temperature'
      where id = 1;
      return;
    end if;
    update public.weather_cache
    set fetched_at   = now(),
        now_c        = public.plain_real(body #>> '{current,temperature_2m}'),
        max_c        = public.plain_real(body #>> '{daily,temperature_2m_max,0}'),
        rain_pct     = public.bounded_int(body #>> '{daily,precipitation_probability_max,0}', 0, 100)::smallint,
        weather_code = public.bounded_int(body #>> '{current,weather_code}', 0, 999)::smallint,
        request_id   = null,
        last_error   = null
    where id = 1;
  else
    update public.weather_cache
    set request_id = null,
        last_error = left(coalesce('HTTP ' || answer.status_code || ': ', '')
                          || coalesce(answer.error_msg, answer.content, 'no answer'), 200)
    where id = 1;
  end if;
exception when others then
  -- An answer that isn't the JSON we expect must not stop the timer
  update public.weather_cache set request_id = null, last_error = left(sqlerrm, 200) where id = 1;
end;
$$;
