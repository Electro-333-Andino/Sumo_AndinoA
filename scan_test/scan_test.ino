/*
 * Diagnóstico mínimo: ¿el radio del ESP32-C3 recibe algo en modo escaneo?
 * No usa nada del proyecto: solo NimBLE + escaneo + Serial.
 * Ciclo: escanea 5 s, muestra lo que ve, pausa 1 s, repite.
 */
#include <NimBLEDevice.h>

class ScanCB : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        Serial.printf("[T] seen: %s", dev->getAddress().toString().c_str());
        if (dev->haveName()) {
            Serial.printf(" '%s'", dev->getName().c_str());
        }
        Serial.println();
    }
    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.printf("[T] fin: %u dispositivos\n", (unsigned)results.getCount());
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[T] iniciando NimBLE...");
    NimBLEDevice::init("ScanTest");
    Serial.println("[T] listo, escaneando...");
}

void loop() {
    static NimBLEScan* scan = nullptr;
    if (scan == nullptr) {
        scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(new ScanCB());
        scan->setActiveScan(false);
        scan->setInterval(100);
        scan->setWindow(50);
        scan->setDuplicateFilter(false);
    }
    scan->start(5000, true); // 5000 ms = 5 s (asíncrono; onScanEnd al terminar)
    delay(6000);
}
