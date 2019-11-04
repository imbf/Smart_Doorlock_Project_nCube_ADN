#include <Arduino.h>
#include <SPI.h>

/*
Copyright (c) 2018, OCEAN
All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <WiFi101.h>
#include <WiFiMDNSResponder.h>


#include "OneM2MClient.h"
#include "m0_ota.h"

const int ledPin = 13; // LED pin for connectivity status indicator

uint8_t USE_WIFI = 1;

#define WINC_CS   8
#define WINC_IRQ  7
#define WINC_RST  4
#define WINC_EN   2

#define WIFI_INIT 1
#define WIFI_CONNECT 2

#define WIFI_CONNECTED 3
#define WIFI_RECONNECT 4
uint8_t WIFI_State = WIFI_INIT;

unsigned long wifi_previousMillis = 0;
const long wifi_interval = 30; // count
const long wifi_led_interval = 100; // ms
uint16_t wifi_wait_count = 0;

unsigned long mqtt_previousMillis = 0;
unsigned long mqtt_interval = 8; // count
const unsigned long mqtt_base_led_interval = 250; // ms
unsigned long mqtt_led_interval = mqtt_base_led_interval; // ms
uint16_t mqtt_wait_count = 0;
unsigned long mqtt_watchdog_count = 0;

// for MQTT
#define _MQTT_INIT 1
#define _MQTT_CONNECT 2
#define _MQTT_CONNECTED 3
#define _MQTT_RECONNECT 4
#define _MQTT_READY 5
#define _MQTT_IDLE 6

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

char mqtt_id[16];
uint8_t MQTT_State = _MQTT_INIT;

char in_message[MQTT_MAX_PACKET_SIZE];
StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;

unsigned long req_previousMillis = 0;
const long req_interval = 15000; // ms

unsigned long chk_previousMillis = 0;
const long chk_interval = 100; // ms
unsigned long chk_count = 0;

#define UPLOAD_UPLOADING 2
#define UPLOAD_UPLOADED 3
unsigned long uploading_previousMillis = 0;
const long uploading_interval = 15000; // ms
uint8_t UPLOAD_State = UPLOAD_UPLOADING;
uint8_t upload_retry_count = 0;

#define NCUBE_REQUESTED 1
#define NCUBE_REQUESTING 2

char req_id[10];
String state = "create_ae";
uint8_t nCube_State = NCUBE_REQUESTED;
uint8_t sequence = 0;

String strRef[8];
int strRef_length = 0;

#define QUEUE_SIZE 8
typedef struct _queue_t {
    uint8_t pop_idx;
    uint8_t push_idx;
    String ref[QUEUE_SIZE];
    String con[QUEUE_SIZE];
    String rqi[QUEUE_SIZE];
    unsigned long* previousMillis[QUEUE_SIZE];
} queue_t;  //queue_t 라는 구조체 생성

queue_t noti_q; //noti_q 구조체 변수 생성
queue_t upload_q; //upload_q 구조체 변수 생성

String body_str = "";

char resp_topic[48];
char noti_topic[48];

unsigned long system_watchdog = 0;

// -----------------------------------------------------------------------------
// User Define
// Period of Sensor Data, can make more
const unsigned long base_generate_interval = 1000; //센서를 탐색하는데 필요한 기본적인 기준 시간

/*
기존 작성된 Period of temp, tvoc, co2 Sensor Data 
unsigned long temp_generate_previousMillis = 0;
unsigned long temp_generate_interval = base_generate_interval;
unsigned long tvoc_generate_previousMillis = 0;
unsigned long tvoc_generate_interval = base_generate_interval;
unsigned long co2_generate_previousMillis = 0;
unsigned long co2_generate_interval = base_generate_interval;
*/
// 새롭게 작성된 Lock Period of Sensor Data
unsigned long lock_generate_previousMillis =0;
unsigned long lock_generate_interval = base_generate_interval;


// Information of CSE as Mobius with MQTT
const String FIRMWARE_VERSION = "1.0.0.0";
String AE_NAME = "LEDGroup";  //AE_NAME을 LEDGroup으로 변경
String AE_ID = "S" + AE_NAME;
const String CSE_ID = "/Mobius2";
const String CB_NAME = "Mobius";
const char* MOBIUS_MQTT_BROKER_IP = "168.131.35.103";  //Mobius 서버 (localhost) ECONOVATION5G
const uint16_t MOBIUS_MQTT_BROKER_PORT = 1883;

OneM2MClient nCube; //oneM2MClient 클래스의 객체인 nCube를 만들어 줄 코드

// LED제어를 위한 PIN번호 설정
int LED1pin = 9;
int LED2pin = 10;
int LED3pin = 11;
int LED4pin = 12;

// 제어상태를 파악하기 위한 LED 개수에 맞춘 번호 설정
int LEDStatus1 = 1; //on:1  off:0
int LEDStatus2 = 2; //on:2  off:0
int LEDStatus3 = 4; //on:4  off:0
int LEDStatus4 = 8; //on:8  off:0

// LED제어를 위한 PINMODE 설정
  
/*
 * 모두 꺼져 있는 경우
 *    모두 OFF : 0(0+0+0+0)
 * 1개만 켜져 있는 경우
 *    1번 on : 1(1+0+0+0)
 *    2번 on : 2(0+2+0+0)
 *    3번 on : 4(0+0+4+0)
 *    4번 on : 8(0+0+0+4)
 * 2개가 켜져 있는 경우
 *    1번, 2번 on : 3(1+2+0+0)
 *    1번, 3번 on : 5(1+0+4+0)
 *    1번, 4번 on : 9(1+0+0+8)
 *    2번, 3번 on : 6(0+2+4+0)
 *    2번, 4번 on : 10(0+2+0+8)
 *    3번, 4번 on : 12(0+0+4+8)
 * 3개가 켜져 있는 경우
 *    1번, 2번, 3번 on : 7(1+2+4+0)
 *    1번, 2번, 4번 on : 11(1+2+0+8)
 *    1번, 3번, 4번 on : 13(1+0+4+8)
 *    2번, 3번, 4번 on : 14(0+2+4+8)
 * 4개가 켜져 있는 경우
 *    1번, 2번, 3번, 4번 on : 15(1+2+4+8)
 */

// setup() 함수를 위한 LEDSetup() 함수 작성
void LEDSetup(){
  pinMode(LED1pin, OUTPUT);
  pinMode(LED2pin, OUTPUT);
  pinMode(LED3pin, OUTPUT);
  pinMode(LED4pin, OUTPUT);

  digitalWrite(LED1pin, HIGH);
  digitalWrite(LED2pin, HIGH);
  digitalWrite(LED3pin, HIGH);
  digitalWrite(LED4pin, HIGH);
}

// add TAS(Thing Adaptation Layer) for Sensor 

/* Not use TAS of Sensor(LED,CSS811, Servomotor)
#include "ServoRotation.h"
ServoRotation servoRotation;
#include "TasLED.h"
TasLED tasLed;
#include "TasCCS811.h"
TasCCS811 TasCCSSensor;
*/

// build tree of resource of oneM2M
//Group 제어를 위해선 매우 중요하다 Resource를 Build하는게 가장 중요하다 !!! (잘못 빌드시 API 통신을 하지 못하는 경우도 발생)
void buildResource() {
    nCube.configResource(2, "/"+CB_NAME, AE_NAME); //AE resource

    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "update"); // Default Container resource
    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "LEDGroup");  // LEDGroup Container resource

    nCube.configResource(23, "/"+CB_NAME+"/"+AE_NAME+"/update", "sub"); // Subscription resource
    nCube.configResource(23, "/"+CB_NAME+"/"+AE_NAME+"/LEDGroup", "sub");  // Subscription resource for LEDGroup
    /*
    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "led"); // LED  Container resource
    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "temp"); // 온도 Container resource
    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "tvoc"); // 대기의 질 Container resource
    nCube.configResource(3, "/"+CB_NAME+"/"+AE_NAME, "lock"); // Servo Container resourec
    */
}

// Period of generating ServoRotation data
void LEDGroupGenProcess() {
    /*
     * 감지 시간을 Random하게 맞추기 위한 프로그램 시작 코드를 이용한 분기처리 구문.
    */ 
    unsigned long currentMillis = millis(); 
    // (unsinged long) millis() 메서드 :  프로그램이 시작한 시간 return 
    
    if (currentMillis - lock_generate_previousMillis >= lock_generate_interval) {
        lock_generate_previousMillis = currentMillis;
        lock_generate_interval = base_generate_interval;
                                    //random(max) : 0~999사이 임의의 값 return
    
        if(digitalRead(LED1pin)==1)
          LEDStatus1=1;
        else
          LEDStatus1=0;
        if(digitalRead(LED2pin)==1)
          LEDStatus2=2;
        else
          LEDStatus2=0;
        if(digitalRead(LED3pin)==1)
          LEDStatus3=4;
        else
          LEDStatus3=0;
        if(digitalRead(LED4pin)==1)
          LEDStatus4=8;
        else
          LEDStatus4=0;
        Serial.print("LED1Pin :");
        Serial.println(digitalRead(LED1pin));
        Serial.print("LED2Pin :");
        Serial.println(digitalRead(LED2pin));
        Serial.print("LED3Pin :");
        Serial.println(digitalRead(LED3pin));
        Serial.print("LED4Pin :");
        Serial.println(digitalRead(LED4pin));
        Serial.println(LED4pin);
        if (state == "create_cin") {
            String cnt = "LEDGroup";
            String con = "\"?\"";
            con = LEDStatus1 + LEDStatus2 + LEDStatus3 + LEDStatus4; //con : 실제 Content, 즉, ContentInstance를 의미한다.
            con = "\"" + con + "\"";

            char rqi[10];
            rand_str(rqi, 8);
            upload_q.ref[upload_q.push_idx] = "/"+CB_NAME+"/"+AE_NAME+"/"+cnt;
            upload_q.con[upload_q.push_idx] = con;
            upload_q.rqi[upload_q.push_idx] = String(rqi);
            upload_q.push_idx++;
            if(upload_q.push_idx >= QUEUE_SIZE) {
              upload_q.push_idx = 0;
              }
            if(upload_q.push_idx == upload_q.pop_idx) {
              upload_q.pop_idx++;
              if(upload_q.pop_idx >= QUEUE_SIZE) {
                upload_q.pop_idx = 0;
                }
              }
              
              Serial.println("pop : " + String(upload_q.pop_idx));
              Serial.println("push : " + String(upload_q.push_idx));    
        }         
    }
}

              /*
                if(!TasCCSSensor.readData()) {  //TASCCSSensor에 오류를 탐지
                    con = String(TasCCSSensor.geteCO2()/10);
                    con = "\"" + con + "\"";

                    char rqi[10];
                    rand_str(rqi, 8);
                    upload_q.ref[upload_q.push_idx] = "/"+CB_NAME+"/"+AE_NAME+"/"+cnt;
                    upload_q.con[upload_q.push_idx] = con;
                    upload_q.rqi[upload_q.push_idx] = String(rqi);
                    upload_q.push_idx++;
                    if(upload_q.push_idx >= QUEUE_SIZE) {
                        upload_q.push_idx = 0;
                    }
                    if(upload_q.push_idx == upload_q.pop_idx) {
                        upload_q.pop_idx++;
                        if(upload_q.pop_idx >= QUEUE_SIZE) {
                            upload_q.pop_idx = 0;
                        }
                    }

                    Serial.println("pop : " + String(upload_q.pop_idx));
                    Serial.println("push : " + String(upload_q.push_idx));
                }
                */
            
/*
void tempGenProcess() {
    unsigned long currentMillis = millis();
    if (currentMillis - temp_generate_previousMillis >= temp_generate_interval) {
        temp_generate_previousMillis = currentMillis;
        temp_generate_interval = base_generate_interval + (random(1000));
                               
                               
        if (state == "create_cin") {
            String cnt = "temp";
            String con = "\"?\"";

            if(TasCCSSensor.available()) {
                float temp = TasCCSSensor.calculateTemperature();
                con = String(temp);
                con = "\"" + con + "\"";

                char rqi[10];
                rand_str(rqi, 8);
                upload_q.ref[upload_q.push_idx] = "/"+CB_NAME+"/"+AE_NAME+"/"+cnt;
                upload_q.con[upload_q.push_idx] = con;
                upload_q.rqi[upload_q.push_idx] = String(rqi);
                upload_q.push_idx++;
                if(upload_q.push_idx >= QUEUE_SIZE) {
                    upload_q.push_idx = 0;
                }
                if(upload_q.push_idx == upload_q.pop_idx) {
                    upload_q.pop_idx++;
                    if(upload_q.pop_idx >= QUEUE_SIZE) {
                        upload_q.pop_idx = 0;
                    }
                }

                Serial.println("pop : " + String(upload_q.pop_idx));
                Serial.println("push : " + String(upload_q.push_idx));
            }
        }
    }
}

void tvocGenProcess() {
    unsigned long currentMillis = millis();
    if (currentMillis - tvoc_generate_previousMillis >= tvoc_generate_interval) {
        tvoc_generate_previousMillis = currentMillis;
        tvoc_generate_interval = base_generate_interval + (random(1000));
                                   
                                      
        if (state == "create_cin") {
            String cnt = "tvoc";
            String con = "\"?\"";

            if(TasCCSSensor.available()) {
                if(!TasCCSSensor.readData()) {
                    con = String(TasCCSSensor.getTVOC()/10);
                    con = "\"" + con + "\"";

                    char rqi[10];
                    rand_str(rqi, 8);
                    upload_q.ref[upload_q.push_idx] = "/"+CB_NAME+"/"+AE_NAME+"/"+cnt;
                    upload_q.con[upload_q.push_idx] = con;
                    upload_q.rqi[upload_q.push_idx] = String(rqi);
                    upload_q.push_idx++;
                    if(upload_q.push_idx >= QUEUE_SIZE) {
                        upload_q.push_idx = 0;
                    }
                    if(upload_q.push_idx == upload_q.pop_idx) {
                        upload_q.pop_idx++;
                        if(upload_q.pop_idx >= QUEUE_SIZE) {
                            upload_q.pop_idx = 0;
                        }
                    }

                    Serial.println("pop : " + String(upload_q.pop_idx));
                    Serial.println("push : " + String(upload_q.push_idx));
                }
                else {
                    Serial.println("CCS811 tvoc ERROR!");
                }
            }
        }
    }
}
*/
// Process notification of Mobius for control
void notiProcess() {
    
    if(noti_q.pop_idx != noti_q.push_idx) {
        Split(noti_q.ref[noti_q.pop_idx], '/');
        Serial.println("strRef_length : " + String(strRef_length) ); 
        for(int i=0;i<strRef_length;i++){
          Serial.println(strRef[i]);
        }
        if(strRef[strRef_length-1] == "LEDGroup") { // led -> lock 수정
          // tasLed.setLED(noti_q.con[noti_q.pop_idx]); //tasLed.setLED(인자) 함수 비활성화
            
            String resp_body = "";
            resp_body += "{\"rsc\":\"2000\",\"to\":\"\",\"fr\":\"" + nCube.getAeid() + "\",\"pc\":\"\",\"rqi\":\"" + noti_q.rqi[noti_q.pop_idx] + "\"}";
            nCube.response(mqtt, resp_body);
            Serial.println("2000 ---->");
        }
        else if(strRef[strRef_length-1] == "update") {
          //servoRotation.temporaryOpenDoor(); Servo를 돌리는 함수.
          //HTTP통신(REST API를 통해) 했을 때 분기처리 해줘야 하는 부분 !!!!
          Serial.print("request con : ");
          Serial.println(noti_q.con[noti_q.pop_idx]);  // request의 con에 들어 있는 String을 가리킨다.
          if(noti_q.con[noti_q.pop_idx].toInt()<=15){ 
            uint32_t pin[4]= {0};
            for(int i=8,index=3;i!=0;i=i/2){
              if((noti_q.con[noti_q.pop_idx].toInt())/i == 1){
                pin[index--] = HIGH;
              } else {
                pin[index--] = LOW;
              }
              noti_q.con[noti_q.pop_idx] = noti_q.con[noti_q.pop_idx].toInt()%i;
            }
            for(int i=0;i<4;i++){
              Serial.println(pin[i]);
            }
              digitalWrite(LED1pin, pin[0]);
              digitalWrite(LED2pin, pin[1]);
              digitalWrite(LED3pin, pin[2]);
              digitalWrite(LED4pin, pin[3]);
          }

          //API 통신에 의한 분기처리 종료 부분
          
            if (noti_q.con[noti_q.pop_idx] == "active") {
                OTAClient.start();   // active OTAClient upgrad process
                
                String resp_body = "";
                resp_body += "{\"rsc\":\"2000\",\"to\":\"\",\"fr\":\"" + nCube.getAeid() + "\",\"pc\":\"\",\"rqi\":\"" + noti_q.rqi[noti_q.pop_idx] + "\"}";
                nCube.response(mqtt, resp_body);
            }
        }
        
        noti_q.pop_idx++;
        if(noti_q.pop_idx >= QUEUE_SIZE) {
            noti_q.pop_idx = 0;
        }
    }
}
//------------------------------------------------------------------------------

void setup() {
    // configure the LED pin for output mode
    pinMode(ledPin, OUTPUT);

    //Initialize serial:
    Serial.begin(9600);
    //while(!Serial);

    noti_q.pop_idx = 0;
    noti_q.push_idx = 0;
    upload_q.pop_idx = 0;
    upload_q.push_idx = 0;

    WiFi_init();

    delay(1000);

    byte mac[6];
    WiFi.macAddress(mac);
    sprintf(mqtt_id, "nCube-%.2X%.2X", mac[1], mac[0]);
    unsigned long seed = mac[0] + mac[1];
    randomSeed(seed);

    delay(500);

    // User Defined setup -------------------------------------------------------
    LEDSetup();

/*
    if(!TasCCSSensor.begin()) {
        Serial.println("Failed to start CCS811 sensor! Please check your wiring.");
        while(1) {
            delay(100);
            tasLed.setLED(String(random(1, 8)));
        }
    }
*/

    //calibrate temperature sensor  온도 센서 보정
    /*
    while(!TasCCSSensor.available());
    float temp = TasCCSSensor.calculateTemperature();
    TasCCSSensor.setTempOffset(temp - 25.0);
    */
    
    lock_generate_interval = base_generate_interval + (random(10)*1000);
    
    /*
    co2_generate_interval = base_generate_interval + (random(10)*1000);
    tvoc_generate_interval = base_generate_interval + (random(10)*1000);
    temp_generate_interval = base_generate_interval + (random(10)*1000);
    */
    delay(500);
    //tasLed.setLED("0");
    //--------------------------------------------------------------------------

    delay(500);

    String topic = "/oneM2M/resp/" + AE_ID + CSE_ID + "/json";
    topic.toCharArray(resp_topic, 64);

    topic = "/oneM2M/req" + CSE_ID + "/" + AE_ID + "/json";
    topic.toCharArray(noti_topic, 64);

    nCube.Init(CSE_ID, MOBIUS_MQTT_BROKER_IP, AE_ID);
    mqtt.setServer(MOBIUS_MQTT_BROKER_IP, MOBIUS_MQTT_BROKER_PORT);
    mqtt.setCallback(mqtt_message_handler);
    MQTT_State = _MQTT_INIT;

    buildResource();
    rand_str(req_id, 8);
    nCube_State = NCUBE_REQUESTED;

    //init OTA client
    OTAClient.begin(AE_NAME, FIRMWARE_VERSION);
}

void loop() {
    // nCube loop
    nCube_loop();

    // user defined loop
    notiProcess();
    LEDGroupGenProcess();//1초마다 LEDGroup의 상태를 탐지하는 function
    
    /*
    co2GenProcess();
    tempGenProcess();
    tvocGenProcess();
    */
}

//------------------------------------------------------------------------------
// nCube core functions
//------------------------------------------------------------------------------

void nCube_loop() {
    WiFi_chkconnect();
    if (!mqtt.loop()) {
        MQTT_State = _MQTT_CONNECT;
        //digitalWrite(13, HIGH);
        mqtt_reconnect();
        //digitalWrite(13, LOW);
    }
    else {
        MQTT_State = _MQTT_CONNECTED;
    }

    chkState();
    publisher();
    otaProcess();
    uploadProcess();
}

void rand_str(char *dest, size_t length) {
    char charset[] = "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        //size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        //*dest++ = charset[index];

        size_t index = random(62);
        *dest++ = charset[index];
    }
    *dest = '\0';
}

void WiFi_init() {
    if(USE_WIFI) {
        // begin WiFi
        digitalWrite(ledPin, HIGH);
        WiFi.setPins(WINC_CS, WINC_IRQ, WINC_RST, WINC_EN);
        if (WiFi.status() == WL_NO_SHIELD) { // check for the presence of the shield:
            Serial.println("WiFi shield not present");
            // don't continue:
            while (true) {
                digitalWrite(ledPin, HIGH);
                delay(100);
                digitalWrite(ledPin, LOW);
                delay(100);
            }
        }
        digitalWrite(ledPin, LOW);
    }

    WIFI_State = WIFI_INIT;
}

void WiFi_chkconnect() {
    if(USE_WIFI) {
        if(WIFI_State == WIFI_INIT) {
            digitalWrite(ledPin, HIGH);

            Serial.println("beginProvision - WIFI_INIT");
            WiFi.beginProvision();
//            WiFi.begin("FILab", "badacafe00");

            WIFI_State = WIFI_CONNECT;
            wifi_previousMillis = 0;
            wifi_wait_count = 0;
            noti_q.pop_idx = 0;
            noti_q.push_idx = 0;
            upload_q.pop_idx = 0;
            upload_q.push_idx = 0;
        }
        else if(WIFI_State == WIFI_CONNECTED) {
            if (WiFi.status() == WL_CONNECTED) {
                return;
            }

            wifi_wait_count = 0;
            if(WIFI_State == WIFI_CONNECTED) {
                WIFI_State = WIFI_RECONNECT;
                wifi_previousMillis = 0;
                wifi_wait_count = 0;
                noti_q.pop_idx = 0;
                noti_q.push_idx = 0;
                upload_q.pop_idx = 0;
                upload_q.push_idx = 0;
            }
            else {
                WIFI_State = WIFI_CONNECT;
                wifi_previousMillis = 0;
                wifi_wait_count = 0;
                noti_q.pop_idx = 0;
                noti_q.push_idx = 0;
                upload_q.pop_idx = 0;
                upload_q.push_idx = 0;
            }
            //nCube.MQTT_init(AE_ID);
        }
        else if(WIFI_State == WIFI_CONNECT) {
            unsigned long currentMillis = millis();
            if (currentMillis - wifi_previousMillis >= wifi_led_interval) {
                wifi_previousMillis = currentMillis;
                if(wifi_wait_count++ >= wifi_interval) {
                    wifi_wait_count = 0;
                    if (WiFi.status() != WL_CONNECTED) {
                        Serial.println("Provisioning......");
                    }
                }
                else {
                    if(wifi_wait_count % 2) {
                        digitalWrite(ledPin, HIGH);
                    }
                    else {
                        digitalWrite(ledPin, LOW);
                    }
                }
            }
            else {
                if (WiFi.status() == WL_CONNECTED) {
                    // you're connected now, so print out the status:
                    printWiFiStatus();

                    digitalWrite(ledPin, LOW);

                    WIFI_State = WIFI_CONNECTED;
                    wifi_previousMillis = 0;
                    wifi_wait_count = 0;
                    noti_q.pop_idx = 0;
                    noti_q.push_idx = 0;
                    upload_q.pop_idx = 0;
                    upload_q.push_idx = 0;
                }
            }
        }
        else if(WIFI_State == WIFI_RECONNECT) {
            digitalWrite(ledPin, HIGH);

            unsigned long currentMillis = millis();
            if (currentMillis - wifi_previousMillis >= wifi_led_interval) {
                wifi_previousMillis = currentMillis;
                if(wifi_wait_count++ >= wifi_interval) {
                    wifi_wait_count = 0;
                    if (WiFi.status() != WL_CONNECTED) {
                        Serial.print("Attempting to connect to SSID: ");
                        Serial.println("previous SSID");

                        WiFi.begin();
                    }
                }
                else {
                    if(wifi_wait_count % 2) {
                        digitalWrite(ledPin, HIGH);
                    }
                    else {
                        digitalWrite(ledPin, LOW);
                    }
                }
            }
            else {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("Connected to wifi");
                    printWiFiStatus();

                    digitalWrite(ledPin, LOW);

                    WIFI_State = WIFI_CONNECTED;
                    wifi_previousMillis = 0;
                    wifi_wait_count = 0;
                }
            }
        }
    }
}

void printWiFiStatus() {
    // print the SSID of the network you're attached to:
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print your WiFi shield's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
}

void otaProcess() {
    if(WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_CONNECTED) {
        if (OTAClient.finished()) {
        }
        else {
            OTAClient.poll();
        }
    }
}

void publisher() {
    unsigned long currentMillis = millis();
    if (currentMillis - req_previousMillis >= req_interval) {
        req_previousMillis = currentMillis;

//        rand_str(req_id, 8);
        nCube_State = NCUBE_REQUESTED;
    }

    if(nCube_State == NCUBE_REQUESTED && WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_CONNECTED) {
        if (state == "create_ae") {
            Serial.print(state + " - ");
            Serial.print(String(sequence));
            Serial.print(" - ");
            Serial.println(String(req_id));
            nCube_State = NCUBE_REQUESTING;
            body_str = nCube.createAE(mqtt, req_id, 0, "3.14");

            if (body_str == "0") {
            Serial.println(F("REQUEST Failed"));
          }
          else {
            Serial.print("Request [");
            Serial.print(nCube.getReqTopic());
            Serial.print("] ----> ");
            Serial.println(body_str.length()+1);
            Serial.println(body_str);
          }
            //digitalWrite(ledPin, HIGH);
        }
        else if (state == "create_cnt") {
            Serial.print(state + " - ");
            Serial.print(String(sequence));
            Serial.print(" - ");
            Serial.println(String(req_id));
            nCube_State = NCUBE_REQUESTING;
            body_str = nCube.createCnt(mqtt, req_id, sequence);
            if (body_str == "0") {
            Serial.println(F("REQUEST Failed"));
          }
          else {
            Serial.print("Request [");
            Serial.print(nCube.getReqTopic());
            Serial.print("] ----> ");
            Serial.println(body_str.length()+1);
            Serial.println(body_str);
          }
            //digitalWrite(ledPin, HIGH);
        }
        else if (state == "delete_sub") {
            Serial.print(state + " - ");
            Serial.print(String(sequence));
            Serial.print(" - ");
            Serial.println(String(req_id));
            nCube_State = NCUBE_REQUESTING;
            body_str = nCube.deleteSub(mqtt, req_id, sequence);
            if (body_str == "0") {
            Serial.println(F("REQUEST Failed"));
          }
          else {
            Serial.print("Request [");
            Serial.print(nCube.getReqTopic());
            Serial.print("] ----> ");
            Serial.println(body_str.length()+1);
            Serial.println(body_str);
          }
            //digitalWrite(ledPin, HIGH);
        }
        else if (state == "create_sub") {
            Serial.print(state + " - ");
            Serial.print(String(sequence));
            Serial.print(" - ");
            Serial.println(String(req_id));
            nCube_State = NCUBE_REQUESTING;
            body_str = nCube.createSub(mqtt, req_id, sequence);
            if (body_str == "0") {
            Serial.println(F("REQUEST Failed"));
          }
          else {
            Serial.print("Request [");
            Serial.print(nCube.getReqTopic());
            Serial.print("] ----> ");
            Serial.println(body_str.length()+1);
            Serial.println(body_str);
          }
            //digitalWrite(ledPin, HIGH);
        }
        else if (state == "create_cin") {
        }
    }
}

unsigned long mqtt_sequence = 0;
void chkState() {
    unsigned long currentMillis = millis();
    if (currentMillis - chk_previousMillis >= chk_interval) {
        chk_previousMillis = currentMillis;

        system_watchdog++;
        if(system_watchdog > 9000) {
            if(system_watchdog % 2) {
                digitalWrite(ledPin, HIGH);
            }
            else {
                digitalWrite(ledPin, LOW);
            }
        }
        else if(system_watchdog > 18000) {
            NVIC_SystemReset();
        }

        if(WIFI_State == WIFI_CONNECT) {
            Serial.println("WIFI_CONNECT");
            MQTT_State = _MQTT_INIT;
        }
        else if(WIFI_State == WIFI_RECONNECT) {
            Serial.println("WIFI_RECONNECT");
            MQTT_State = _MQTT_INIT;
        }
        else if(WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_INIT) {
            MQTT_State = _MQTT_CONNECT;
        }

        if(MQTT_State == _MQTT_CONNECT) {
            Serial.println("_MQTT_CONNECT");
        }

        /*if(WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_CONNECTED) {
            chk_count++;
            if(chk_count >= 100) {
                chk_count = 0;

                //noInterrupts();
                body_str = nCube.heartbeat(mqtt);
                // char seq[10];
                // sprintf(seq, "%ld", ++mqtt_sequence);
                // mqtt.publish("/nCube/count/test", seq, strlen(seq));
                // Serial.println(String(mqtt_sequence));
                //interrupts();

                if (body_str == "Failed") {
                    Serial.println(F("Heartbeat Failed"));
                }
                else {
                    Serial.print("Send heartbeat [");
                    Serial.print(nCube.getHeartbeatTopic());
                    Serial.print("] ----> ");
                    Serial.println(body_str.length()+1);
                    Serial.println(body_str);
                    system_watchdog = 0;
                }
            }
        }*/
    }
}

void Split(String sData, char cSeparator)
{
    int nCount = 0;
    int nGetIndex = 0 ;
    strRef_length = 0;

    String sTemp = "";
    String sCopy = sData;

    while(true) {
        nGetIndex = sCopy.indexOf(cSeparator);

        if(-1 != nGetIndex) {
            sTemp = sCopy.substring(0, nGetIndex);
            strRef[strRef_length++] = sTemp;
            sCopy = sCopy.substring(nGetIndex + 1);
        }
        else {
            strRef[strRef_length++] = sCopy;
            break;
        }
        ++nCount;
    }
}

void mqtt_reconnect() {
    if(WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_CONNECT) {
        unsigned long currentMillis = millis();
        if (currentMillis - mqtt_previousMillis >= mqtt_led_interval) {
            mqtt_led_interval = mqtt_base_led_interval + random(mqtt_base_led_interval);
            mqtt_previousMillis = currentMillis;
            if(mqtt_wait_count++ >= (mqtt_interval)) {
                mqtt_wait_count = 0;

                Serial.print("Attempting MQTT connection...");
                // Attempt to connect
                //rand_str(mqtt_id, 8);
                if (mqtt.connect(mqtt_id)) {
                    mqtt_watchdog_count = 0;
                    Serial.println("connected");

                    if (mqtt.subscribe(resp_topic)) {
                        Serial.println(String(resp_topic) + " Successfully subscribed");
                    }

                    if (mqtt.subscribe(noti_topic)) {
                        Serial.println(String(noti_topic) + " Successfully subscribed");
                    }

                    MQTT_State = _MQTT_CONNECTED;
                    nCube.reset_heartbeat();
                }
                else {
                    Serial.print("failed, rc=");
                    Serial.print(mqtt.state());
                    Serial.println(" try again in 2 seconds");
                    mqtt_watchdog_count++;
                    if(mqtt_watchdog_count > 10) {
                        NVIC_SystemReset();
                    }
                }
            }
            else {
                if(mqtt_wait_count % 2) {
                    digitalWrite(ledPin, HIGH);
                }
                else {
                    digitalWrite(ledPin, LOW);
                }
            }
        }
    }
}

void mqtt_message_handler(char* topic_in, byte* payload, unsigned int length) {
    String topic = String(topic_in);

    // Serial.print("Message arrived [");
    // Serial.print(topic);
    // Serial.print("] <---- ");
    // Serial.println(length);
    //
    // for (unsigned int i = 0; i < length; i++) {
    //     Serial.print((char)payload[i]);
    // }
    // Serial.println();

    //noInterrupts();

    if (topic.substring(8,12) == "resp") {
        memset((char*)in_message, '\0', length+1);
        memcpy((char*)in_message, payload, length);
        JsonObject& resp_root = jsonBuffer.parseObject(in_message);

        if (!resp_root.success()) {
            Serial.println(F("parseObject() failed"));
            return;
        }

        wifiClient.flush();

        resp_handler(resp_root["rsc"], resp_root["rqi"]);

        jsonBuffer.clear();
    }
    else if(topic.substring(8,11) == "req") {
        memset((char*)in_message, '\0', length+1);
        memcpy((char*)in_message, payload, length);
        JsonObject& req_root = jsonBuffer.parseObject(in_message);

        if (!req_root.success()) {
            Serial.println(F("parseObject() failed"));
            return;
        }

        wifiClient.flush();

        noti_handler(req_root["pc"]["m2m:sgn"]["sur"], req_root["rqi"], req_root["pc"]["m2m:sgn"]["nev"]["rep"]["m2m:cin"]["con"]);

        jsonBuffer.clear();
    }
    //interrupts();
    system_watchdog = 0;
}

void noti_handler(String sur, String rqi, String con) {
    if (state == "create_cin") {
        Serial.print("<---- ");
        if(sur.charAt(0) != '/') {    //sur : Mobius/lock/update/sub
            sur = '/' + sur;
            Serial.println(sur); 
        }
        else {
            Serial.println(sur);
        }

        String valid_sur = nCube.validSur(sur);
        if (valid_sur != "empty") {
            noti_q.ref[noti_q.push_idx] = valid_sur;
            noti_q.con[noti_q.push_idx] = con;
            noti_q.rqi[noti_q.push_idx] = rqi;
            noti_q.push_idx++;
            if(noti_q.push_idx >= QUEUE_SIZE) {
                noti_q.push_idx = 0;
            }
            if(noti_q.push_idx == noti_q.pop_idx) {
                noti_q.pop_idx++;
                if(noti_q.pop_idx >= QUEUE_SIZE) {
                    noti_q.pop_idx = 0;
                }
            }
        }
    }
}

void resp_handler(int response_code, String response_id) {
    String request_id = String(req_id);

    if (request_id == response_id) {
        if (response_code == 2000 || response_code == 2001 || response_code == 2002 || response_code == 4105 || response_code == 4004) {
            Serial.print("<---- ");
            Serial.println(response_code); ///Mobius/lock/lock/sub
            if (state == "create_ae") {
                sequence++;
                if(sequence >= nCube.getAeCount()) {
                    state = "create_cnt";
                    sequence = 0;
                }
                rand_str(req_id, 8);
                nCube_State = NCUBE_REQUESTED;
            }
            else if (state == "create_cnt") {
                sequence++;
                if(sequence >= nCube.getCntCount()) {
                    state = "delete_sub";
                    sequence = 0;
                }
                rand_str(req_id, 8);
                nCube_State = NCUBE_REQUESTED;
            }
            else if(state == "delete_sub") {
                sequence++;
                if(sequence >= nCube.getSubCount()) {
                    state = "create_sub";
                    sequence = 0;
                }
                rand_str(req_id, 8);
                nCube_State = NCUBE_REQUESTED;
            }
            else if (state == "create_sub") {
                sequence++;
                if(sequence >= nCube.getSubCount()) {
                    state = "create_cin";
                    sequence = 0;
                }
                rand_str(req_id, 8);
                nCube_State = NCUBE_REQUESTED;
            }
            else if (state == "create_cin") {
                upload_retry_count = 0;
                if(upload_q.pop_idx == upload_q.push_idx) {

                }
                else {
                    *upload_q.previousMillis[upload_q.pop_idx] = millis();

                    upload_q.pop_idx++;
                    if(upload_q.pop_idx >= QUEUE_SIZE) {
                        upload_q.pop_idx = 0;
                    }
                }
            }
            //digitalWrite(ledPin, LOW);
            UPLOAD_State = UPLOAD_UPLOADED;
        }
    }
}

void uploadProcess() {
    if(WIFI_State == WIFI_CONNECTED && MQTT_State == _MQTT_CONNECTED) {
        unsigned long currentMillis = millis();
        if (currentMillis - uploading_previousMillis >= uploading_interval) {
            uploading_previousMillis = currentMillis;

            if (state == "create_cin") {
                if(UPLOAD_State == UPLOAD_UPLOADING) {
                    Serial.println("upload timeout");
                }

                UPLOAD_State = UPLOAD_UPLOADED;
                upload_retry_count++;
                if(upload_retry_count > 2) {
                    upload_retry_count = 0;
                    if(upload_q.pop_idx == upload_q.push_idx) {

                    }
                    else {
                        upload_q.pop_idx++;
                        if(upload_q.pop_idx >= QUEUE_SIZE) {
                            upload_q.pop_idx = 0;
                        }
                    }
                }
            }
        }

        if((UPLOAD_State == UPLOAD_UPLOADED) && (upload_q.pop_idx != upload_q.push_idx)) {
            if (wifiClient.available()) {
                return;
            }

            uploading_previousMillis = millis();
            UPLOAD_State = UPLOAD_UPLOADING;

            upload_q.rqi[upload_q.pop_idx].toCharArray(req_id, 10);

            Serial.println("pop : " + String(upload_q.pop_idx));
            Serial.println("push : " + String(upload_q.push_idx));
            //noInterrupts();
            body_str = nCube.createCin(mqtt, upload_q.rqi[upload_q.pop_idx], upload_q.ref[upload_q.pop_idx], upload_q.con[upload_q.pop_idx]);
            wifiClient.flush();
            //interrupts();
            if (body_str == "0") {
            Serial.println(F("REQUEST Failed"));
          }
          else {
                system_watchdog = 0;
            Serial.print("Request [");
            Serial.print(nCube.getReqTopic());
            Serial.print("] ----> ");
            Serial.println(body_str.length()+1);
            Serial.println(body_str);
          }
            //digitalWrite(ledPin, HIGH);
        }
    }
}