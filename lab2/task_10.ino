#include <PDM.h>
#include <Arduino_APDS9960.h>
#include <Arduino_BMI270_BMM150.h> // Make sure this matches your board's IMU library

// --- THRESHOLDS (You will need to tune these during testing) ---
const int MIC_THRESHOLD = 50;       // Values above this are considered NOISY
const int DARK_THRESHOLD = 15;      // Clear channel values below this are DARK
const float MOTION_THRESHOLD = 8.0; // Gyro sum above this means MOVING
const int PROX_THRESHOLD = 150;     // APDS proximity: Lower is CLOSER (0=close, 255=far)

// --- PDM Microphone Variables ---
short sampleBuffer[256];
volatile int samplesRead = 0;
int micLevel = 0;

// --- Motion hold: keeps motion flag active for a few cycles after movement ---
float peakMotion = 0;
int motionHoldCycles = 0;
const int MOTION_HOLD = 3; // hold motion flag for 3 cycles (~1.5s)

void onPDMdata() {
  int bytesAvailable = PDM.available();
  // Bug 2 fix: cap read to buffer size to prevent overflow
  if (bytesAvailable > (int)sizeof(sampleBuffer)) {
    bytesAvailable = sizeof(sampleBuffer);
  }
  PDM.read(sampleBuffer, bytesAvailable);
  samplesRead = bytesAvailable / 2;
}

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for Serial Monitor to open

  // Initialize IMU
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  // Initialize APDS9960 (Light/Color & Proximity)
  if (!APDS.begin()) {
    Serial.println("Failed to initialize APDS9960!");
    while (1);
  }

  // Initialize PDM Microphone
  PDM.onReceive(onPDMdata);
  if (!PDM.begin(1, 16000)) {
    Serial.println("Failed to start PDM microphone!");
    while (1);
  }
}

void loop() {
  // 1. Read Microphone (Audio Level)
  // Bug 1 fix: copy volatile ISR variable atomically to avoid race condition
  noInterrupts();
  int currentSamples = samplesRead;
  samplesRead = 0;
  interrupts();

  if (currentSamples > 0) {
    long sum = 0;
    for (int i = 0; i < currentSamples; i++) {
      sum += abs(sampleBuffer[i]);
    }
    micLevel = sum / currentSamples;
  } else {
    // Bug 3 fix: no new samples this cycle — reset so old noise doesn't persist
    micLevel = 0;
  }

  // 2. Read APDS9960 (Ambient Light)
  int r = 0, g = 0, b = 0, clearLight = 0;
  if (APDS.colorAvailable()) {
    APDS.readColor(r, g, b, clearLight);
  }

  // 3. Read IMU (Motion via Gyroscope)
  float gx = 0, gy = 0, gz = 0, motion = 0;
  if (IMU.gyroscopeAvailable()) {
    IMU.readGyroscope(gx, gy, gz);
    motion = abs(gx) + abs(gy) + abs(gz);
    if (motion > peakMotion) {
      peakMotion = motion;
      motionHoldCycles = MOTION_HOLD;
    }
  }
  if (motionHoldCycles > 0) {
    motion = peakMotion;
    motionHoldCycles--;
  } else {
    peakMotion = 0;
  }

  // 4. Read APDS9960 (Proximity)
  int prox = 255; // Default to far
  if (APDS.proximityAvailable()) {
    prox = APDS.readProximity();
  }

  // --- COMPUTE BINARY DECISIONS (FLAGS) ---
  int isSound   = (micLevel   > MIC_THRESHOLD)    ? 1 : 0;
  int isDark    = (clearLight < DARK_THRESHOLD)    ? 1 : 0;
  int isMoving  = (motion     > MOTION_THRESHOLD)  ? 1 : 0;
  // Lower proximity value = closer to sensor (0=close, 255=far)
  int isNear    = (prox       < PROX_THRESHOLD)    ? 1 : 0;

  // --- RULE-BASED SENSOR FUSION LOGIC ---
  // All 16 combinations of (sound, dark, moving, near) are covered — no fallback needed.
  String stateLabel;

  if      (isSound == 0 && isDark == 0 && isMoving == 0 && isNear == 0) {
    stateLabel = "QUIET_BRIGHT_STEADY_FAR";
  }
  else if (isSound == 0 && isDark == 0 && isMoving == 0 && isNear == 1) {
    stateLabel = "QUIET_BRIGHT_STEADY_NEAR";
  }
  else if (isSound == 0 && isDark == 0 && isMoving == 1 && isNear == 0) {
    stateLabel = "QUIET_BRIGHT_MOVING_FAR";
  }
  else if (isSound == 0 && isDark == 0 && isMoving == 1 && isNear == 1) {
    stateLabel = "QUIET_BRIGHT_MOVING_NEAR";
  }
  else if (isSound == 0 && isDark == 1 && isMoving == 0 && isNear == 0) {
    stateLabel = "QUIET_DARK_STEADY_FAR";
  }
  else if (isSound == 0 && isDark == 1 && isMoving == 0 && isNear == 1) {
    stateLabel = "QUIET_DARK_STEADY_NEAR";
  }
  else if (isSound == 0 && isDark == 1 && isMoving == 1 && isNear == 0) {
    stateLabel = "QUIET_DARK_MOVING_FAR";
  }
  else if (isSound == 0 && isDark == 1 && isMoving == 1 && isNear == 1) {
    stateLabel = "QUIET_DARK_MOVING_NEAR";
  }
  else if (isSound == 1 && isDark == 0 && isMoving == 0 && isNear == 0) {
    stateLabel = "NOISY_BRIGHT_STEADY_FAR";
  }
  else if (isSound == 1 && isDark == 0 && isMoving == 0 && isNear == 1) {
    stateLabel = "NOISY_BRIGHT_STEADY_NEAR";
  }
  else if (isSound == 1 && isDark == 0 && isMoving == 1 && isNear == 0) {
    stateLabel = "NOISY_BRIGHT_MOVING_FAR";
  }
  else if (isSound == 1 && isDark == 0 && isMoving == 1 && isNear == 1) {
    stateLabel = "NOISY_BRIGHT_MOVING_NEAR";
  }
  else if (isSound == 1 && isDark == 1 && isMoving == 0 && isNear == 0) {
    stateLabel = "NOISY_DARK_STEADY_FAR";
  }
  else if (isSound == 1 && isDark == 1 && isMoving == 0 && isNear == 1) {
    stateLabel = "NOISY_DARK_STEADY_NEAR";
  }
  else if (isSound == 1 && isDark == 1 && isMoving == 1 && isNear == 0) {
    stateLabel = "NOISY_DARK_MOVING_FAR";
  }
  else {
    stateLabel = "NOISY_DARK_MOVING_NEAR";
  }

  // --- PRINT RESULTS TO SERIAL MONITOR ---
  // Line 1: Raw Values
  Serial.print("raw,mic=");    Serial.print(micLevel);
  Serial.print(",clear=");     Serial.print(clearLight);
  Serial.print(",motion=");    Serial.print(motion, 2);
  Serial.print(",prox=");      Serial.println(prox);

  // Line 2: Flags
  Serial.print("flags,sound="); Serial.print(isSound);
  Serial.print(",dark=");       Serial.print(isDark);
  Serial.print(",moving=");     Serial.print(isMoving);
  Serial.print(",near=");       Serial.println(isNear);

  // Line 3: Final State
  Serial.print("state,"); Serial.println(stateLabel);

  Serial.println("----------------------------------------");

  delay(500); // Wait half a second before next classification
}
