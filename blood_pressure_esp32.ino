#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Cau hinh man hinh OLED 0.96 inch I2C (128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Khai bao chan ket noi cam bien HX710B voi ESP32
#define HX710B_DOUT 18
#define HX710B_SCK  19

// Khai bao chan nut nhan va coi bao tren ESP32
#define RESET_BUTTON 4   // Nut nhan Reset (tich hop dien tro keo len noi)
#define BUZZER_PIN   23  // Coi bao am thanh

// Cac bien toan cuc luu gia tri do
long pressure_raw = 0;
float pressure_mmHg = 0.0;
float systolic = 0.0;      // Huyet ap tam thu (SYS)
float diastolic = 0.0;     // Huyet ap tam truong (DIA)
float map_pressure = 0.0;  // Huyet ap trung binh (MAP)

// Cac co trang thai he thong
bool measuring = false;
bool peak_detected = false;
bool measurement_done = false;

// Bo loc trung binh truot (Moving Average Filter)
const int FILTER_SIZE = 10;
float pressure_buffer[FILTER_SIZE];
int buffer_index = 0;

// Bien theo doi ap suat va thoi gian
float max_pressure = 0.0;
float baseline_pressure = 0.0;
bool baseline_set = false;
float pressure_threshold = 20.0; // Nguong phat hien bat dau bom (mmHg)
unsigned long measure_start_time = 0;

// Ham cap nhat giao dien hien thi len man hinh OLED
void updateOLED(String line1, String line2, String line3 = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 4);
  display.println(line1);

  display.setTextSize(2);
  display.setCursor(0, 22);
  display.println(line2);

  if (line3 != "") {
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.println(line3);
  }
  display.display();
}

// Ham doc du lieu 24-bit tu module HX710B
long readHX710B() {
  long result = 0;
  unsigned long timeout = millis();

  // Cho chan DOUT xuong muc LOW (co gioi han timeout tranh treo ESP32)
  while (digitalRead(HX710B_DOUT) == HIGH) {
    if (millis() - timeout > 500) return 0;
  }

  // Doc 24 bit du lieu tu cam bien
  for (int i = 0; i < 24; i++) {
    digitalWrite(HX710B_SCK, HIGH);
    delayMicroseconds(1);
    result = result << 1;
    if (digitalRead(HX710B_DOUT) == HIGH) {
      result++;
    }
    digitalWrite(HX710B_SCK, LOW);
    delayMicroseconds(1);
  }

  // Xung clock thu 25 de chon kenh doc tiep theo
  digitalWrite(HX710B_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(HX710B_SCK, LOW);
  delayMicroseconds(1);

  // Chuyen doi sang so nguyen co dau 32-bit tu du lieu 24-bit
  if (result & 0x800000) {
    result |= 0xFF000000;
  }
  return result;
}

// Ham chuyen doi gia tri tho (raw) sang don vi mmHg (MPS20N0040D: 0 - 40 kPa ~ 0 - 300 mmHg)
float convertToMmHg(long raw_value) {
  float voltage = ((float)raw_value / 8388608.0) * 3.3; // Dien ap tham chieu 3.3V cua ESP32
  float kPa = (voltage / 3.3) * 40.0;
  float mmHg = kPa * 7.50062;
  return (mmHg < 0) ? 0.0 : mmHg;
}

// Ham loc nhieu trung binh truot
float filterPressure(float new_pressure) {
  pressure_buffer[buffer_index] = new_pressure;
  buffer_index = (buffer_index + 1) % FILTER_SIZE;

  float sum = 0.0;
  for (int i = 0; i < FILTER_SIZE; i++) {
    sum += pressure_buffer[i];
  }
  return sum / FILTER_SIZE;
}

// Ham phat coi bao hieu
void beepBuzzer(int times, int duration_ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration_ms);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(100);
  }
}

// Ham dat lai he thong ve trang thai san sang do moi
void resetMeasurement() {
  measuring = false;
  measurement_done = false;
  peak_detected = false;
  systolic = 0.0;
  diastolic = 0.0;
  map_pressure = 0.0;
  max_pressure = 0.0;

  updateOLED("READY TO MEASURE", "0 mmHg", "Pump cuff to 150mmHg");
  Serial.println("System reset - Ready for new measurement.");
}

// Ham tu dong phat hien qua trinh bom va bat dau do
void autoDetectMeasurement(float pressure) {
  if (measurement_done) return;

  // Thiet lap ap suat nen (baseline) ban dau
  if (!baseline_set) {
    static int baseline_count = 0;
    static float baseline_sum = 0.0;

    baseline_sum += pressure;
    baseline_count++;

    if (baseline_count >= 50) {
      baseline_pressure = baseline_sum / baseline_count;
      baseline_set = true;
      updateOLED("CALIBRATED", "Base: " + String((int)baseline_pressure), "Pump cuff to start");
      delay(1000);
    }
    return;
  }

  // Tu dong kich hoat che do do khi ap suat tang vuot nguong
  if (!measuring && (pressure - baseline_pressure) > pressure_threshold) {
    measuring = true;
    peak_detected = false;
    max_pressure = 0.0;
    systolic = 0.0;
    diastolic = 0.0;
    measure_start_time = millis();

    Serial.println("Auto-started blood pressure measurement.");
  }
}

// Ham phan tich dao dong va xac dinh chi so SYS, DIA, MAP
void detectBloodPressure(float pressure) {
  if (!measuring || measurement_done) return;

  // Cap nhat ap suat dinh trong qua trinh bom
  if (pressure > max_pressure) {
    max_pressure = pressure;
  }

  // Phat hien huyet ap tam thu (SYS) khi xa khi
  if (pressure < max_pressure * 0.75 && max_pressure >= 140.0 && !peak_detected) {
    peak_detected = true;
    systolic = pressure + 12.0;
    Serial.print("Detected SYS: ");
    Serial.println(systolic);
  }

  // Phat hien huyet ap tam truong (DIA) va ket thuc do
  if (pressure < max_pressure * 0.45 && peak_detected) {
    diastolic = pressure + 5.0;
    map_pressure = diastolic + (systolic - diastolic) / 3.0;
    measuring = false;
    measurement_done = true;

    beepBuzzer(2, 150);
    Serial.println("Measurement completed.");
  }

  // Tu dong ngat neu qua thoi gian do (60 giay)
  if (millis() - measure_start_time > 60000) {
    measuring = false;
    measurement_done = true;
    updateOLED("TIMEOUT ERROR!", "Try Again", "Press RESET button");
    beepBuzzer(1, 500);
    Serial.println("Measurement timeout.");
  }
}

// Ham kiem tra nut nhan Reset
void handleButtons() {
  if (digitalRead(RESET_BUTTON) == LOW) {
    delay(50); // Chong doi phim (Debounce)
    if (digitalRead(RESET_BUTTON) == LOW) {
      resetMeasurement();
      while (digitalRead(RESET_BUTTON) == LOW); // Cho nha nut
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Khoi tao giao tiep I2C cho ESP32 (SDA = GPIO 21, SCL = GPIO 22)
  Wire.begin(21, 22);

  // Cau hinh cac chan GPIO
  pinMode(HX710B_DOUT, INPUT);
  pinMode(HX710B_SCK, OUTPUT);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Khoi tao man hinh OLED 0.96 inch
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 OLED allocation failed!"));
    for (;;);
  }

  updateOLED("NIBP MONITOR", "ESP32 System", "Initializing...");
  delay(1500);

  // Khoi tao bo dem loc nhieu
  for (int i = 0; i < FILTER_SIZE; i++) {
    pressure_buffer[i] = 0.0;
  }

  updateOLED("CALIBRATING", "Please wait...", "Do not pump cuff");
}

void loop() {
  handleButtons();

  // Doc va loc tin hieu ap suat
  pressure_raw = readHX710B();
  float raw_mmHg = convertToMmHg(pressure_raw);
  pressure_mmHg = filterPressure(raw_mmHg);

  // Thuc thi thuat toan do neu chua hoan thanh
  if (!measurement_done) {
    autoDetectMeasurement(pressure_mmHg);
    detectBloodPressure(pressure_mmHg);
  }

  // Cap nhat hien thi len man hinh OLED
  if (baseline_set) {
    if (measurement_done && systolic > 0 && diastolic > 0) {
      String bpResult = String((int)systolic) + "/" + String((int)diastolic);
      String statusMsg = "MAP:" + String((int)map_pressure) + " | Press RESET";
      updateOLED("RESULT (mmHg):", bpResult, statusMsg);
    } else if (measuring) {
      String guide = (pressure_mmHg >= 150.0) ? "Release valve slowly" : "Pump up to 150 mmHg";
      updateOLED("MEASURING...", String((int)pressure_mmHg) + " mmHg", guide);
    } else if (!measurement_done) {
      updateOLED("READY (Auto-Detect)", String((int)pressure_mmHg) + " mmHg", "Wrap cuff & pump");
    }
  }

  // Xuat du lieu Serial Plotter de kiem tra dang song
  Serial.print("Raw:");
  Serial.print(pressure_raw);
  Serial.print(",Pressure_mmHg:");
  Serial.println(pressure_mmHg);

  delay(50);
}
