#include <Arduino_HS300x.h>
#include <Arduino_BMI270_BMM150.h>
#include <Arduino_APDS9960.h>

// --- THRESHOLDS ---
const float HUMID_JUMP_THRESHOLD    = 2.0;   // %RH change above baseline triggers humid_jump
const float TEMP_RISE_THRESHOLD     = 0.5;   // degC change above baseline triggers temp_rise
const float MAG_SHIFT_THRESHOLD     = 20.0;  // uT total shift from baseline triggers mag_shift
const int   LIGHT_CHANGE_THRESHOLD  = 200;   // clear channel change from baseline triggers light_or_color_change

// --- COOLDOWN ---
const unsigned long COOLDOWN_MS = 3000;      // ms before same event can re-trigger
unsigned long lastEventTime  = 0;
String        lastEventLabel = "BASELINE_NORMAL";

// --- BASELINES (set during first few readings) ---
float baseHumid = -1;
float baseTemp  = -1;
float baseMagX  = -999, baseMagY = -999, baseMagZ = -999;
long  baseClear = -1;   // Bug 3 fix: use long to match sumClear and avoid truncation
const int BASELINE_SAMPLES = 5;
int   baselineCount = 0;
float sumHumid = 0, sumTemp = 0, sumMagX = 0, sumMagY = 0, sumMagZ = 0;
long  sumClear = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!HS300x.begin()) {
    Serial.println("Failed to initialize HS300x!");
    while (1);
  }

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  if (!APDS.begin()) {
    Serial.println("Failed to initialize APDS9960!");
    while (1);
  }

  Serial.println("Calibrating baselines... keep environment still.");
}

void loop() {
  // --- READ SENSORS ---
  float rh   = HS300x.readHumidity();
  float temp = HS300x.readTemperature();

  float mx = 0, my = 0, mz = 0;
  bool magReady = IMU.magneticFieldAvailable();
  if (magReady) {
    IMU.readMagneticField(mx, my, mz);
  }

  int r = 0, g = 0, b = 0, clearVal = 0;
  bool colorReady = APDS.colorAvailable();
  if (colorReady) {
    APDS.readColor(r, g, b, clearVal);
  }

  // --- BASELINE CALIBRATION (first N samples) ---
  // Bug 2 fix: only accumulate when both sensors actually have valid data this cycle
  if (baselineCount < BASELINE_SAMPLES) {
    if (magReady && colorReady) {
      sumHumid += rh;
      sumTemp  += temp;
      sumMagX  += mx;
      sumMagY  += my;
      sumMagZ  += mz;
      sumClear += clearVal;
      baselineCount++;

      if (baselineCount == BASELINE_SAMPLES) {
        baseHumid = sumHumid / BASELINE_SAMPLES;
        baseTemp  = sumTemp  / BASELINE_SAMPLES;
        baseMagX  = sumMagX  / BASELINE_SAMPLES;
        baseMagY  = sumMagY  / BASELINE_SAMPLES;
        baseMagZ  = sumMagZ  / BASELINE_SAMPLES;
        baseClear = sumClear / BASELINE_SAMPLES;
        Serial.println("Baseline set. Monitoring started.");
      } else {
        Serial.println("Calibrating...");
      }
    }
    delay(1000);
    return;
  }

  // --- COMPUTE EVENT FLAGS ---
  int humid_jump = (rh   - baseHumid > HUMID_JUMP_THRESHOLD) ? 1 : 0;
  int temp_rise  = (temp  - baseTemp  > TEMP_RISE_THRESHOLD)  ? 1 : 0;

  float magDrift = abs(mx - baseMagX) + abs(my - baseMagY) + abs(mz - baseMagZ);
  int mag_shift  = (magDrift > MAG_SHIFT_THRESHOLD) ? 1 : 0;

  int light_or_color_change = (abs(clearVal - baseClear) > LIGHT_CHANGE_THRESHOLD) ? 1 : 0;

  // --- RULE-BASED EVENT CLASSIFICATION WITH COOLDOWN ---
  unsigned long now = millis();
  String eventLabel = "BASELINE_NORMAL";

  // Priority order: more specific events first
  if (humid_jump || temp_rise) {
    eventLabel = "BREATH_OR_WARM_AIR_EVENT";
  } else if (mag_shift) {
    eventLabel = "MAGNETIC_DISTURBANCE_EVENT";
  } else if (light_or_color_change) {
    eventLabel = "LIGHT_OR_COLOR_CHANGE_EVENT";
  }

  // Apply cooldown: suppress re-triggering the same non-baseline event
  if (eventLabel != "BASELINE_NORMAL" && eventLabel == lastEventLabel) {
    if (now - lastEventTime < COOLDOWN_MS) {
      eventLabel = "BASELINE_NORMAL";
    }
  }

  if (eventLabel != "BASELINE_NORMAL") {
    lastEventTime  = now;
    lastEventLabel = eventLabel;
  }

  // --- PRINT TO SERIAL MONITOR ---
  // Line 1: Raw values
  Serial.print("raw,rh=");    Serial.print(rh, 2);
  Serial.print(",temp=");     Serial.print(temp, 2);
  Serial.print(",mag=");      Serial.print(magDrift, 2);
  Serial.print(",r=");        Serial.print(r);
  Serial.print(",g=");        Serial.print(g);
  Serial.print(",b=");        Serial.print(b);
  Serial.print(",clear=");    Serial.println(clearVal);

  // Line 2: Flags
  Serial.print("flags,humid_jump=");        Serial.print(humid_jump);
  Serial.print(",temp_rise=");              Serial.print(temp_rise);
  Serial.print(",mag_shift=");              Serial.print(mag_shift);
  Serial.print(",light_or_color_change=");  Serial.println(light_or_color_change);

  // Line 3: Event label
  Serial.print("event,"); Serial.println(eventLabel);

  Serial.println("----------------------------------------");

  delay(500);
}
