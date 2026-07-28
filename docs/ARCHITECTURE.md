# Architektur

## Datenfluss

Alle Controllerdaten durchlaufen denselben Weg:

```text
Modbus RTU oder Register-Simulation
  -> Profilerkennung
  -> LegacyDeviceReader / NcG3DeviceReader
  -> typisierte Registerdaten
  -> EpeverJson
  -> WebSocket, HTTP-JSON und MQTT
```

Die Simulation erzeugt daher keine fertigen JSON-Werte. Sie stellt realistische
Modbus-Register bereit und testet damit dieselben Decoder und Umrechnungen wie
ein echtes Gerät.

## Verantwortlichkeiten

- `main.cpp` verdrahtet Komponenten und enthält nur `setup()`, `loop()` sowie
  die plattformnahen Initialisierungsfunktionen.
- `EpeverController` koordiniert Profilerkennung und Lesevorgänge.
- `LegacyDeviceReader.cpp` und `NcG3DeviceReader.cpp` lesen die jeweiligen
  Registerserien.
- `EpeverJson.cpp` ist die einzige Abbildung der gelesenen Gerätedaten auf das
  öffentliche JSON-Schema.
- `PollingService` besitzt Polling-Gerätenummer und Polling-Zeitgeber.
- `MqttService` besitzt MQTT-Publish-Zeitgeber und Publish-Anforderungen.
- `ApplicationRequests` besitzt verzögerte Reset-, Neustart- und
  Discovery-Anforderungen.
- `PollingControl` schützt den exklusiven Modbus-Zugriff während
  Gerätebefehlen und OTA.
- `TemperatureSensorService` besitzt Zustand und Ablauf der optionalen
  OneWire-Sensoren.

`liveJson` hat einen Schreiber: Polling und der von ihm aufgerufene
`EpeverController`. Web-, WebSocket- und MQTT-Komponenten erhalten nur
konstanten Zugriff. Web-Befehle ändern Controllerwerte und fordern anschließend
einen neuen MQTT-Publish an; der nächste Poll aktualisiert das JSON.

## Hardware- und Zeitregeln

- Die Hardware-UART ist ausschließlich für Modbus RTU reserviert.
  Diagnoseausgaben gehen über WebSerial.
- Zeitvergleiche verwenden immer `millis() - letzterZeitpunkt`, damit der
  Überlauf von `millis()` korrekt bleibt.
- Die Controller-RTC enthält lokale Wanduhrzeit. Sie wird intern ohne eine
  zusätzliche feste Zeitzonenverschiebung transportiert. NTP verwendet die in
  den Einstellungen konfigurierte POSIX-Zeitzone, bevor es die RTC schreibt.
- Registeradressen stehen ausschließlich in `LegacyRegisters.h` und
  `NcG3Registers.h`. Hardwarepins, Intervalle und Größenlimits stehen in
  `ProjectConfig.h`.
- Die Register-Unionen werden durch `static_assert` gegen unerwartetes Padding
  abgesichert. Zielplattform und Modbus-Wortabbildung sind Little Endian.

## Weboberfläche

HTML-Dateien enthalten nur Darstellung und Browserlogik. Laufzeitdaten werden
über diese Schnittstellen geladen oder geändert:

- `GET /api/system`
- `GET` und `POST /api/settings`
- `GET /livejson` und WebSocket `/ws`
- `GET` und `POST /api/mppt/settings`
- `POST /api/mppt/clone`
- `POST /api/device-time`
- `POST /api/device-address`
- `POST /api/ha-discovery`, `/api/reboot` und `/api/reset`

`src/html.h` ist ein generiertes Build-Artefakt. Änderungen an der Oberfläche
werden in den HTML-Quelldateien unter `src/webpages/` vorgenommen und durch
`tools/mini_html.py` eingebettet.

## Prüfen ohne Gerät

```powershell
python tools/check_invariants.py
platformio run -e simulator
platformio run -e d1_mini -e esp01_1m
```

Der Simulator führt beim Start zusätzlich einen Register-Selbsttest für
Legacy, IT-NC und ET-NC aus. Das Ergebnis steht in WebSerial und als
`SIMULATION_SELF_TEST` im Live-JSON.
