-- Weather on a timer, fetched by the database itself.
-- Open-Meteo rejects requests from Supabase edge functions (HTTP 429: their outgoing addresses are shared
-- with other projects and the free quota is counted per address). The database has its own address, so it
-- asks Open-Meteo every 15 minutes and the `sync` edge function only reads the stored answer.
--
-- pg_net requests are asynchronous: one job sends the request, a second job a minute later stores the answer.

create extension if not exists pg_net with schema extensions;  -- its objects still live in its own `net` schema
create extension if not exists pg_cron;

alter table public.weather_cache
  alter column fetched_at drop not null,
  add column request_id bigint,   -- the request that is still waiting for its answer
  add column last_error text;     -- why the last refresh failed; null after a good one

insert into public.weather_cache (id) values (1) on conflict (id) do nothing;

-- The timer decides how fresh the weather is, so this setting now means: don't show weather older than this.
alter table public.hub_settings alter column weather_max_age_s set default 10800;
update public.hub_settings set weather_max_age_s = 10800 where id = 1;

create function public.weather_request()
returns void
language plpgsql security definer set search_path = ''
as $$
declare
  cfg public.hub_settings;
begin
  select * into cfg from public.hub_settings where id = 1;
  update public.weather_cache
  set request_id = net.http_get(
        url := format(
          'https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s'
          '&current=temperature_2m,weather_code&daily=temperature_2m_max,precipitation_probability_max'
          '&timezone=%s&forecast_days=1',
          cfg.latitude, cfg.longitude, replace(cfg.timezone, '/', '%2F')),
        timeout_milliseconds := 8000)
  where id = 1;
end;
$$;

create function public.weather_store()
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
    update public.weather_cache
    set fetched_at   = now(),
        now_c        = (body #>> '{current,temperature_2m}')::real,
        max_c        = (body #>> '{daily,temperature_2m_max,0}')::real,
        rain_pct     = round((body #>> '{daily,precipitation_probability_max,0}')::numeric)::smallint,
        weather_code = (body #>> '{current,weather_code}')::smallint,
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

revoke execute on function public.weather_request() from public, anon, authenticated;
revoke execute on function public.weather_store()   from public, anon, authenticated;

select cron.schedule('weather-request', '*/15 * * * *',    'select public.weather_request()');
select cron.schedule('weather-store',   '1-59/15 * * * *', 'select public.weather_store()');
-- pg_cron keeps a row per run; without this the log grows by about 200 rows a day for ever
select cron.schedule('cron-log-cleanup', '17 3 * * *',
  $$delete from cron.job_run_details where end_time < now() - interval '7 days'$$);
