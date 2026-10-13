-- Home sensing: database setup.
-- Paste into Supabase > SQL Editor > New query, then Run. Safe to run once on a fresh project.
--
-- Security model:
--   * Sensor nodes never touch tables directly. They call ingest() with their own secret key,
--     which is checked against a SHA-256 hash stored in `devices`.
--   * The dashboard reads only after login, and only if the user's email is in `viewers`.

-- ---------- Tables ----------

create table public.devices (
  id         text primary key,            -- short id used by the firmware, e.g. 'entrance'
  name       text not null,               -- display name, e.g. 'Entrance'
  key_hash   text not null,               -- hex SHA-256 of the device's secret key
  created_at timestamptz not null default now()
);

create table public.readings (
  id           bigint generated always as identity primary key,
  device_id    text not null references public.devices (id),
  ts           timestamptz not null,       -- when the reading was taken (device clock, NTP-synced)
  temp_c       real,
  humidity     real,
  pressure_hpa real,
  unique (device_id, ts)                   -- re-sent readings are ignored, not duplicated
);

create index readings_device_ts_idx on public.readings (device_id, ts desc);

create table public.viewers (
  email text primary key                   -- who may see the dashboard data
);

alter table public.devices  enable row level security;
alter table public.readings enable row level security;
alter table public.viewers  enable row level security;

-- ---------- Read access for the dashboard ----------

create function public.is_viewer()
returns boolean
language sql stable security definer set search_path = ''
as $$
  select exists (select 1 from public.viewers v where v.email = (auth.jwt() ->> 'email'));
$$;

create policy "viewers can read devices" on public.devices
  for select to authenticated using ((select public.is_viewer()));

create policy "viewers can read readings" on public.readings
  for select to authenticated using ((select public.is_viewer()));

-- Averaged readings per time bucket, so long ranges stay fast (and under the 1000-row API limit).
-- Runs as the caller, so the policies above still apply.
create function public.readings_bucketed(
  p_device_id text,
  p_from      timestamptz,
  p_to        timestamptz,
  p_bucket    interval
)
returns table (ts timestamptz, temp_c real, humidity real, pressure_hpa real)
language sql stable security invoker set search_path = ''
as $$
  select date_bin(p_bucket, r.ts, timestamptz '2000-01-01') as ts,
         avg(r.temp_c)::real,
         avg(r.humidity)::real,
         avg(r.pressure_hpa)::real
  from public.readings r
  where r.device_id = p_device_id and r.ts >= p_from and r.ts < p_to
  group by 1
  order by 1;
$$;

-- ---------- Write access for sensor nodes ----------

-- p_readings: JSON array like [{"ts": 1760371200, "t": 21.4, "h": 48.2, "p": 1013.1}, ...]
-- ts is Unix seconds (UTC). Returns how many new rows were stored.
create function public.ingest(p_device_id text, p_key text, p_readings jsonb)
returns integer
language plpgsql security definer set search_path = ''
as $$
declare
  stored integer;
begin
  if not exists (
    select 1 from public.devices d
    where d.id = p_device_id
      and d.key_hash = encode(sha256(convert_to(p_key, 'UTF8')), 'hex')
  ) then
    raise exception 'invalid device credentials' using errcode = '28000';
  end if;

  insert into public.readings (device_id, ts, temp_c, humidity, pressure_hpa)
  select p_device_id,
         to_timestamp((r ->> 'ts')::double precision),
         (r ->> 't')::real,
         (r ->> 'h')::real,
         (r ->> 'p')::real
  from jsonb_array_elements(p_readings) as r
  on conflict (device_id, ts) do nothing;

  get diagnostics stored = row_count;
  return stored;
end;
$$;

revoke execute on function public.ingest(text, text, jsonb) from public, authenticated;
grant  execute on function public.ingest(text, text, jsonb) to anon;
