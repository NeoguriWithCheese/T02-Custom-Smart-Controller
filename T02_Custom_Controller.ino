/*
 * =====================================================================================
 * T02 Custom Smart Controller (ESP32)
 * =====================================================================================
 * 
 * 🚨 LEGAL DISCLAIMER & WARNING 🚨
 * HIGH POWER ELECTRONICS INVOLVED! This code is designed to drive a 24V, 120W motor. 
 * Incorrect wiring or hardware failure can result in fire, property damage, or severe injury.
 * 
 * This software is provided "AS IS", without warranty of any kind, express or implied.
 * In no event shall the authors or copyright holders be liable for any claim, damages, 
 * or other liability, whether in an action of contract, tort or otherwise, arising from,
 * out of or in connection with the software or the use or other dealings in the software.
 * YOU ASSEMBLE AND USE THIS DEVICE ENTIRELY AT YOUR OWN RISK.
 * 
 * 🏷️ TRADEMARK NOTICE:
 * "Lovense" and "Hismith" are registered trademarks of their respective owners. 
 * This is an UNOFFICIAL, community-driven DIY project and is NOT affiliated with, 
 * endorsed by, or sponsored by any of these companies. Brand names and BLE UUIDs 
 * are used within this code purely for the legally protected purpose of software interoperability.
 * 
 * 📜 LICENSE: MIT License
 * =====================================================================================
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_system.h>


// ============================================================
// LOVENSE UUID
// ============================================================


#define LOVENSE_SERVICE_UUID \
  "66300001-0023-4bd4-bbd5-a6920e4c5653"


#define LOVENSE_TX_UUID \
  "66300002-0023-4bd4-bbd5-a6920e4c5653"


#define LOVENSE_RX_UUID \
  "66300003-0023-4bd4-bbd5-a6920e4c5653"


// ============================================================
// HISMITH UUID
// ============================================================


#define HISMITH_SERVICE_UUID \
  "0000ffe5-0000-1000-8000-00805f9b34fb"


#define HISMITH_WRITE_UUID \
  "0000ffe9-0000-1000-8000-00805f9b34fb"


#define HISMITH_NOTIFY_SERVICE_UUID \
  "0000ffe0-0000-1000-8000-00805f9b34fb"


#define HISMITH_NOTIFY_UUID \
  "0000ffe4-0000-1000-8000-00805f9b34fb"


#define HISMITH_MODEL_SERVICE_UUID \
  "0000ff90-0000-1000-8000-00805f9b34fb"


#define HISMITH_MODEL_UUID \
  "0000ff96-0000-1000-8000-00805f9b34fb"


// ============================================================
// PIN
// ============================================================


// BTS7960
#define RPWM_PIN        25
#define LPWM_PIN        26
#define R_EN_PIN        27
#define L_EN_PIN        14


// B100K
#define POT_PIN         32


// Mode button
#define MODE_BUTTON_PIN 33


// ============================================================
// PWM
// ============================================================


#define MAX_PWM         255
#define MIN_PWM         28   // 모터가 돌지 않고 소리만 나는 구간을 피할 최소 PWM 값 (유저 튜닝 완료)
#define RAMP_STEP       25   // (기존 2 -> 25) 모터 가속 속도. 러벤스 패턴의 급격한 변화에 즉각 반응하도록 100ms 이내에 도달하게 세팅


// 고주파 소음(삐~ 소리)을 사람 귀에 안 들리는 초음파 영역으로 밀어내기 위해 20kHz 적용
#define PWM_FREQUENCY   20000
#define PWM_RESOLUTION  8


// ============================================================
// MODE BUTTON
// ============================================================


#define DEBOUNCE_MS 50


// ============================================================
// CONTROL MODE
// ============================================================


enum ControlMode
{
  MODE_MANUAL = 0,
  MODE_LOVENSE = 1,
  MODE_HISMITH = 2
};


ControlMode currentMode = MODE_MANUAL;


// ============================================================
// LED BLINKER
// ============================================================


#define LED_PIN 2 // ESP32 내장 파란색 LED (또는 외장 LED 연결 가능)

int blinkCountTarget = 0;
int currentBlinks = 0;
bool ledState = false;
unsigned long lastBlinkTime = 0;
const int blinkInterval = 250; // 깜빡임 속도 (250ms 켜짐, 250ms 꺼짐)

void triggerModeLED() {
  if (currentMode == MODE_MANUAL) blinkCountTarget = 1;
  else if (currentMode == MODE_LOVENSE) blinkCountTarget = 2;
  else if (currentMode == MODE_HISMITH) blinkCountTarget = 3;
  
  currentBlinks = 0;
  ledState = true;
  // ESP32 Core v3 방식: 핀 번호를 직접 사용합니다.
  ledcWrite(LED_PIN, 10); 
  lastBlinkTime = millis();
}

void updateLED() {
  if (blinkCountTarget > 0) {
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      ledState = !ledState;
      
      if (ledState) {
        ledcWrite(LED_PIN, 10); // 소프트웨어 저항 (밝기 4%)
      } else {
        ledcWrite(LED_PIN, 0); // 끄기 (0%)
      }
      
      // 꺼졌을 때를 1회 완료로 계산
      if (!ledState) {
        currentBlinks++;
        if (currentBlinks >= blinkCountTarget) {
          blinkCountTarget = 0; // 깜빡임 종료
        }
      }
    }
  }
}


// ============================================================
// PREFERENCES
// ============================================================


Preferences preferences;


// ============================================================
// BLE
// ============================================================


NimBLEServer* server = nullptr;


// Lovense
NimBLECharacteristic* lovenseTx = nullptr;
NimBLECharacteristic* lovenseRx = nullptr;


// HISMITH
NimBLECharacteristic* hismithRx = nullptr;
NimBLECharacteristic* hismithTx = nullptr;
NimBLECharacteristic* hismithModel = nullptr;


bool appConnected = false;


// ============================================================
// SPEED STATE
// ============================================================


// Lovense
int lovenseThrusting = 0;


// HISMITH
int hismithSpeed = 0;


// B100K
int potADC = 0;
int manualPercent = 0;


// Final
int targetPercent = 0;
int targetPWM = 0;
int currentPWM = 0;


// ============================================================
// BUTTON STATE
// ============================================================


bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;


unsigned long debounceStart = 0;


// ============================================================
// PRINT HEX
// ============================================================


void printHex(
  const uint8_t* data,
  size_t length
)
{
  for (size_t i = 0; i < length; i++)
  {
    if (data[i] < 0x10)
      Serial.print("0");


    Serial.print(
      data[i],
      HEX
    );


    if (i < length - 1)
      Serial.print(" ");
  }


  Serial.println();
}


// ============================================================
// HISMITH CHECKSUM
// ============================================================


uint8_t hismithChecksum(
  uint8_t command,
  uint8_t value
)
{
  return command + value;
}


// ============================================================
// MOTOR STOP
// ============================================================


void motorStop()
{
  ledcWrite(
    RPWM_PIN,
    0
  );


  ledcWrite(
    LPWM_PIN,
    0
  );


  currentPWM = 0;
}


// ============================================================
// MOTOR PWM
// ============================================================


void writeMotorPWM(
  int pwm
)
{
  pwm =
    constrain(
      pwm,
      0,
      MAX_PWM
    );


  // T02는 한 방향 회전
  ledcWrite(
    LPWM_PIN,
    0
  );


  ledcWrite(
    RPWM_PIN,
    pwm
  );
}


// ============================================================
// SPEED -> PWM (데드존 보정됨)
// ============================================================


int percentToPWM(
  int percent
)
{
  percent =
    constrain(
      percent,
      0,
      100
    );


  if (percent == 0) 
  {
    return 0;
  }


  return map(
    percent,
    1,
    100,
    MIN_PWM,
    MAX_PWM
  );
}


// ============================================================
// B100K
// ============================================================


void readB100K()
{
  // 1. 다중 샘플링 (오버샘플링): 한 번만 읽지 않고 16번 읽어서 평균을 냅니다.
  // ESP32의 고질적인 아날로그 핀 노이즈(전압 튐 현상)를 1차적으로 억제합니다.
  long sumADC = 0;
  for (int i = 0; i < 16; i++) {
    // [천재적인 해킹 기법: 정전용량 초기화]
    // analogRead는 작동 시 칩 내부의 풀다운을 강제로 꺼버립니다.
    // 그래서 매번 읽기 직전에 풀다운을 억지로 다시 켜서 유령 신호(전자파)를 GND(0V)로 완벽히 씻어냅니다.
    pinMode(POT_PIN, INPUT_PULLDOWN);
    delayMicroseconds(100); 

    // 뽑혀있다면 씻겨나간 0V가 그대로 읽히고 (정지 유지)
    // 꽂혀있다면 100k옴 다이얼이 즉시 본래 전압을 채워넣어 정상 속도가 읽힙니다.
    sumADC += analogRead(POT_PIN);
  }
  int avgADC = sumADC / 16;

  // 2. EMA(지수 이동 평균) 필터: 이전 값과 현재 값을 섞어서 잔떨림을 없앱니다.
  // B100K처럼 저항값이 높은(100k옴) 다이얼은 노이즈에 매우 취약하므로 소프트웨어 필터가 필수입니다.
  static float smoothedADC = 0;
  float alpha = 0.1; // 0.1은 극강의 부드러움(약간의 묵직함), 1.0은 필터 없음
  
  // 처음에 0에서 시작할 때 튀는 것을 방지
  if (smoothedADC == 0) smoothedADC = avgADC; 
  smoothedADC = (alpha * avgADC) + ((1.0 - alpha) * smoothedADC);

  // 3. 커브 적용 및 퍼센트 변환
  float ratio = smoothedADC / 4095.0;
  
  // [미세 조절용 엑셀 곡선] 
  float curveRatio = ratio * ratio;

  int newPercent = curveRatio * 100.0;
  newPercent = constrain(newPercent, 0, 100);

  // 4. 히스테리시스(노이즈 밴드) 적용
  // 다이얼을 가만히 두었을 때 1% 단위로 왔다 갔다 떨리는 것을 방지합니다.
  if (abs(newPercent - manualPercent) >= 1 || newPercent == 0 || newPercent == 100) {
    manualPercent = newPercent;
  }
}


// ============================================================
// TARGET SPEED
// ============================================================


void calculateTarget()
{
  if (
    currentMode ==
    MODE_MANUAL
  )
  {
    targetPercent =
      manualPercent;


    return;
  }


  if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    targetPercent =
      map(
        lovenseThrusting,
        0,
        20,
        0,
        100
      );


    return;
  }


  if (
    currentMode ==
    MODE_HISMITH
  )
  {
    targetPercent =
      hismithSpeed;


    return;
  }
}


// ============================================================
// MOTOR RAMP
// ============================================================


void updateMotorRamp()
{
  static unsigned long lastRampUpdate = 0;


  if (
    millis() -
    lastRampUpdate <
    10
  )
  {
    return;
  }


  lastRampUpdate =
    millis();


  targetPercent =
    constrain(
      targetPercent,
      0,
      100
    );


  targetPWM =
    percentToPWM(
      targetPercent
    );


  if (
    currentPWM <
    targetPWM
  )
  {
    currentPWM +=
      RAMP_STEP;


    if (
      currentPWM >
      targetPWM
    )
    {
      currentPWM =
        targetPWM;
    }
  }
  else if (
    currentPWM >
    targetPWM
  )
  {
    currentPWM -=
      RAMP_STEP;


    if (
      currentPWM <
      targetPWM
    )
    {
      currentPWM =
        targetPWM;
    }
  }


  writeMotorPWM(
    currentPWM
  );
}


// ============================================================
// SAVE MODE
// ============================================================


void saveMode()
{
  preferences.begin(
    "controller",
    false
  );


  preferences.putUChar(
    "mode",
    (uint8_t)currentMode
  );


  preferences.end();
}


// ============================================================
// LOAD MODE
// ============================================================


void loadMode()
{
  // 전원을 껐다 켜더라도 무조건 이전에 마지막으로 저장되었던 모드를 불러옵니다.
  preferences.begin(
    "controller",
    true
  );


  uint8_t mode =
    preferences.getUChar(
      "mode",
      MODE_MANUAL
    );


  preferences.end();


  if (
    mode ==
    MODE_LOVENSE
  )
  {
    currentMode =
      MODE_LOVENSE;
  }
  else if (
    mode ==
    MODE_HISMITH
  )
  {
    currentMode =
      MODE_HISMITH;
  }
  else
  {
    currentMode =
      MODE_MANUAL;
  }
}


// ============================================================
// SWITCH MODE
// ============================================================


void switchMode()
{
  if (
    currentMode ==
    MODE_MANUAL
  )
  {
    currentMode =
      MODE_LOVENSE;
  }
  else if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    currentMode =
      MODE_HISMITH;
  }
  else
  {
    currentMode =
      MODE_MANUAL;
  }


  saveMode();


  Serial.println();
  Serial.println(
    "========================================"
  );


  Serial.print(
    "[MODE SWITCH] "
  );


  if (
    currentMode ==
    MODE_MANUAL
  )
  {
    Serial.println(
      "MANUAL"
    );
  }
  else if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    Serial.println(
      "LOVENSE"
    );
  }
  else
  {
    Serial.println(
      "HISMITH"
    );
  }


  Serial.println(
    "ESP32 RESTARTING..."
  );


  Serial.println(
    "========================================"
  );


  motorStop();


  delay(300);


  ESP.restart();
}


// ============================================================
// MODE BUTTON
// ============================================================


void updateModeButton()
{
  bool reading =
    digitalRead(
      MODE_BUTTON_PIN
    );


  if (
    reading !=
    lastButtonReading
  )
  {
    debounceStart =
      millis();
  }


  if (
    millis() -
    debounceStart >=
    DEBOUNCE_MS
  )
  {
    if (
      reading !=
      stableButtonState
    )
    {
      stableButtonState =
        reading;


      if (
        stableButtonState ==
        LOW
      )
      {
        switchMode();
      }
    }
  }


  lastButtonReading =
    reading;
}


// ============================================================
// LOVENSE NOTIFY
// ============================================================


void lovenseNotify(
  const String& text
)
{
  if (
    !appConnected ||
    lovenseTx == nullptr
  )
  {
    return;
  }


  lovenseTx->setValue(
    text.c_str()
  );


  lovenseTx->notify();


  Serial.print(
    "[LOVENSE NOTIFY] "
  );


  Serial.println(
    text
  );
}


// ============================================================
// HISMITH NOTIFY
// ============================================================


void hismithNotify(
  const uint8_t* data,
  size_t length
)
{
  if (
    !appConnected ||
    hismithTx == nullptr
  )
  {
    Serial.println(
      "[HISMITH NOTIFY SKIP]"
    );


    return;
  }


  hismithTx->setValue(
    data,
    length
  );


  bool result =
    hismithTx->notify();


  Serial.print(
    "[HISMITH NOTIFY] "
  );


  printHex(
    data,
    length
  );


  Serial.print(
    "[HISMITH NOTIFY RESULT] "
  );


  Serial.println(
    result
      ? "OK"
      : "FAILED"
  );
}


// ============================================================
// HISMITH RESPONSE 
// ============================================================


void hismithSendResponse(
  uint8_t prefix,
  uint8_t command,
  uint8_t value
)
{
  uint8_t packet[4];


  packet[0] =
    prefix; 


  packet[1] =
    command;


  packet[2] =
    value;


  packet[3] =
    hismithChecksum(
      command,
      value
    );


  Serial.print(
    "[HISMITH RESPONSE] "
  );


  printHex(
    packet,
    4
  );


  hismithNotify(
    packet,
    4
  );
}


// ============================================================
// SERVER CALLBACK
// ============================================================


class ServerCallbacks :
  public NimBLEServerCallbacks
{
  void onConnect(
    NimBLEServer* pServer,
    NimBLEConnInfo& connInfo
  )
  {
    appConnected =
      true;


    Serial.println();
    Serial.println(
      "########################################"
    );


    Serial.println(
      "PHONE CONNECTED"
    );


    Serial.println(
      "########################################"
    );


    Serial.print(
      "Peer: "
    );


    Serial.println(
      connInfo
        .getAddress()
        .toString()
        .c_str()
    );
  }


  void onDisconnect(
    NimBLEServer* pServer,
    NimBLEConnInfo& connInfo,
    int reason
  )
  {
    appConnected =
      false;


    lovenseThrusting =
      0;


    hismithSpeed =
      0;


    targetPercent =
      0;


    targetPWM =
      0;


    motorStop();


    Serial.println();
    Serial.println(
      "########################################"
    );


    Serial.println(
      "PHONE DISCONNECTED"
    );


    Serial.println(
      "########################################"
    );


    Serial.print(
      "Reason: "
    );


    Serial.println(
      reason
    );


    NimBLEDevice::startAdvertising();
  }
};


// ============================================================
// LOVENSE CALLBACK
// ============================================================


class LovenseRxCallbacks :
  public NimBLECharacteristicCallbacks
{
  void onWrite(
    NimBLECharacteristic* pCharacteristic,
    NimBLEConnInfo& connInfo
  )
  {
    std::string raw =
      pCharacteristic->getValue();


    if (
      raw.empty()
    )
    {
      return;
    }


    String cmd =
      String(
        raw.c_str()
      );


    Serial.println();
    Serial.println(
      "########################################"
    );


    Serial.println(
      "LOVENSE BLE WRITE"
    );


    Serial.println(
      "########################################"
    );


    Serial.print(
      "ASCII: "
    );


    Serial.println(
      cmd
    );


    if (
      cmd ==
      "DeviceType;"
    )
    {
      lovenseNotify(
        "f:35:01:02:03:04:05:06;"
      );


      return;
    }


    if (
      cmd ==
      "Battery;"
    )
    {
      lovenseNotify(
        "85;"
      );


      return;
    }


    if (
      cmd ==
      "GetCap;"
    )
    {
      lovenseNotify(
        "CAP:1;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "GetMode"
      )
    )
    {
      lovenseNotify(
        "F01MD:2;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "GetRBut"
      )
    )
    {
      lovenseNotify(
        "GetRBut:0;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "GetLight"
      )
    )
    {
      lovenseNotify(
        "off;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "Collect:"
      )
    )
    {
      lovenseNotify(
        "OK;"
      );


      return;
    }


    if (
      cmd ==
      "AutoTime;"
    )
    {
      lovenseNotify(
        "OK;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "AI,"
      )
    )
    {
      lovenseNotify(
        "OK;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "GetAS"
      )
    )
    {
      lovenseNotify(
        "OK;"
      );


      return;
    }


    if (
      cmd ==
      "PowerOff;"
    )
    {
      lovenseThrusting =
        0;


      targetPercent =
        0;


      targetPWM =
        0;


      motorStop();


      lovenseNotify(
        "OK;"
      );


      return;
    }


    if (
      cmd.startsWith(
        "Thrusting:"
      )
    )
    {
      int colon =
        cmd.indexOf(':');


      int semicolon =
        cmd.indexOf(';');


      if (
        colon >= 0 &&
        semicolon > colon
      )
      {
        String valueString =
          cmd.substring(
            colon + 1,
            semicolon
          );


        int value =
          valueString.toInt();


        lovenseThrusting =
          constrain(
            value,
            0,
            20
          );


        Serial.print(
          "[THRUSTING] "
        );


        Serial.println(
          lovenseThrusting
        );
      }


      return;
    }


    lovenseNotify(
      "OK;"
    );
  }
};


// ============================================================
// HISMITH CALLBACK 
// ============================================================


class HismithRxCallbacks :
  public NimBLECharacteristicCallbacks
{
  void onWrite(
    NimBLECharacteristic* pCharacteristic,
    NimBLEConnInfo& connInfo
  )
  {
    std::string raw =
      pCharacteristic->getValue();


    if (
      raw.empty()
    )
    {
      return;
    }


    Serial.println();
    Serial.println(
      "########################################"
    );


    Serial.println(
      "HISMITH BLE WRITE"
    );


    Serial.println(
      "########################################"
    );


    Serial.print(
      "Length: "
    );


    Serial.println(
      raw.length()
    );


    Serial.print(
      "HEX: "
    );


    printHex(
      (const uint8_t*)raw.data(),
      raw.length()
    );


    if (
      raw.length() <
      4
    )
    {
      Serial.println(
        "[HISMITH] Invalid packet"
      );


      return;
    }


    const uint8_t* data =
      (const uint8_t*)raw.data();


    uint8_t prefix =
      data[0];


    uint8_t command =
      data[1];


    uint8_t value =
      data[2];


    uint8_t checksum =
      data[3];


    Serial.print(
      "[PREFIX] 0x"
    );


    Serial.println(
      prefix,
      HEX
    );


    Serial.print(
      "[COMMAND] 0x"
    );


    Serial.println(
      command,
      HEX
    );


    Serial.print(
      "[VALUE] 0x"
    );


    Serial.println(
      value,
      HEX
    );


    Serial.print(
      "[CHECKSUM] 0x"
    );


    Serial.println(
      checksum,
      HEX
    );


    // ========================================================
    // AA
    // ========================================================


    if (
      prefix ==
      0xAA
    )
    {
      switch (
        command
      )
      {
        case 0x01:
        {
          Serial.print(
            "[HISMITH AA] COMMAND 01, VALUE = "
          );


          Serial.println(
            value,
            HEX
          );


          hismithSendResponse(
            prefix,
            0x01,
            value
          );


          break;
        }


        case 0x02:
        {
          hismithSpeed =
            0;


          targetPercent =
            0;


          targetPWM =
            0;


          Serial.println(
            "[HISMITH AA] STOP"
          );


          hismithSendResponse(
            prefix,
            0x02,
            0x00
          );


          break;
        }


        // ====================================================
        // AA 03 = SPEED QUERY
        // ====================================================


        case 0x03:
        {
          Serial.print(
            "[HISMITH AA] SPEED QUERY -> "
          );


          Serial.print(
            hismithSpeed
          );


          Serial.println(
            "%"
          );


          hismithSendResponse(
            prefix,
            0x03,
            hismithSpeed
          );


          break;
        }


        // ====================================================
        // AA 04 = SPEED SET
        // ====================================================


        case 0x04:
        {
          hismithSpeed =
            constrain(
              value,
              0,
              100
            );


          Serial.print(
            "[HISMITH AA] SPEED = "
          );


          Serial.print(
            hismithSpeed
          );


          Serial.println(
            "%"
          );


          hismithSendResponse(
            prefix,
            0x04,
            hismithSpeed
          );


          break;
        }


        default:
        {
          Serial.println(
            "[HISMITH AA] UNKNOWN COMMAND -> DUMMY BATTERY 85%"
          );


          hismithSendResponse(
            prefix,
            command,
            0x55
          );


          break;
        }
      }


      return;
    }


    // ========================================================
    // FF
    // ========================================================


    if (
      prefix ==
      0xFF
    )
    {
      switch (
        command
      )
      {
        case 0x01:
        {
          Serial.print(
            "[HISMITH FF] MODE = "
          );


          Serial.println(
            value,
            HEX
          );


          hismithSendResponse(
            prefix,
            0x01,
            value
          );


          break;
        }


        case 0x02:
        {
          hismithSpeed =
            0;


          targetPercent =
            0;


          targetPWM =
            0;


          Serial.println(
            "[HISMITH FF] STOP"
          );


          hismithSendResponse(
            prefix,
            0x02,
            0x00
          );


          break;
        }


        case 0x03:
        {
          if (
            value ==
            0xA0
          )
        {
            Serial.print(
              "[HISMITH FF] SPEED QUERY -> "
            );


            Serial.print(
              hismithSpeed
            );


            Serial.println(
              "%"
            );


            hismithSendResponse(
              prefix,
              0x03,
              hismithSpeed
            );
          }
          else
          {
            hismithSpeed =
              constrain(
                value,
                0,
                100
              );


            Serial.print(
              "[HISMITH FF] SPEED = "
            );


            Serial.print(
              hismithSpeed
            );


            Serial.println(
              "%"
            );


            hismithSendResponse(
              prefix,
              0x03,
              hismithSpeed
            );
          }


          break;
        }


        case 0x04:
        {
          Serial.print(
            "[HISMITH FF] COMMAND 04 = "
          );


          Serial.println(
            value,
            HEX
          );


          hismithSendResponse(
            prefix,
            0x04,
            value
          );


          break;
        }


        case 0x06:
        {
          hismithSendResponse(
            prefix,
            0x06,
            value
          );


          break;
        }


        case 0x07:
        {
          hismithSendResponse(
            prefix,
            0x07,
            value
          );


          break;
        }


        case 0x08:
        {
          hismithSendResponse(
            prefix,
            0x08,
            value
          );


          break;
        }


        default:
        {
          Serial.println(
            "[HISMITH FF] UNKNOWN COMMAND -> DUMMY BATTERY 85%"
          );


          hismithSendResponse(
            prefix,
            command,
            0x55
          );


          break;
        }
      }


      return;
    }


    Serial.println(
      "[HISMITH] UNKNOWN PREFIX"
    );
  }
};


// ============================================================
// HISMITH MODEL
// ============================================================


class HismithModelCallbacks :
  public NimBLECharacteristicCallbacks
{
  void onRead(
    NimBLECharacteristic* pCharacteristic,
    NimBLEConnInfo& connInfo
  )
  {
    // 사실 앱에서 이 값을 제대로 읽지 않지만 만약을 위해 1001 로 세팅
    const char* model =
      "1001";


    pCharacteristic->setValue(
      model
    );


    Serial.println(
      "[HISMITH] Model read -> 1001"
    );
  }
};


// ============================================================
// SETUP
// ============================================================


void setup()
{
  // ==========================================================
  // [수정됨] 앱 캐시 우회를 위한 기기 주민등록번호(MAC) 3차 변경
  // ==========================================================
  uint8_t fake_mac[6] = {0xDD, 0xEE, 0xFF, 0xAA, 0xBB, 0xDD};
  esp_base_mac_addr_set(fake_mac);


  Serial.begin(
    115200
  );


  delay(
    1000
  );


  loadMode();


  Serial.println();
  Serial.println();


  Serial.println(
    "############################################"
  );


  Serial.println(
    "# T02 HISMITH / LOVENSE CONTROLLER"
  );


  Serial.println(
    "############################################"
  );


  if (
    currentMode ==
    MODE_MANUAL
  )
  {
    Serial.println(
      "BOOT MODE: MANUAL (BLE OFF)"
    );
  }
  else if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    Serial.println(
      "BOOT MODE: LOVENSE"
    );
  }
  else
  {
    Serial.println(
      "BOOT MODE: HISMITH"
    );
  }


  // ==========================================================
  // GPIO
  // ==========================================================


  pinMode(
    R_EN_PIN,
    OUTPUT
  );


  pinMode(
    L_EN_PIN,
    OUTPUT
  );


  pinMode(
    POT_PIN,
    INPUT
  );


  pinMode(
    MODE_BUTTON_PIN,
    INPUT_PULLUP
  );


  digitalWrite(
    R_EN_PIN,
    HIGH
  );


  digitalWrite(
    L_EN_PIN,
    HIGH
  );


  // ==========================================================
  // PWM
  // ==========================================================


  bool rAttached =
    ledcAttach(
      RPWM_PIN,
      PWM_FREQUENCY,
      PWM_RESOLUTION
    );


  bool lAttached =
    ledcAttach(
      LPWM_PIN,
      PWM_FREQUENCY,
      PWM_RESOLUTION
    );


  if (
    !rAttached ||
    !lAttached
  )
  {
    Serial.println(
      "[ERROR] LEDC attach failed!"
    );


    while (
      true
    )
    {
      delay(
        1000
      );
    }
  }


  motorStop();


  // ==========================================================
  // ADC
  // ==========================================================


  analogReadResolution(
    12
  );


  analogSetPinAttenuation(
    POT_PIN,
    ADC_11db
  );


  // ==========================================================
  // BLE (수동 모드 제외)
  // ==========================================================


  if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    // ========================================================
    // LOVENSE
    // ========================================================


    NimBLEDevice::init(
      "LVS-f35"
    );


    NimBLEDevice::setPower(
      ESP_PWR_LVL_P9
    );


    server =
      NimBLEDevice::createServer();


    server->setCallbacks(
      new ServerCallbacks()
    );


    NimBLEService* service =
      server->createService(
        LOVENSE_SERVICE_UUID
      );


    lovenseTx =
      service->createCharacteristic(
        LOVENSE_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
      );


    lovenseRx =
      service->createCharacteristic(
        LOVENSE_RX_UUID,
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::WRITE_NR
      );


    lovenseRx->setCallbacks(
      new LovenseRxCallbacks()
    );


    service->start();


    NimBLEAdvertising* advertising =
      NimBLEDevice::getAdvertising();


    advertising->setName(
      "LVS-f35"
    );


    advertising->addServiceUUID(
      LOVENSE_SERVICE_UUID
    );


    advertising->start();
  }
  else if (
    currentMode ==
    MODE_HISMITH
  )
  {
    // ========================================================
    // HISMITH
    // ========================================================


    NimBLEDevice::init(
      "HISMITH"
    );


    NimBLEDevice::setPower(
      ESP_PWR_LVL_P9
    );


    server =
      NimBLEDevice::createServer();


    server->setCallbacks(
      new ServerCallbacks()
    );


    // FFE5 / FFE9
    NimBLEService* writeService =
      server->createService(
        HISMITH_SERVICE_UUID
      );


    hismithRx =
      writeService->createCharacteristic(
        HISMITH_WRITE_UUID,
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::WRITE_NR
      );


    hismithRx->setCallbacks(
      new HismithRxCallbacks()
    );


    writeService->start();


    // FFE0 / FFE4
    NimBLEService* notifyService =
      server->createService(
        HISMITH_NOTIFY_SERVICE_UUID
      );


    hismithTx =
      notifyService->createCharacteristic(
        HISMITH_NOTIFY_UUID,
        NIMBLE_PROPERTY::NOTIFY
      );


    notifyService->start();


    // FF90 / FF96
    NimBLEService* modelService =
      server->createService(
        HISMITH_MODEL_SERVICE_UUID
      );


    hismithModel =
      modelService->createCharacteristic(
        HISMITH_MODEL_UUID,
        NIMBLE_PROPERTY::READ
      );


    hismithModel->setCallbacks(
      new HismithModelCallbacks()
    );


    modelService->start();


    // ========================================================
    // HISMITH BATTERY SERVICE (Standard 180F)
    // ========================================================
    NimBLEService* batteryService =
      server->createService(
        NimBLEUUID((uint16_t)0x180f)
      );


    NimBLECharacteristic* batteryLevel =
      batteryService->createCharacteristic(
        NimBLEUUID((uint16_t)0x2a19),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
      );


    uint8_t dummyBattery = 85;
    batteryLevel->setValue(&dummyBattery, 1);
    batteryService->start();


    NimBLEAdvertising* advertising =
      NimBLEDevice::getAdvertising();


    // ========================================================
    // [수정됨] 이름 앞에 1001 코드를 강제 주입하여 송출
    // ========================================================
    std::string advName = "";
    advName += (char)0x10;
    advName += (char)0x01;
    advName += "HISMITH";
    advertising->setName(advName);


    advertising->addServiceUUID(
      HISMITH_SERVICE_UUID
    );


    advertising->addServiceUUID(
      HISMITH_NOTIFY_SERVICE_UUID
    );


    advertising->addServiceUUID(
      NimBLEUUID((uint16_t)0x180f)
    );


    advertising->start();
  }


  // ==========================================================
  // READY
  // ==========================================================


  Serial.println();
  Serial.println(
    "========================================"
  );


  Serial.println(
    "SYSTEM READY"
  );


  Serial.println(
    "========================================"
  );


  if (
    currentMode ==
    MODE_MANUAL
  )
  {
    Serial.println(
      "Profile: MANUAL (BLE OFF)"
    );
  }
  else if (
    currentMode ==
    MODE_LOVENSE
  )
  {
    Serial.println(
      "Profile: LOVENSE XMachine"
    );


    Serial.println(
      "Name: LVS-f35"
    );
  }
  else
  {
    Serial.println(
      "Profile: HISMITH"
    );


    Serial.println(
      "Name: HISMITH"
    );
  }


  Serial.print(
    "PWM: "
  );


  Serial.print(
    PWM_FREQUENCY
  );


  Serial.println(
    " Hz"
  );


  Serial.println(
    "Waiting..."
  );


  // ==========================================================
  // LED 셋업 및 모드 알림 시작
  // ==========================================================
  // LED를 위한 ledc 할당 (ESP32 Core 3.x 최신 API 적용)
  ledcAttach(LED_PIN, 5000, 8); 
  ledcWrite(LED_PIN, 0); // 초기 상태 끄기
  triggerModeLED();
}


// ============================================================
// LOOP
// ============================================================


void loop()
{
  updateLED();


  updateModeButton();


  readB100K();


  calculateTarget();


  updateMotorRamp();


  static unsigned long lastPrint = 0;


  if (
    millis() -
    lastPrint >=
    500
  )
  {
    lastPrint =
      millis();


    Serial.println();
    Serial.println(
      "--------------- STATUS ---------------"
    );


    Serial.print(
      "MODE: "
    );


    if (
      currentMode ==
      MODE_MANUAL
    )
    {
      Serial.println(
        "MANUAL"
      );
    }
    else if (
      currentMode ==
      MODE_LOVENSE
    )
    {
      Serial.println(
        "LOVENSE"
      );
    }
    else
    {
      Serial.println(
        "HISMITH"
      );
    }


    if (
      currentMode !=
      MODE_MANUAL
    )
    {
      Serial.print(
        "BLE: "
      );


      Serial.println(
        appConnected
          ? "CONNECTED"
          : "DISCONNECTED"
      );
    }
    else
    {
      Serial.println(
        "BLE: OFF (MANUAL MODE)"
      );
    }


    Serial.print(
      "B100K ADC: "
    );


    Serial.println(
      potADC
    );


    Serial.print(
      "MANUAL SPEED: "
    );


    Serial.print(
      manualPercent
    );


    Serial.println(
      "%"
    );


    Serial.print(
      "LOVENSE THRUST: "
    );


    Serial.print(
      lovenseThrusting
    );


    Serial.println(
      "/20"
    );


    Serial.print(
      "HISMITH SPEED: "
    );


    Serial.print(
      hismithSpeed
    );


    Serial.println(
      "%"
    );


    Serial.print(
      "TARGET: "
    );


    Serial.print(
      targetPercent
    );


    Serial.println(
      "%"
    );


    Serial.print(
      "TARGET PWM: "
    );


    Serial.println(
      targetPWM
    );


    Serial.print(
      "CURRENT PWM: "
    );


    Serial.println(
      currentPWM
    );


    Serial.println(
      "---------------------------------------"
    );
  }


  delay(5);
}
