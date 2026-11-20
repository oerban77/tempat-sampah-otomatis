# Tempat Sampah Otomatis — ESP32 + Ultrasonic HC-SR04 + Servo SG90

Tempat sampah yang membuka tutupnya sendiri begitu tanganmu mendekat.
Sensor HC-SR04 mengukur jarak; kalau ada tangan lebih dekat dari 20 cm,
servo membuka tutup (90°), LED hijau menyala, dan notifikasi Telegram
terkirim. Tutup menutup sendiri 3 detik setelah tangan pergi — tanganmu
tidak pernah menyentuh tutup yang kotor.

Video penjelasan (60 detik):
https://youtube.com/shorts/52neGEDJ3aM

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| HC-SR04 | Sensor jarak ultrasonic (TRIG / ECHO) |
| Servo SG90 | Membuka–menutup tutup (0° / 90°) |
| LED hijau | Menyala saat tutup terbuka (seri 220 Ω) |

## Wiring

| Dari | Ke |
|---|---|
| HC-SR04 TRIG | ESP32 GPIO5 |
| HC-SR04 ECHO | ESP32 GPIO18 |
| HC-SR04 VCC | ESP32 VIN 5V |
| Servo sinyal (kuning) | ESP32 GPIO25 |
| Servo VCC (merah) | ESP32 VIN 5V |
| LED hijau | ESP32 GPIO32 (seri 220 Ω ke GND) |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

1. ESP32 mengukur jarak dengan HC-SR04 tiap 100 ms.
2. Tiga bacaan terakhir difilter median — satu bacaan liar tidak langsung
   membuka tutup.
3. Jarak < 20 cm → servo ke 90° (tutup terbuka), LED hijau menyala,
   notifikasi Telegram terkirim (paling sering sekali tiap 30 detik).
4. Tutup tetap terbuka selama tangan masih di dekat sensor.
5. Tangan pergi → 3 detik kemudian servo kembali ke 0° (tutup tertutup),
   LED mati.
6. Echo timeout 30 ms: kalau tidak ada pantulan sama sekali, pengukuran
   tidak menggantung.

## Konfigurasi (di awal sketch)

- `USE_TELEGRAM` (1) — kirim notifikasi Telegram tiap tutup dibuka.
  Set 0 untuk tanpa Telegram (tanpa WiFi).
- `WIFI_SSID` / `WIFI_PASS` — WiFi rumahmu.
- `BOT_TOKEN` / `CHAT_ID` — token bot dan chat ID Telegram.
- `JARAK_BUKA_CM` (20.0) — jarak tangan yang membuka tutup.
- `TAHAN_BUKA_MS` (3000) — jeda sebelum tutup menutup setelah tangan pergi.
- `SUDUT_BUKA` (90) / `SUDUT_TUTUP` (0) — sudut servo buka/tutup.
- `INTERVAL_UKUR_MS` (100) — jarak diukur tiap 100 ms.
- `ECHO_TIMEOUT_US` (30000) — timeout echo 30 ms.

## Library yang dibutuhkan

| Library | Versi | Keterangan |
|---|---|---|
| ESP32Servo | 3.2.1 | Servo SG90 (WAJIB 3.2.x — 3.0.1 gagal compile di ESP32 core 4.x) |
| UniversalTelegramBot | 1.3.0 | Notifikasi Telegram (hanya bila USE_TELEGRAM 1) |
| ArduinoJson | 6.21.5 | Parsing Telegram (WAJIB v6 — v7 tidak dipakai di sini) |

Semua library di atas juga tersedia sebagai ZIP di folder [`libraries/`](libraries/)
— install lewat Arduino IDE: Sketch → Include Library → Add .ZIP Library.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 TempatSampah
```

Terverifikasi 20 Nov 2026: sketch 1.076.911 byte (82% flash),
RAM 50.892 byte (15%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
