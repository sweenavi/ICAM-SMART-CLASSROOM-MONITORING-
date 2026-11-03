# ICAM — Offline ESP32-S3 Dashboard

This build uses **ESP32 SoftAP mode**. The ESP32 creates its own Wi-Fi network, so college Wi-Fi, a router, mobile data, and internet access are not needed.

## Connect
- Wi-Fi SSID: `ICAM-Classroom`
- Password: `ICAM@2026`
- Dashboard: `http://192.168.4.1`
- JSON API: `http://192.168.4.1/api/status`

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

The firmware's existing automation functions are not intentionally changed. The web extension only reads existing variables and serves the dashboard. Test on the actual board before demonstration. Local HTTP has no encryption/authentication; do not expose this access point or server publicly. MQ-2 is not a certified safety alarm.


The firmware also stores a local CSV history in LittleFS at `/icam_readings.csv`, appending every 10 seconds. Download it from `/api/log.csv`; no internet or cloud database is required. It includes ACS712 current in A/mA.
