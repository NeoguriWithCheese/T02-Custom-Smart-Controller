#include <NimBLEDevice.h>
#include <esp_mac.h>

// 모터 제어 핀 (본인의 배선에 맞게 수정 필요)
const int PWM_PIN_R = 18;
const int PWM_PIN_L = 19;
const int EN_PIN_R = 22;
const int EN_PIN_L = 23;

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
bool deviceConnected = false;
String deviceMacAddress = "";

// 🚀 1. MAC 주소 스푸핑용 (러벤스 서버 락 우회)
// 혹시라도 여전히 락이 걸린다면, 본인이 소유하신 Edge 2의 실제 MAC 주소를 여기에 적어주시면 완벽합니다.
uint8_t spoofed_mac[6] = {0x04, 0x7F, 0x0E, 0x11, 0x22, 0x33}; 

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        deviceConnected = true;
        Serial.println("✅ 블루투스 연결 성공!");
    }
    void onDisconnect(NimBLEServer* pServer) override {
        deviceConnected = false;
        Serial.println("❌ 블루투스 연결 끊김!");
        // 연결이 끊어지면 안전을 위해 모터 정지
        ledcWrite(0, 0);
        ledcWrite(1, 0);
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            String cmd = String(rxValue.c_str());
            Serial.print("수신된 명령어: ");
            Serial.println(cmd);

            // 🚀 2. PC Xtoys 호환성을 위해 Lovense Max(b:40)로 기기 타입 응답
            if (cmd.startsWith("DeviceType;")) {
                String response = "b:40:" + deviceMacAddress + ";";
                pTxCharacteristic->setValue(response.c_str());
                pTxCharacteristic->notify();
            } 
            else if (cmd.startsWith("Battery;")) {
                pTxCharacteristic->setValue("80;"); // 배터리 80% 가짜 응답
                pTxCharacteristic->notify();
            }
            else if (cmd.startsWith("PowerOff;")) {
                pTxCharacteristic->setValue("OK;");
                pTxCharacteristic->notify();
                ledcWrite(0, 0);
                ledcWrite(1, 0);
            }
            // 🚀 3. Max 진동 명령(V:)을 피스톤 속도로 매핑 처리
            else if (cmd.startsWith("V:")) { 
                int speedIndex = cmd.indexOf(':');
                int end = cmd.indexOf(';');
                if (speedIndex != -1 && end != -1) {
                    int speed = cmd.substring(speedIndex + 1, end).toInt(); // 러벤스 속도: 0 ~ 20
                    int pwmValue = map(speed, 0, 20, 0, 255); // 0~20을 PWM 0~255로 변환
                    Serial.print("모터 속도 적용: ");
                    Serial.println(pwmValue);
                    
                    // 모터 구동 로직 (단방향 피스톤 구동)
                    if (pwmValue > 0) {
                        ledcWrite(0, pwmValue); 
                        ledcWrite(1, 0);
                    } else {
                        ledcWrite(0, 0);
                        ledcWrite(1, 0);
                    }
                }
                pTxCharacteristic->setValue("OK;");
                pTxCharacteristic->notify();
            }
            // 그 외 알 수 없는 명령이나 상태 체크는 모두 OK 반환으로 에러 방지
            else {
                pTxCharacteristic->setValue("OK;");
                pTxCharacteristic->notify();
            }
        }
    }
};

void setup() {
    Serial.begin(115200);

    // MAC 주소 강제 변경 (가짜 폰/가짜 기기 락 우회)
    esp_base_mac_addr_set(spoofed_mac);
    
    // 모터 핀 초기화
    pinMode(EN_PIN_R, OUTPUT);
    pinMode(EN_PIN_L, OUTPUT);
    digitalWrite(EN_PIN_R, HIGH);
    digitalWrite(EN_PIN_L, HIGH);
    
    ledcSetup(0, 5000, 8); // 채널 0, 5kHz, 8비트 해상도
    ledcSetup(1, 5000, 8); // 채널 1
    ledcAttachPin(PWM_PIN_R, 0);
    ledcAttachPin(PWM_PIN_L, 1);

    // 🚀 블루투스 기기 이름을 Max로 위장 (모든 PC앱 및 구버전 앱 100% 호환성 확보)
    NimBLEDevice::init("LVS-Max4");

    // 변경된 MAC 주소를 텍스트로 저장
    const uint8_t* mac = NimBLEDevice::getAddress().getNative();
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    deviceMacAddress = String(macStr);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // 러벤스 서비스 생성
    NimBLEService *pService = pServer->createService("53300001-0023-4bd4-bbd5-a6920e4c5653");

    pTxCharacteristic = pService->createCharacteristic(
        "53300003-0023-4bd4-bbd5-a6920e4c5653",
        NIMBLE_PROPERTY_NOTIFY
    );

    NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        "53300002-0023-4bd4-bbd5-a6920e4c5653",
        NIMBLE_PROPERTY_WRITE | NIMBLE_PROPERTY_WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    pService->start();

    // 🚀 4. 광고(Advertising) 설정: 스캔 응답을 활용하여 아이폰 Hismith 인식 문제 해결
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    
    // [메인 패킷] 러벤스 정보 송출
    NimBLEAdvertisementData advertData;
    advertData.setName("LVS-Max4");
    advertData.setCompleteServices(NimBLEUUID("53300001-0023-4bd4-bbd5-a6920e4c5653"));
    pAdvertising->setAdvertisementData(advertData);

    // [보조 패킷(Scan Response)] 히스미스 정보 송출
    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setCompleteServices(NimBLEUUID((uint16_t)0x8888));
    pAdvertising->setScanResponseData(scanResponseData);

    pAdvertising->start();
    Serial.println("블루투스 방송 시작! Lovense 앱(비행기모드 해제 가능), Hismith 앱, PC(Xtoys) 연결 준비 완료.");
}

void loop() {
    delay(10);
}
