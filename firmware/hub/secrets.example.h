#pragma once
// Template: copy to secrets.h (same folder) and fill in. secrets.h is git-ignored.

#define WIFI_SSID          "YOUR_WIFI_NAME"
#define WIFI_PASSWORD      "PUT_YOUR_WIFI_PASSWORD_HERE"

#define SUPABASE_URL       "https://YOUR-PROJECT-ID.supabase.co"
#define SUPABASE_ANON_KEY  "PUT_YOUR_PUBLISHABLE_KEY_HERE"   // not used by the Hub; kept so both devices share one template

#define DEVICE_ID          "living"
#define DEVICE_KEY         "GENERATE_A_RANDOM_KEY"   // its SHA-256 hash goes in devices.key_hash
