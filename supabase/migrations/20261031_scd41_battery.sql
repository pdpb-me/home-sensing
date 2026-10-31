-- Entrance node moved from BMP280 to SCD41 + battery: adds CO2 and battery, stops using pressure.
-- pressure_hpa stays in the table so the history is kept; nothing reads or writes it any more.

alter table public.readings
  add column co2_ppm     real,
  add column battery_v   real,
  add column battery_pct real,       -- null while charging: the voltage says nothing about charge then
  add column charging    boolean;

drop function public.readings_bucketed(text, timestamptz, timestamptz, interval);

create function public.readings_bucketed(
  p_device_id text,
  p_from      timestamptz,
  p_to        timestamptz,
  p_bucket    interval
)
returns table (ts timestamptz, temp_c real, humidity real, co2_ppm real, battery_pct real)
language sql stable security invoker set search_path = ''
as $$
  select date_bin(p_bucket, r.ts, timestamptz '2000-01-01') as ts,
         avg(r.temp_c)::real,
         avg(r.humidity)::real,
         avg(r.co2_ppm)::real,
         avg(r.battery_pct)::real
  from public.readings r
  where r.device_id = p_device_id and r.ts >= p_from and r.ts < p_to
  group by 1
  order by 1;
$$;

-- p_readings: JSON array like [{"ts": 1760371200, "t": 21.4, "h": 48.2, "c": 612, "bv": 3.92, "bp": 66, "chg": false}, ...]
create or replace function public.ingest(p_device_id text, p_key text, p_readings jsonb)
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
