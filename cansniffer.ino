/**
********************************************************************************
*                       INCLUDES
********************************************************************************
*/

#include <driver/twai.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>

/*
********************************************************************************
*                       USER DEFINED PARAMETERS & TYPEDEF
********************************************************************************
*/

#define CRX_PIN GPIO_NUM_4
#define CTX_PIN GPIO_NUM_5

typedef struct {
    float rpm;
    float speed;
    float throttle;
    float load;
} RawTelemetry;

typedef struct {
    float hp;
    float aggr;
    float zeroToSixty;
} ProcTelemetry;

/*
********************************************************************************
*                       FILE SCOPE VARIABLES & TABLES
********************************************************************************
*/
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

SemaphoreHandle_t rawTelemetryLock;
SemaphoreHandle_t procTelemetryLock;

RawTelemetry rawTelemetry = {0.0f, 0.0f, 0.0f, 0.0f};
ProcTelemetry procTelemetry = {0.0f, 0.0f, 0.0f};

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

/*
********************************************************************************
*                       SETUP & LOOP FUNCTIONS
********************************************************************************
*/

void setup() {
    SPIFFS.begin(true);
    initTWAI();
    initWebServer();

    rawTelemetryLock = xSemaphoreCreateMutex();
    procTelemetryLock = xSemaphoreCreateMutex();

    xTaskCreate(RequestTask, "RequestTask", 2048, NULL, 1, NULL);
    xTaskCreate(ReceiveTask, "ReceiveTask", 2048, NULL, 3, NULL);
    xTaskCreate(ProcessTask, "ProcessTask", 2048, NULL, 1, NULL);
    xTaskCreate(WebsocketTask, "WebsocketTask", 6144, NULL, 2, NULL);
}

void loop() {
    // unused
}

/*
********************************************************************************
*                       INIT FUNCTIONS
********************************************************************************
*/

// Initializing TWAI (CAN library)
void initTWAI()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CTX_PIN, CRX_PIN, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 5;
    g_config.rx_queue_len = 5;
    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();
}

// Initialzing wireless AP with html/css/javascript stored in SPIFFS
void initWebServer() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("OBD_DASHBOARD", "12345678");
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
}

/*
********************************************************************************
*                       TASK FUNCTIONS
********************************************************************************
*/

// task that repeatedly sends PID requests to car for RPM, speed, throttle, load
void RequestTask(void *args) {
    while (1) {
        twai_message_t msg;
        msg = buildPIDRequest(0x0C);    // rpm
        twai_transmit(&msg, pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(75));

        msg = buildPIDRequest(0x0D);    // speed
        twai_transmit(&msg, pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(75));

        msg = buildPIDRequest(0x11);    // throttle
        twai_transmit(&msg, pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(75));

        msg = buildPIDRequest(0x04);    // load
        twai_transmit(&msg, pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(75));
    }
}

// task that blocks until it receives data from OBD-II. Safely updates raw telemetry struct
void ReceiveTask(void *args) {
    while (1) {
        twai_message_t rx_msg;
        if (twai_receive(&rx_msg, portMAX_DELAY) != ESP_OK) continue;
        if (rx_msg.data_length_code < 3) continue;
        if (rx_msg.data[1] != 0x41) continue;

        // updating raw telemetry struct based on pid
        uint8_t pid = rx_msg.data[2];
        xSemaphoreTake(rawTelemetryLock, portMAX_DELAY);
        switch(pid) {
            case 0x0C: {
                rawTelemetry.rpm = ((rx_msg.data[3] * 256) + rx_msg.data[4]) / 4.0;
                break;
            }
            case 0x0D: {
                rawTelemetry.speed = rx_msg.data[3];
                break;
            }
            case 0x11: {
                rawTelemetry.throttle = (rx_msg.data[3] * 100.0f) / 255.0f;
                break;
            }
            case 0x04: {
                rawTelemetry.load = (rx_msg.data[3] * 100.0f) / 255.0f;
                break;
            }
        }
        xSemaphoreGive(rawTelemetryLock);
    }
}

// task that processes raw telemetry data into hp, aggressiveness, and 0-60
void ProcessTask(void *args) {
    while (1) {
        // shallow copy of raw telemetry struct
        xSemaphoreTake(rawTelemetryLock, portMAX_DELAY);
        float rpm = rawTelemetry.rpm;
        float speed = rawTelemetry.speed;
        float throttle = rawTelemetry.throttle;
        float load = rawTelemetry.load;
        xSemaphoreGive(rawTelemetryLock);

        float hp = estimateHPload(rpm, load);
        float aggr = estimateAggr(throttle, load, speed);
        float zeroToSixty = updateZeroToSixty(speed);
        
        xSemaphoreTake(procTelemetryLock, portMAX_DELAY);
        procTelemetry.hp = hp;
        procTelemetry.aggr = aggr;
        procTelemetry.zeroToSixty = zeroToSixty;
        xSemaphoreGive(procTelemetryLock);

        vTaskDelay(100);
    }
}

// task that sends most recent data to websocket to UI
void WebsocketTask(void *args) {
    StaticJsonDocument<256> doc;
    while (1) {
        xSemaphoreTake(rawTelemetryLock, portMAX_DELAY);
        float rpm = rawTelemetry.rpm;
        float speed = rawTelemetry.speed;
        float throttle = rawTelemetry.throttle;
        float load = rawTelemetry.load;
        xSemaphoreGive(rawTelemetryLock);

        xSemaphoreTake(procTelemetryLock, portMAX_DELAY);
        float hp = procTelemetry.hp;
        float agg = procTelemetry.aggr;
        float zeroToSixty = procTelemetry.zeroToSixty;
        xSemaphoreGive(procTelemetryLock);

        doc["r"] = rpm;
        doc["s"] = speed;
        doc["t"] = throttle;
        doc["l"] = load;
        doc["h"] = hp;
        doc["a"] = agg;
        doc["z"] = zeroToSixty;

        // send data as a JSON
        char buffer[512];
        size_t len = serializeJson(doc, buffer);
        buffer[len] = '\0';
        ws.textAll(buffer, len);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*
********************************************************************************
*                       HELPER FUNCTIONS
********************************************************************************
*/

// builds a request for a single PID
twai_message_t buildPIDRequest(uint8_t pid) {
    twai_message_t msg;
    msg.identifier = 0x7DF;
    msg.extd = 0;
    msg.rtr = 0;
    msg.data_length_code = 8;

    msg.data[0] = 0x02;
    msg.data[1] = 0x01;
    msg.data[2] = pid;
    msg.data[3] = 0x00;
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;

    return msg;
}

// estimates the horsepower using load (some formula off the internet)
float estimateHPload(float rpm, float loadPercent) {
    const float MAX_TORQUE = 311.0f;
    float estTorque = (loadPercent / 100.0f) * MAX_TORQUE;
    float hp = (estTorque * rpm) / 5252.0f;
    if (hp > 300) hp = 300;
    if (hp < 0) hp = 0;
    return hp;
}

// determines aggressiveness using weighted sum
float estimateAggr(float thr, float load, float speed) {
    static float prevSpeed = 0.0f;
    static uint32_t prevTime = 0;

    uint32_t now = xTaskGetTickCount();
    if (prevTime == 0) { prevTime = now; prevSpeed = speed; return 0.0f; }

    float dt = (now - prevTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;    
    prevTime = now;

    float accel = (speed - prevSpeed) / dt;  
    prevSpeed = speed;

    float accelNorm = (accel / 10.0f) * 100.0f;
    if (accelNorm < 0) accelNorm = 0;
    if (accelNorm > 100) accelNorm = 100;

    float agg = 0.4f * thr + 0.4f * load + 0.2f * accelNorm;
    return agg > 100 ? 100 : (agg < 0 ? 0 : agg);
}

// detects 0-60 time
float updateZeroToSixty(float speed) {
    static bool running = false;
    static uint32_t startTime = 0;
    static float lastRun = 0.0f;

    uint32_t now = xTaskGetTickCount();

    if (speed < 1.0f) {
        if (!running) {
            return lastRun;
        }
        running = false;
        startTime = 0;
        return lastRun;
    }

    if (!running && speed >= 1.0f && speed < 5.0f) {
        running = true;
        startTime = now;
        return 0.0f;
    }

    if (running) {
        float t = (now - startTime) / 1000.0f;
        if (speed >= 60.0f) {
            running = false;
            lastRun = t;
            return lastRun;
        }
        return t;
    }

    return lastRun;
}

// dummy event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    // do nothing
}
