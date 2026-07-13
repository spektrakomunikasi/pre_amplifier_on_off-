# Pre Amplifier ON/OFF (ESP8266 + RTC + LCD)

Modul ini menyalakan/mematikan pre-amplifier (via relay) untuk keperluan **takrim sebelum waktu sholat**.

## Fitur
- ESP8266 NodeMCU
- RTC DS3231 (tetap jalan walau internet mati)
- LCD 1602 I2C
- Web setup (AP + optional STA)
- 10 event/hari (5 ON sebelum sholat, 5 OFF sesudah sholat)
- Konfigurasi offset before/after per waktu sholat

## Default Lokasi
- Tangerang 15159
- Lat: `-6.1783`
- Lon: `106.6319`
- TZ: `UTC+7`
- Method: `Singapore`

## Struktur Repo
- `firmware/final_v1_1_esp8266_sholat_relay_tangerang15159.ino`
- `docs/wiring.md`
- `hardware/schematic/`
- `hardware/pcb/`

## Cara Pakai Singkat
1. Upload sketch dari folder firmware ke NodeMCU.
2. Nyalakan alat, konek ke AP `SHOLAT-AMP-SETUP` pass `12345678`.
3. Buka `http://192.168.4.1`.
4. Isi SSID/PASS (opsional), koordinat, timezone, before/after.
5. Simpan, restart.
6. Set RTC dari menu web.

## Catatan
- Pin default: D1=SCL, D2=SDA, D6=relay control.
- Jika logika relay terbalik, ubah define `RELAY_ON_LEVEL` dan `RELAY_OFF_LEVEL` di sketch.
