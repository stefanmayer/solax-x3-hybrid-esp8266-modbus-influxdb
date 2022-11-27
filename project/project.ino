#include <ModbusRTU.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFiMulti.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// Static IP form my RaspberryPI server where the InfluxDB is running
// you can also use a managed solution (e.g. the cloud solution of InfluxDB)
#define MY_INFLUXDB_URL       "http://192.168.1.252:8086"
#define MY_INFLUXDB_BUCKET    "srwr"
#define TZ_INFO               "UTC"
#define DE_RE                 D3
#define LED                   D4
#define DATA_POLL_INTERVAL    1000

InfluxDBClient influxClient(MY_INFLUXDB_URL, MY_INFLUXDB_BUCKET);
SoftwareSerial solaxModbus(D2, D1);

ModbusRTU mb;
ESP8266WebServer server(80);

Point sensor("solax");

enum regType { UINT16, INT16, UINT32, INT32, STRING };

typedef struct
{
  String name;
  int addr;
  int len;
  regType type;
  int myRand;
} modbusRegister;

modbusRegister holdingRegs[] = {
  { "InverterSN",     0x0000, 7, STRING, 0 },
  { "FactoryName",    0x0007, 7, STRING, 0 },
  { "ModuleName",     0x000E, 7, STRING, 0 }
};

modbusRegister inputRegs[] = {
  { "PvVoltage1",                0x0003, 1, UINT16, 0 },
  { "PvCurrent1",                0x0005, 1, UINT16, 0 },
  { "Temperature",               0x0008, 1, UINT16, 0 },
  { "RunMode",                   0x0009, 1, UINT16, 0 },
  { "Powerdc1",                  0x000A, 1, UINT16, 0 },
  { "BatVoltage_Charge1",        0x0014, 1, UINT16, 0 },
  { "BatCurrent_Charge1",        0x0015, 1, UINT16, 0 },
  { "Batpower_Charge1",          0x0016, 1, UINT16, 0 },
  { "BMS_Connect_State",         0x0017, 1, UINT16, 0 },
  { "TemperatureBat",            0x0018, 1, UINT16, 0 },
  { "Battery Capacity",          0x001C, 1, UINT16, 0 },
  { "OutputEnergy_Charge",       0x001D, 2, UINT32, 0 },
  { "OutputEnergy_Charge_today", 0x0020, 1, UINT16, 0 },
  { "InputEnergy_Charge",        0x0021, 2, UINT32, 0 },
  { "InputEnergy_Charge_today",  0x0023, 1, UINT16, 0 },
  { "BMS ChargeMaxCurrent",      0x0024, 1, UINT16, 0 },
  { "BMS DischargeMaxCurrent",   0x0025, 1, UINT16, 0 },
  { "feedin_power",              0x0046, 2, INT32,  0 },
  { "feedin_energy_total",       0x0048, 2, UINT32, 0 },
  { "consum_energy_total",       0x004A, 2, UINT32, 0 },
  { "Etoday_togrid",             0x0050, 1, UINT16, 0 },
  { "Etotal_togrid",             0x0052, 2, UINT32, 0 },
  { "GridVoltage_R",             0x006A, 1, UINT16, 0 },
  { "GridCurrent_R",             0x006B, 1, INT16, 0 },
  { "GridPower_R",               0x006C, 1, INT16, 0 },
  { "GridVoltage_S",             0x006E, 1, UINT16, 0 },
  { "GridCurrent_S",             0x006F, 1, INT16, 0 },
  { "GridPower_S",               0x0070, 1, INT16, 0 },
  { "GridVoltage_T",             0x0072, 1, UINT16, 0 },
  { "GridCurrent_T",             0x0073, 1, INT16, 0 },
  { "GridPower_T",               0x0074, 1, INT16, 0 },
  { "FeedinPower_Rphase",        0x0082, 2, INT32, 0 },
  { "FeedinPower_Sphase",        0x0084, 2, INT32, 0 },
  { "FeedinPower_Tphase",        0x0086, 2, INT32, 0 },
  { "feedin_energy_today",       0x0098, 2, UINT32, 0 },
  { "consum_energy_today",       0x009A, 2, UINT32, 0 }
};



// WLAN-Daten
const char* ssid = "<YOUR_SSID>";
const char* password = "<YOUR_WIFI_PASSWORD>";

bool ledToggle = false;
bool influxDBState = true;
bool modbusState = true;

void getHealth() {
  if (influxDBState && modbusState) {
    server.send(200);
  }
  else if (!modbusState) {
    String response = "{\"error\": \"Unable to fetch data from modbus\"}";
    server.send(503, "text/json", response);
  }
  else if (!influxDBState) {
    String response = "{\"error\": \"InfluxDB write failed\", \"message\":\"";
    response += influxClient.getLastErrorMessage();
    response += "\"}";

    server.send(503, "text/json", response);
  }
}

void restServerRouting() {
  server.on(F("/health"), HTTP_GET, getHealth);
}

bool cb(Modbus::ResultCode event, uint16_t transactionId, void* data) { // Callback to monitor errors
  if (event != Modbus::EX_SUCCESS) {
    Serial.print("Modbus receive error: 0x");
    Serial.println(event, HEX);
    modbusState = false;
  }
  else {
    modbusState = true;
  }
  return true;
}

void setup() {
  pinMode(LED, OUTPUT);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  if (influxClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(influxClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }

  restServerRouting();
  server.begin();
  Serial.println("HTTP server ready: /health");

  solaxModbus.begin(19200, SWSERIAL_8N1);
  mb.begin(&solaxModbus, DE_RE);
  mb.master();
}

void readHoldingRegs() {
  int numHoldingRegs = sizeof(holdingRegs) / sizeof(holdingRegs[0]);
  for (int i = 0; i < numHoldingRegs; ++i) {
    if (!mb.slave()) {

      modbusRegister holdingReg = holdingRegs[i];
      uint16_t res[holdingReg.len];

      Serial.print("Fetching modbus reg: ");
      Serial.print(holdingReg.name);
      Serial.print("=");
      mb.readHreg(1, holdingReg.addr, res, holdingReg.len, cb);
      while (mb.slave()) {
        mb.task();
        delay(25);
      }

      if (modbusState) {
        if (holdingReg.type == STRING) {
          char stringArr[holdingReg.len * 2];
          for (int j = 0; j < holdingReg.len; ++j) {
            stringArr[j * 2 + 1] = res[j] & 0xFF;
            stringArr[j * 2] = (res[j] >> 8) & 0xFF;
          }
          stringArr[holdingReg.len * 2] = '\0';
          sensor.addField(holdingReg.name, stringArr);
          Serial.println(stringArr);
        }
      }
    }
  }
}

void readInputRegs() {
  int numInputRegs = sizeof(inputRegs) / sizeof(inputRegs[0]);
  for (int i = 0; i < numInputRegs; ++i) {
    if (!mb.slave()) {

      modbusRegister inputReg = inputRegs[i];
      uint16_t res[inputReg.len];

      Serial.print("Fetching modbus reg: ");
      Serial.print(inputReg.name);
      Serial.print("=");
      mb.readIreg(1, inputReg.addr, res, inputReg.len, cb);
      while (mb.slave()) {
        mb.task();
        delay(25);
      }

      if (modbusState) {
        if (inputReg.type == STRING) {
          char stringArr[inputReg.len * 2];
          for (int j = 0; j < inputReg.len; ++j) {
            stringArr[j * 2 + 1] = res[j] & 0xFF;
            stringArr[j * 2] = (res[j] >> 8) & 0xFF;
          }
          stringArr[inputReg.len * 2] = '\0';
          sensor.addField(inputReg.name, stringArr);
          Serial.println(stringArr);

        } else if (inputReg.type == UINT16) {
          uint16_t value = res[0];
          if (inputReg.myRand >= 0) {
            value += random(inputReg.myRand / 2, inputReg.myRand);
          }
          sensor.addField(inputReg.name, value);
          Serial.println(value);

        } else if (inputReg.type == INT16) {
          int16_t value = (int16_t)res[0];
          if (inputReg.myRand >= 0) {
            value += random(inputReg.myRand / 2, inputReg.myRand);
          }
          sensor.addField(inputReg.name, value);
          Serial.println(value);

        } else if (inputReg.type == INT32) {
          int32_t value = (int32_t)((res[1] << 16) + res[0]);
          sensor.addField(inputReg.name, value);
          Serial.println(value);

        } else if (inputReg.type == UINT32) {
          uint32_t value = (res[1] << 16) + res[0];
          sensor.addField(inputReg.name, value);
          Serial.println(value);
        }
      }
    }
  }
}

void writeToInfluxDB() {
  Serial.print("Writing: ");
  Serial.println(sensor.toLineProtocol());
  if (!influxClient.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
    influxDBState = false;
  } else {
    influxDBState = true;
  }
}

void loop() {
  // Heartbeat (onboard) LED
  digitalWrite(D4, ledToggle ? HIGH : LOW);
  server.handleClient();
  sensor.clearFields();

  //readHoldingRegs(); <-- not important, just some metadata
  readInputRegs();
  writeToInfluxDB();

  ledToggle = !ledToggle;
  delay(DATA_POLL_INTERVAL);
  yield();
}
