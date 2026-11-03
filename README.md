# ICAM — Offline ESP32-S3 Dashboard

This build uses **ESP32 SoftAP mode**. The ESP32 creates its own Wi-Fi network, so college Wi-Fi, a router, mobile data, and internet access are not needed.

## Connect
- Wi-Fi SSID: `ICAM-Classroom`
- Password: `ICAM@2026`
- Dashboard: `http://192.168.4.1`
- JSON API: `http://192.168.4.1/api/status`
- Saved offline database (CSV download): `http://192.168.4.1/api/log.csv`

If the phone/laptop says “No internet”, stay connected to ICAM-Classroom. The dashboard and its CSS/JavaScript are served locally from LittleFS.

## Folder structure
```
ICAM2/
├── ICAM2.ino
├── icam_web.h
└── data/
    ├── index.html
    ├── styles.css
    ├── app.js
    ├── guide.html
    └── README.md
```

## Upload
1. Back up the current working sketch and LittleFS files.
2. Open `ICAM2.ino` in Arduino IDE and select the same working ESP32-S3 board configuration.
3. Upload the sketch.
4. Upload the contents of `data/` to LittleFS using a compatible uploader and the same partition layout. This overwrites the existing filesystem.
5. Open Serial Monitor at 115200 baud, reset the board, connect to `ICAM-Classroom`, and browse to `http://192.168.4.1`.

## Sensor fields
The read-only `/api/status` endpoint returns DHT22 temperature/humidity, dual-IR student count, LDR digital ambient detection, MQ-2 raw ADC/threshold state, ACS712 estimated current (A/mA), and fan/LED/buzzer output states.

## Offline database
The firmware appends a row to `/icam_readings.csv` in LittleFS every 10 seconds. It stores uptime (milliseconds since boot), temperature, humidity, DHT validity, student count/occupancy, LDR state, MQ-2 raw/estimated voltage, ACS712 current in A and mA, relay states, buzzer state, and gas-threshold state. The dashboard's **Download saved database** button downloads this file, or open `/api/log.csv` directly. It works with no internet and survives a reset as long as the LittleFS partition is not erased. Since this ESP32 build has no real-time clock, the first column is uptime, not calendar date/time. The log is capped at approximately 180 KB; when it reaches the cap, it starts a new log, so older rows are discarded. This is a lightweight CSV log, not a SQL database; cloud databases such as Supabase require internet or a separate local server. Logging every 10 seconds is intended for project demonstrations, not high-frequency permanent data acquisition.

The firmware's existing automation functions and sensor sampling are not intentionally changed. The web extension reads the existing variables, serves the dashboard, and appends a CSV log to LittleFS; it does not write to relay, buzzer, or sensor pins. Test on the actual board before demonstration. Local HTTP has no encryption/authentication; do not expose this access point or server publicly. MQ-2 is not a certified safety alarm.
