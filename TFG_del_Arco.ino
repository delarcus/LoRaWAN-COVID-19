#include <arduino_lmic.h>
#include <arduino_lmic_hal_boards.h>
#include <arduino_lmic_hal_configuration.h>
#include <arduino_lmic_lorawan_compliance.h>
#include <arduino_lmic_user_configuration.h>


#include <lmic.h>
#include <SPI.h>

#include <MHZ19.h>
#include <SoftwareSerial.h>

#include "heltec.h"
#include "DHT.h"


#define DHTTYPE DHT22
#define DHTPIN 13

#define RXPin 22 // para MH-Z19B
#define TXPin 21 // para MH-Z19B

unsigned long getDataTimer;
unsigned long counter;
unsigned long paquete;

float h; // 4 bytes
float t;
int CO2; // 4 bytes
int val_CO2 = 0; // valor CO2

SoftwareSerial ss(RXPin, TXPin);// RX, TX
MHZ19 mhz(&ss);
DHT dht(DHTPIN, DHTTYPE);

// lsb (little-endian)
static const u1_t PROGMEM DEVEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevEui (u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}


// lsb (little-endian)
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}


// msb (big-endian)
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevKey (u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

static uint8_t payload[8];
static osjob_t sendjob;

const unsigned TX_INTERVAL = 60;

const lmic_pinmap lmic_pins = {
  .nss = 18,                     
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 14,                     
  .dio = {/*dio0*/ 26, /*dio1*/ 35, /*dio2*/ 33} 
};


void printHex2(unsigned v) {
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}


void onEvent (ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      {
        u4_t netid = 0;
        devaddr_t devaddr = 0;
        u1_t nwkKey[16];
        u1_t artKey[16];
        LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
        Serial.print("netid: ");
        Serial.println(netid, DEC);
        Serial.print("devaddr: ");
        Serial.println(devaddr, HEX);
        Serial.print("AppSKey: ");
        for (size_t i = 0; i < sizeof(artKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(artKey[i]);
        }
        Serial.println("");
        Serial.print("NwkSKey: ");
        for (size_t i = 0; i < sizeof(nwkKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(nwkKey[i]);
        }
        Serial.println();
      }
      LMIC_setLinkCheckMode(0);
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("Received ack"));
      if (LMIC.dataLen) {
        Serial.print(F("Received "));
        Serial.print(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
      }



      // Schedule next transmission
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      Serial.println("\n PAQUETE ENVIADO ");
      paquete++;



      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    case EV_TXSTART:
      Serial.println(F("EV_TXSTART"));
      break;
    case EV_TXCANCELED:
      Serial.println(F("EV_TXCANCELED"));
      break;
    case EV_RXSTART:
      /* do not print anything -- it wrecks timing */
      break;
    case EV_JOIN_TXCOMPLETE:
      Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
      break;

    default:
      Serial.print(F("Unknown event: "));
      Serial.println((unsigned) ev);
      break;
  }
}

void do_send(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {

    // CO2 //
    CO2 = mhz.getCO2();
    Serial.print("Co2 ");
    Serial.println(CO2);

    // TEMPERATURE //
    t = dht.readTemperature();
    Serial.print("Temperatura: ");
    Serial.println(t);

    // HUMIDITY //
    h = dht.readHumidity();
    Serial.print("Humedad: ");
    Serial.println(h);

    uint16_t ft;
    uint16_t fh;
    uint32_t fco2;

    
    if (isnan(h) || isnan(t) || (mhz.retrieveData() != MHZ19_RESULT_OK)) {
      Serial.println(F("Failed to read from DHT sensor!"));

      ft = (uint16_t)(30 * 100);
      fh = (uint16_t)(30 * 100);
      fco2 = (uint32_t)(430 * 100);
    }
    else{
      ft = (uint16_t)(t * 100);
      fh = (uint16_t)(h * 100);
      fco2 = (uint32_t)(CO2 * 100);
    }

    payload[0] = ft >> 8;
    payload[1] = ft & 0xFF; // He cambiado % por &
    payload[2] = fh >> 8;
    payload[3] = fh & 0xFF;
    payload[4] = fco2 & 0xFF;
    payload[5] = (fco2 >> 8) & 0xFF;
    payload[6] = (fco2 >> 16) & 0xFF;
    payload[7] = (fco2 >> 24) & 0xFF;


    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, payload, sizeof(payload) - 1, 0);
    Serial.println(F("Packet queued"));
  }
}

void setup() { 
  
  Serial.begin(115200);
  delay(1000);
  ss.begin(9600);  // Software Serial

  Heltec.begin(true /*DisplayEnable Enable*/, true /*LoRa Enable*/, true /*Serial Enable*/, true /*LoRa use PABOOST*/, 868E6 /*LoRa RF working band*/);

  Serial.println(F("Starting\n"));

 
  dht.begin();
  mhz.setAutoCalibration(true);  // default settings - off autocalibration

  // LMIC init
  os_init();
  
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}




void loop() {
  os_runloop_once(); // Comprueba y actualiza constantemente la comunicación LoRaWAN

  if (millis() - getDataTimer >= 3000) {

    //val_tempC = mhz.getTemperature() + OFFSET_TEMP;
    val_CO2 = mhz.getCO2();
    //val_accCO2 = mhz.getAccuracy();

    MHZ19_RESULT response = mhz.retrieveData();

    if (response == MHZ19_RESULT_OK) {
      Serial.print(F("\n\nCO2: "));
      Serial.println(val_CO2);

      Heltec.display -> clear();
      Heltec.display -> drawString(0, 0, "Co2: " + String(val_CO2) + " ppm");
      //Heltec.display -> drawString(0, 10, "Nº de medidas: " + String(counter));
      //Heltec.display -> drawString(0, 20, "Paquetes enviados: " + String(paquete));
      Heltec.display -> display();
    }
    else {
      Serial.print( "Error Reading MH-Z19B Module" );
      Serial.print(F("Error, code: "));
      Serial.println(response);


      Heltec.display -> clear();
      Heltec.display -> drawString(0, 0, "ERROR en medición: " + String(response));
      Heltec.display -> drawString(0, 10, "Nº de medidas: " + String(counter));
      Heltec.display -> drawString(0, 20, "Paquetes enviados: " + String(paquete));
      Heltec.display -> display();
    }
    counter++;

    recogerTemp();

    getDataTimer = millis();
  }
}


void recogerTemp() {
  h = dht.readHumidity();
  t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  Serial.print(F("\n\nHumedad: "));
  Serial.print(h);
  Serial.print(F("% Temperatura: "));
  Serial.print(t);
  Serial.println(F("°C "));


  Heltec.display -> drawString(0, 20, "Temperatura: " + String(t) + " ºC");
  Heltec.display -> drawString(0, 40, "Humedad: " + String(h) + " %");
  Heltec.display -> display();
}
