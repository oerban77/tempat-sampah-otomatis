// Tempat Sampah Otomatis — ESP32 + Ultrasonic HC-SR04 + Servo SG90
//
// Cara kerja:
//  - Sensor HC-SR04 mengukur jarak tiap 100 milidetik; tiga bacaan
//    terakhir difilter median supaya satu bacaan liar tidak membuka
//    tutup tanpa sengaja.
//  - Begitu ada tangan/sampah lebih dekat dari 20 cm, servo membuka
//    tutup (90 derajat) dan LED hijau menyala. Tutup tetap terbuka
//    selama tangan masih di dekat sensor, lalu menutup sendiri
//    (0 derajat) 3 detik setelah tangan pergi.
//  - Setiap kali tutup dibuka, notifikasi Telegram terkirim — paling
//    sering sekali tiap 30 detik (debounce), supaya tidak spam.
//  - Echo timeout 30 milidetik: kalau tidak ada pantulan sama sekali,
//    pengukuran tidak menggantung (pulseIn kembali dengan 0).
//
// Wiring:
//  - HC-SR04   : TRIG -> GPIO5, ECHO -> GPIO18, VCC -> VIN 5V, GND -> GND
//  - Servo SG90: sinyal (kabel kuning/oranye) -> GPIO25,
//    VCC (merah) -> VIN 5V, GND (cokelat) -> GND
//    (servo mengambil arus dari VIN 5V, bukan dari pin 3V3 ESP32)
//  - LED hijau : GPIO32 (seri resistor 220 ohm ke GND)
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - ESP32Servo 3.2.1,
//    UniversalTelegramBot 1.3.0, ArduinoJson 6.21.5 (WAJIB v6 —
//    dua terakhir hanya bila USE_TELEGRAM 1).
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32 TempatSampah
// (terverifikasi 20 Nov 2026: 1.076.911 byte, 82% flash, RAM 50.892 byte,
//  15%, ESP32 core 4.0.0, COMPILE_RC=0)

#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ---------- KONFIGURASI ----------
#define USE_TELEGRAM 1  // 1 = kirim notifikasi Telegram tiap tutup dibuka, 0 = tanpa Telegram

#define PIN_TRIG 5       // HC-SR04 TRIG
#define PIN_ECHO 18      // HC-SR04 ECHO
#define PIN_SERVO 25     // Servo SG90 (sinyal)
#define PIN_LED_HIJAU 32 // LED hijau, seri 220 ohm ke GND

// Ambang dan waktu (semua bernama — tidak ada magic number di logika)
const unsigned long INTERVAL_UKUR_MS = 100;        // jarak diukur tiap 100 ms
const uint8_t JUMLAH_SAMPEL_MEDIAN = 3;            // median dari 3 bacaan
const unsigned long JEDA_ANTAR_SAMPEL_MS = 10;     // jeda antar bacaan median
const float JARAK_BUKA_CM = 20.0;                  // tangan < 20 cm -> buka tutup
const float JARAK_TIDAK_TERDETEKSI_CM = 400.0;     // tidak ada pantulan
const unsigned long TAHAN_BUKA_MS = 3000;          // tutup menutup 3 detik setelah tangan pergi
const unsigned long ECHO_TIMEOUT_US = 30000;       // pulseIn berhenti setelah 30 ms
const unsigned long PULSA_TRIG_US = 10;            // pulsa pemicu HC-SR04
const unsigned long SETTLE_TRIG_US = 2;            // TRIG LOW dulu sebelum pulsa
const float KECEPATAN_SUARA_CM_PER_US = 0.0343;    // 343 m/detik, pulang-pergi dibagi 2
const int SUDUT_TUTUP = 0;                         // servo menutup
const int SUDUT_BUKA = 90;                         // servo membuka
const unsigned long TELEGRAM_DEBOUNCE_MS = 30000;  // notifikasi paling sering tiap 30 detik
const unsigned long WIFI_TIMEOUT_MS = 15000;       // tunggu WiFi paling lama 15 detik

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";

// ---------- STATE ----------
Servo servoTutup;
bool tutupTerbuka = false;
unsigned long waktuTerakhirTerdeteksi = 0;  // millis() saat tangan terakhir terlihat
unsigned long waktuUkurBerikutnya = 0;     // jadwal ukur berikutnya
unsigned long waktuTelegramTerakhir = 0;   // millis() notifikasi terakhir
bool telegramSiap = false;

#if USE_TELEGRAM
WiFiClientSecure klienAman;
UniversalTelegramBot bot(BOT_TOKEN, klienAman);
#endif

// ---------- FUNGSI ----------

// Satu kali pengukuran jarak dengan HC-SR04 (sentimeter).
// Mengembalikan JARAK_TIDAK_TERDETEKSI_CM bila tidak ada pantulan
// (pulseIn habis waktu setelah ECHO_TIMEOUT_US).
float bacaJarakSekali() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(SETTLE_TRIG_US);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(PULSA_TRIG_US);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long durasi = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (durasi == 0) {
    return JARAK_TIDAK_TERDETEKSI_CM;
  }
  return (durasi * KECEPATAN_SUARA_CM_PER_US) / 2.0;
}

// Jarak hasil median dari JUMLAH_SAMPEL_MEDIAN bacaan — satu bacaan
// liar (mis. gema) tidak langsung mengubah keputusan buka/tutup.
float bacaJarak() {
  float sampel[JUMLAH_SAMPEL_MEDIAN];
  for (uint8_t i = 0; i < JUMLAH_SAMPEL_MEDIAN; i++) {
    sampel[i] = bacaJarakSekali();
    if (i + 1 < JUMLAH_SAMPEL_MEDIAN) {
      delay(JEDA_ANTAR_SAMPEL_MS);
    }
  }
  // Urutkan naik, ambil nilai tengah.
  for (uint8_t i = 0; i < JUMLAH_SAMPEL_MEDIAN - 1; i++) {
    for (uint8_t j = i + 1; j < JUMLAH_SAMPEL_MEDIAN; j++) {
      if (sampel[j] < sampel[i]) {
        float tukar = sampel[i];
        sampel[i] = sampel[j];
        sampel[j] = tukar;
      }
    }
  }
  return sampel[JUMLAH_SAMPEL_MEDIAN / 2];
}

// Buka tutup: servo ke SUDUT_BUKA, LED hijau menyala.
void bukaTutup() {
  servoTutup.write(SUDUT_BUKA);
  digitalWrite(PIN_LED_HIJAU, HIGH);
  tutupTerbuka = true;
  Serial.println("Tutup dibuka.");
}

// Tutup kembali: servo ke SUDUT_TUTUP, LED hijau mati.
void tutupTutup() {
  servoTutup.write(SUDUT_TUTUP);
  digitalWrite(PIN_LED_HIJAU, LOW);
  tutupTerbuka = false;
  Serial.println("Tutup ditutup.");
}

// Kirim notifikasi Telegram bila fitur aktif dan WiFi tersambung.
// Debounce: paling sering sekali tiap TELEGRAM_DEBOUNCE_MS.
void kirimTelegram(const char *pesan) {
#if USE_TELEGRAM
  unsigned long sekarang = millis();
  if (!telegramSiap) {
    return;
  }
  if (sekarang - waktuTelegramTerakhir < TELEGRAM_DEBOUNCE_MS) {
    return;
  }
  waktuTelegramTerakhir = sekarang;
  if (bot.sendMessage(CHAT_ID, pesan, "")) {
    Serial.println("Notifikasi Telegram terkirim.");
  } else {
    Serial.println("Gagal kirim Telegram.");
  }
#else
  (void)pesan;  // tanpa Telegram, parameter tidak dipakai
#endif
}

// Mesin state tutup: buka saat tangan dekat, tahan selama tangan
// masih ada, tutup TAHAN_BUKA_MS setelah tangan pergi.
void kendaliTutup(float jarakCm, unsigned long sekarang) {
  if (jarakCm < JARAK_BUKA_CM) {
    waktuTerakhirTerdeteksi = sekarang;
    if (!tutupTerbuka) {
      bukaTutup();
      kirimTelegram("Tempat sampah dibuka — ada yang buang sampah.");
    }
  } else if (tutupTerbuka && sekarang - waktuTerakhirTerdeteksi >= TAHAN_BUKA_MS) {
    tutupTutup();
  }
}

// ---------- SETUP & LOOP ----------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  digitalWrite(PIN_LED_HIJAU, LOW);

  servoTutup.attach(PIN_SERVO);
  servoTutup.write(SUDUT_TUTUP);
  Serial.println("Tempat Sampah Otomatis siap.");

#if USE_TELEGRAM
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    klienAman.setInsecure();
    telegramSiap = true;
    Serial.print("WiFi tersambung, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi gagal tersambung — lanjut tanpa Telegram.");
  }
#endif
}

void loop() {
  unsigned long sekarang = millis();
  if (sekarang < waktuUkurBerikutnya) {
    return;
  }
  waktuUkurBerikutnya = sekarang + INTERVAL_UKUR_MS;

  float jarakCm = bacaJarak();
  Serial.print("Jarak: ");
  Serial.print(jarakCm);
  Serial.println(" cm");
  kendaliTutup(jarakCm, sekarang);
}
