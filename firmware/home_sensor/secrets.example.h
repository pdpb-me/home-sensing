#pragma once
// Template: copy to secrets.h (same folder) and fill in. secrets.h is git-ignored.

#define WIFI_SSID          "YOUR_WIFI_NAME"
#define WIFI_PASSWORD      "PUT_YOUR_WIFI_PASSWORD_HERE"

#define SUPABASE_URL       "https://YOUR-PROJECT-ID.supabase.co"
#define SUPABASE_ANON_KEY  "PUT_YOUR_ANON_PUBLISHABLE_KEY_HERE"

#define DEVICE_ID          "entrance"
#define DEVICE_KEY         "GENERATE_A_RANDOM_KEY"

// Optional, per device (defaults are in home_sensor.ino)
// #define DEVICE_ALTITUDE_M    120     // metres above sea level, for CO2 compensation
// #define DEVICE_TEMP_OFFSET_C 0.5f    // tune against a reference thermometer
