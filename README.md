# Solax X3 Hybrid esp8266 modbus-to-influxdb converter
This ESP tool reads the metrics data from the Solax X3 Hybrid Gen4 inverter via modbus and writes it to a InfluxDB

* Reads the important metrics data from the Solax inverter modbus input register
* Every 5 seconds
* The metrics are stored in an InfluxDB (in my case it is hosted on a Raspberry PI and the data are visualized in a Grafana dashboard)

## Hardware

* The Solax X3 Hybrid Gen4 inverter provides a modbus RTU (RS485) interface (it is called COM on the inverter)
  * Remark: some older versions of this inverter also supported modbus TCP but since the 4 generation this option was removed (I don't understand why??)
  * The modbus default baudrate for this device is `19200`
* I used the NodeMCU version of esp8266
* A RJ45 socket is used for this interface
* The following pinning is required:
  * PIN 4 -> RS485 A
  * PIN 5 -> RS485 B
  * PIN 6 -> GND (optional, in my case it was not required)
* For the RS485 connection on the ESP you need an impedance transformer (in my case I used the MAX485

## Pinning

* Connect the A and B pins from the MAX485 to D1 and D2 (in my case I read the data via SoftwareSerial)
* Connect the DE_RE pins together on the D3 pin

