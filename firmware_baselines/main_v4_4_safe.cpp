// AI Magic Wand: ESP32-S3 standalone RandomForest + OLED + WiFi mouse
// On-device Random Forest; 2-second timestamped capture resampled to 256 x 6.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <SPI.h>
#include <XPowersLib.h>
#include <U8g2lib.h>
#include <math.h>
#include "SensorQMI8658.hpp"
#include "rf_model_5gestures.h"
#include "ml_400hz/features_400hz.h"
#include "wifi_secrets.h"

constexpr int PMU_SDA=42, PMU_SCL=41, I2C_SDA=17, I2C_SCL=18;
constexpr int IMU_SCK=36, IMU_MISO=37, IMU_MOSI=35, IMU_CS=34, SD_CS=47;
constexpr int BOOT_PIN=0;
constexpr uint32_t SAMPLE_US=2500; // target 400 Hz
constexpr int DIM=6, RAW_SAMPLES=800; // 2 seconds, target 400 Hz
constexpr float CONF_MIN=0.70f; // keep conservative until labeled live validation
constexpr uint32_t REARM_MS=800;
// Change these signs/axes if the cursor is inverted for your preferred grip.
constexpr float CURSOR_SPEED=0.12f; // cursor emitted at most every 20 ms
constexpr float CURSOR_DEADZONE_DPS=9.0f;

XPowersAXP2101 pmu(Wire1,PMU_SDA,PMU_SCL);
SPIClass imuBus(HSPI);
SensorQMI8658 imu;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0,U8X8_PIN_NONE);
WiFiUDP udp;
float windowData[RAW_SAMPLES][DIM];
uint32_t windowTimes[RAW_SAMPLES];
float resampledWindow[wand400::RESAMPLED][DIM];
uint32_t gestureStartUs=0;
// Gesture events now transmit to Mac. Mac receiver defaults to DRY RUN.
constexpr bool ENABLE_GESTURE_ACTIONS=false; // V4 SAFE TEST: gestures disabled
int sampleCount=0;
uint32_t nextSample=0, lastAction=0, lastOled=0, lastWifiTry=0, lastCursor=0;
uint32_t stillSince=0; bool idleSent=false, gestureMode=false, lastButton=HIGH;
// Arm only after a stable pause; never restart a capture while still moving.
bool gestureArmed=false;
uint32_t lastGestureEnd=0;
uint32_t lastDebug=0;
String lastLabel="READY", lastActionName="NONE"; float lastConfidence=0;
uint32_t seqNo=0, bootSession=0; bool wifiAnnounced=false;
// Class names and order are exported from the trained five-class model.
static_assert(RF_NUM_CLASSES == 5, "Requires 5-class Random Forest");

void die(const char* msg){ Serial.print("ERROR: ");Serial.println(msg);while(true)delay(1000); }
bool sendCommand(const char* cmd, int dx=0,int dy=0){
  return false; // V4 SAFE TEST: NEVER SEND UDP (including MOVE)
  if (!ENABLE_GESTURE_ACTIONS && strcmp(cmd,"MOVE")!=0) return false;
  if (WiFi.status()!=WL_CONNECTED) return false;
  // Protocol: token|session|seq|command|dx|dy.
  // Random session lets the receiver accept seq=1 again after ESP32 reboot.
  char buf[260];
  snprintf(buf,sizeof(buf),"%s|%08lx|%lu|%s|%d|%d",WAND_SHARED_TOKEN,
           (unsigned long)bootSession,(unsigned long)++seqNo,cmd,dx,dy);
  if (!udp.beginPacket(WAND_MAC_IP,WAND_UDP_PORT)) return false;
  udp.write((const uint8_t*)buf,strlen(buf));
  return udp.endPacket()==1;
}

void drawOled() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  // Header
  oled.drawStr(0, 10, "AI MAGIC WAND");
  oled.drawHLine(0, 12, 128);

  // Connection
  oled.setCursor(0, 24);
  oled.print("WiFi: ");
  oled.print(
      WiFi.status() == WL_CONNECTED
      ? "CONNECTED" : "OFFLINE");

  // Mode
  oled.setCursor(0, 35);
  oled.print("MODE: ");
  oled.print(gestureMode ? "GESTURES" : "CURSOR");

  // Short label to fit the 128px OLED
  const char* shortLabel = "READY";

  if (lastLabel == "double_flick")
    shortLabel = "D FLICK";
  else if (lastLabel == "swipe_up")
    shortLabel = "SWIPE UP";
  else if (lastLabel == "swipe_down")
    shortLabel = "SWIPE DOWN";
  else if (lastLabel == "semicircle_left")
    shortLabel = "SEMI LEFT";
  else if (lastLabel == "idle")
    shortLabel = "IDLE";

  oled.setCursor(0, 46);
  oled.print("ML: ");
  oled.print(shortLabel);

  if (gestureMode &&
      lastLabel != "READY" &&
      lastConfidence >= 0.0f &&
      lastConfidence <= 1.0f) {

    const int confidencePct =
        (int)lroundf(lastConfidence * 100.0f);

    oled.print(" ");
    oled.print(confidencePct);
    oled.print("%");
  }

  // Bottom status and recording progress
  oled.setCursor(0, 57);

  if (!gestureMode) {
    oled.print("Task: ");
    oled.print(lastActionName);
  }
  else if (sampleCount > 0) {
    const uint32_t elapsedUs =
        micros() - gestureStartUs;

    int percent = (int)(elapsedUs / 20000UL);
    if (percent > 100) percent = 100;

    oled.print("REC ");
    oled.print(percent);
    oled.print("%");

    // 48 x 9 pixel progress bar
    oled.drawFrame(79, 49, 48, 9);
    oled.drawBox(
        81, 51,
        (percent * 44) / 100, 5);
  }
  else if (gestureArmed) {
    oled.print("READY: MOVE!");
  }
  else if (millis() - lastGestureEnd < REARM_MS) {
    oled.print("COOLDOWN...");
  }
  else {
    oled.print("HOLD STILL...");
  }

  oled.sendBuffer();
}

// Exactly replicate python/train_compare.py 'features': detrend(type=linear),
// NumPy unnormalized rfft magnitude squared, and per-axis statistics.
// Feature parity tested against Python: 800 timestamped samples -> 256 -> 60 features.
int predictRF(const float features[wand400::FEATURES],float &confidence){
  float scores[RF_NUM_CLASSES]={0};
  for(uint16_t t=0;t<RF_NUM_TREES;t++){
    int nodeIdx=RF_ROOTS[t];
    // Guard against corrupted tree traversal.
    for(int depth=0;depth<128;depth++){
      if (nodeIdx<0 || nodeIdx>=RF_NUM_NODES) break;
      const RFNode &nd=RF_NODES[nodeIdx];
      if(nd.feature<0){for(int c=0;c<RF_NUM_CLASSES;c++)scores[c]+=nd.p[c];break;}
      nodeIdx=(features[nd.feature]<=nd.threshold)?nd.left:nd.right;
      if(nodeIdx<0 || nodeIdx>=RF_NUM_NODES)break;
    }
  }
  Serial.println("=== RF CLASS PROBABILITIES ===");
  for(int c=0;c<RF_NUM_CLASSES;c++){
    Serial.printf("%s: %.3f\n",
        RF_CLASS_NAMES[c],
        scores[c]/RF_NUM_TREES);
  }
  Serial.println("==============================");

  int idx=0;
  for(int c=1;c<RF_NUM_CLASSES;c++)if(scores[c]>scores[idx])idx=c;
  confidence=scores[idx]/RF_NUM_TREES;
  // One machine-readable record per completed inference for live validation.
  Serial.print("RF_PROBS");
  for(int c=0;c<RF_NUM_CLASSES;c++){
    Serial.printf(",%s=%.4f", RF_CLASS_NAMES[c], scores[c]/RF_NUM_TREES);
  }
  Serial.println();
  return idx;
}

void triggerGesture(int index,float confidence){
  if(index<0 || index>=RF_NUM_CLASSES) return;
  const char* label=RF_CLASS_NAMES[index];
  lastLabel=label;
  lastConfidence=confidence;
  Serial.printf("ML %s probability=%.3f\n",label,confidence);
  if(strcmp(label,"idle")==0){lastActionName="IDLE / NO ACTION";return;}
  float requiredConf=CONF_MIN;
  if(strcmp(label,"double_flick")==0) requiredConf=0.55f;
  else if(strcmp(label,"swipe_up")==0) requiredConf=0.55f;
  else if(strcmp(label,"swipe_down")==0) requiredConf=0.50f; // v4 tentative

  if(confidence<requiredConf){
    lastActionName="LOW CONF / NO ACTION";
    return;
  }
  const uint32_t now=millis();
  if(now-lastAction<REARM_MS) {lastActionName="COOLDOWN";return;}
  const char* cmd=nullptr;
  const char* display=nullptr;
  if(strcmp(label,"double_flick")==0){cmd="LEFT_CLICK";display="LEFT CLICK";}
  else if(strcmp(label,"semicircle_left")==0){cmd="PREV_TAB";display="PREVIOUS TAB";}
  else if(strcmp(label,"swipe_down")==0){cmd="SCROLL_DOWN";display="SCROLL DOWN";}
  else if(strcmp(label,"swipe_up")==0){cmd="SCROLL_UP";display="SCROLL UP";}
  if(!cmd) {lastActionName="UNKNOWN / NO ACTION";return;}
  if(sendCommand(cmd)){
    lastActionName=display;
    lastAction=now;
    Serial.printf("SENT: %s\n",cmd);
  }else{
    lastActionName="WIFI / NOT SENT";
    Serial.printf("NOT SENT: %s (check WiFi)\n",cmd);
  }
}

void processMotion(const float v[DIM]){
  const uint32_t now=millis();
  // QMI8658 gyroscope is expected to be in degrees/second.
  const float gyroMotion=sqrtf(v[3]*v[3]+v[4]*v[4]+v[5]*v[5]);
  const float accelNorm=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
  const bool stationary=gyroMotion<15.0f; // measured stationary baseline ~8 dps

  // Print useful diagnostics every 500 ms, without flooding serial output.
  if(now-lastDebug>=500){
    lastDebug=now;
    Serial.printf("DEBUG gyro=%.2f accelNorm=%.3f stationary=%d armed=%d samples=%d mode=%d\n",
        gyroMotion,accelNorm,(int)stationary,(int)gestureArmed,sampleCount,(int)gestureMode);
  }

  if(!gestureMode){
    sampleCount=0;
    gestureArmed=false;
    stillSince=0;
    idleSent=false;
    if(now-lastCursor>=20){
      lastCursor=now;
      const float hx=fabsf(v[5])>=CURSOR_DEADZONE_DPS?v[5]:0.0f;
      const float hy=fabsf(v[3])>=CURSOR_DEADZONE_DPS?v[3]:0.0f;
      const int dx=constrain((int)lroundf(hx*CURSOR_SPEED),-20,20);
      const int dy=constrain((int)lroundf(hy*CURSOR_SPEED),-20,20);
      if(dx||dy)sendCommand("MOVE",dx,dy);
    }
    return;
  }

  // Complete a capture even if movement ceases before the first second ends.
  if(sampleCount>0){
    if (sampleCount < RAW_SAMPLES) {
      for (int a=0; a<DIM; ++a) windowData[sampleCount][a]=v[a];
      windowTimes[sampleCount]=micros()-gestureStartUs;
      ++sampleCount;
    }
    if(windowTimes[sampleCount-1] >= 2000000UL || sampleCount >= RAW_SAMPLES){
      const uint32_t captureElapsedUs = windowTimes[sampleCount-1];
      const float measuredHz = captureElapsedUs > 0
          ? (sampleCount - 1) * 1000000.0f / captureElapsedUs
          : 0.0f;
      Serial.printf("CAPTURE: samples=%d duration_ms=%.1f actual_hz=%.1f\n",
                    sampleCount,
                    captureElapsedUs / 1000.0f,
                    measuredHz);

      float feats[wand400::FEATURES];
      if(wand400::resample(windowTimes,windowData,sampleCount,resampledWindow)){
        wand400::features(resampledWindow,feats);
        float conf=0.0f;
        int label=predictRF(feats,conf);
        triggerGesture(label,conf);
      } else {
        Serial.println("ERROR: invalid capture timestamps");
      }
      sampleCount=0;
      gestureArmed=false;
      stillSince=0;
      lastGestureEnd=millis();
      nextSample=micros()+SAMPLE_US; // restart cadence after inference cost
      Serial.println("WAITING FOR IDLE");
    }
    return;
  }

  if(stationary){
    if(stillSince==0)stillSince=now;
    const uint32_t quietMs=now-stillSince;
    if(quietMs>=300 && now-lastGestureEnd>=REARM_MS){
      if(!gestureArmed){
        gestureArmed=true;
        Serial.println("READY FOR GESTURE");
      }
    }
    return;
  }

  // Movement interrupted the quiet interval.
  stillSince=0;
  idleSent=false;
  if(!gestureArmed || now-lastGestureEnd<REARM_MS)return;

  gestureArmed=false;
  gestureStartUs=micros();
  for(int a=0;a<DIM;a++)windowData[0][a]=v[a];
  windowTimes[0]=0;
  sampleCount=1;
  Serial.println("GESTURE START");
}

void setup(){
  Serial.begin(115200);delay(700);
  Wire1.begin(PMU_SDA,PMU_SCL);
  if(!pmu.init())die("AXP2101 not found");
  pmu.setALDO1Voltage(3300);pmu.enableALDO1(); // OLED power
  pmu.setALDO2Voltage(3300);pmu.enableALDO2(); // IMU power
  delay(200);
  Wire.begin(I2C_SDA,I2C_SCL);Wire.setClock(400000);
  oled.begin();drawOled();
  pinMode(SD_CS,OUTPUT);digitalWrite(SD_CS,HIGH);
  imuBus.begin(IMU_SCK,IMU_MISO,IMU_MOSI,IMU_CS);
  if(!imu.begin(imuBus,IMU_CS))die("QMI8658 SPI failure");
  if(!imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
           SensorQMI8658::ACC_ODR_500Hz,SensorQMI8658::LPF_MODE_0))die("IMU accel config");
  if(!imu.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
           SensorQMI8658::GYR_ODR_448_4Hz,SensorQMI8658::LPF_MODE_0))die("IMU gyro config");
  imu.enableGyroscope();imu.enableAccelerometer();
  pinMode(BOOT_PIN,INPUT_PULLUP);
  bootSession=esp_random();
  WiFi.mode(WIFI_STA);WiFi.begin(WAND_WIFI_SSID,WAND_WIFI_PASSWORD);
  Serial.println("V4 SAFE DRY-RUN (NO UDP); BOOT toggles CURSOR/GESTURE");
  nextSample=micros()+SAMPLE_US;
}

void loop(){
  bool b=digitalRead(BOOT_PIN);
  static uint32_t lastToggle=0;
  if(lastButton==HIGH && b==LOW && millis()-lastToggle>350){
    gestureMode=!gestureMode;sampleCount=0;stillSince=0;idleSent=false;gestureArmed=false;lastGestureEnd=millis();
    lastToggle=millis();lastAction=millis();lastActionName="MODE SWITCH";
    Serial.printf("MODE: %s\n",gestureMode?"GESTURES":"CURSOR");
  }
  lastButton=b;
  if(WiFi.status()==WL_CONNECTED && !wifiAnnounced){
    wifiAnnounced=true;Serial.print("ESP32 WiFi IP: ");Serial.println(WiFi.localIP());
  }
  if(WiFi.status()!=WL_CONNECTED) wifiAnnounced=false;
  if(WiFi.status()!=WL_CONNECTED && millis()-lastWifiTry>10000){
    lastWifiTry=millis();WiFi.reconnect();
  }
  if(millis()-lastOled>=250){lastOled=millis();drawOled();}
  uint32_t now=micros();
  // Yield while waiting for the next 400 Hz target sample: avoid starving FreeRTOS idle/Wi-Fi tasks.
  if((int32_t)(now-nextSample)<0){delayMicroseconds(100);return;}
  nextSample=now+SAMPLE_US;
  if(!imu.getDataReady()){return;}
  float ax,ay,az,gx,gy,gz;
  if(!imu.getAccelerometer(ax,ay,az) || !imu.getGyroscope(gx,gy,gz)){
    delay(1);
    return;
  }
  float v[6]={ax,ay,az,gx,gy,gz};
  processMotion(v);
  delay(0); // Let other tasks run after handling a sample.
}
