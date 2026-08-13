# Shopfloor Windows & Door State Monitoring System

An end-to-end IoT monitoring solution that tracks the real-time **OPEN / CLOSED** status of shopfloor windows and doors using Wi-Fi-enabled microcontrollers, an **InfluxDB** time-series database, and **Grafana** visualization dashboards.

---

## Project Overview

The primary goal of this project is to collect magnetic door/window sensor data, ingest it into an InfluxDB server via HTTP Line Protocol over Wi-Fi, and provide shopfloor visibility and alerting through Grafana.

```
┌────────────────────────┐      Wi-Fi / HTTP      ┌─────────────────────────────────┐
│  Wemos D1 Mini / ESP   │ ─────────────────────> │     CentOS Stream 8 Server      │
│  (Magnetic Switch Pin) │  POST /write (8086/8181)│  (InfluxDB v1 / InfluxDB v3 Core)│
└────────────────────────┘                        └─────────────────────────────────┘
                                                                   │
                                                                   ▼
                                                  ┌─────────────────────────────────┐
                                                  │         Grafana v9.2.10         │
                                                  │       (Real-Time Dashboards)    │
                                                  └─────────────────────────────────┘

```

---

## Hardware Requirements

* **Microcontroller:** Wemos D1 Mini (ESP32 / ESP8266)
* **Sensor:** Magnetic Door Switch (Cảm Biến Từ Tính Gắn Cửa)
* **Cable:** Micro-USB / USB-C Data Cable (for flashing & 5V power supply)
* **Power Supply:** 5V USB Wall Adapter / Shopfloor power line

### Logic Level Definition

* **State `1` (CLOSED):** Sensor contacts closed $\rightarrow$ Circuit pulled to **LOW / 0V**.
* **State `0` (OPEN):** Sensor contacts separated $\rightarrow$ Circuit pulled to **HIGH / 3.3V**.

---

## Tech Stack & Infrastructure

* **Firmware:** Custom C++ / NAMF Firmware (Compiled via **PlatformIO**)
* **Server OS:** CentOS Stream 8 (Server IP: `172.25.1.15`)
* **Time-Series Database:**
* InfluxDB v1.x (Port `8086`)
* InfluxDB v3 Core (Port `8181` - systemd background service)


* **Visualization:** Grafana v9.2.10 (Port `3000`)
* **Server Web UI Management:** Cockpit (`172.25.1.15:9090`)

---

## Firmware Flashing (PlatformIO)

The microcontroller runs a compiled firmware image responsible for reading digital input pins, maintaining Wi-Fi connection, and posting HTTP payloads to InfluxDB.

### 1. Build & Upload Command

Connect the ESP device via USB data cable and run PlatformIO from your terminal:

```bash
# Upload firmware over the designated serial COM port
pio run -e lang_en -t upload --upload-port COM3

```

> **Note on Serial vs Network Ports:**
> * **COM Port (e.g., `COM3`, `/dev/ttyUSB0`):** Physical USB-to-serial connection used strictly for initial flashing and reading local boot logs via Serial Monitor (115200 baud).
> * **Network Port (e.g., `8086`, `8181`):** TCP ports used by the ESP over Wi-Fi to push time-series data to InfluxDB once booted.
> 
> 

---

## Database Setup (InfluxDB 3 Core on CentOS)

### 1. Service Installation & Startup

To run InfluxDB v3 as a persistent system service on the CentOS host (`172.25.1.15`):

```bash
# Connect to the server
ssh root@172.25.1.15

# Create data directory
mkdir -p /root/influxdata

# Test manual start
/root/.influxdb/influxdb3 serve --http-bind 0.0.0.0:8181 --data-dir /root/influxdata --node-id rrc-node-01

```

### 2. Admin Token Generation

Open a secondary SSH terminal to issue the administrative token:

```bash
influxdb3 create token --admin

```

### 3. Systemd Service Setup

To ensure InfluxDB 3 auto-starts on system reboot, create `/etc/systemd/system/influxdb3-wrapper.service`:

```ini
[Unit]
Description=InfluxDB3 Server Wrapper
After=network.target

[Service]
Type=simple
ExecStart=/root/.influxdb/influxdb3 serve --http-bind 0.0.0.0:8181 --data-dir /root/influxdata --node-id rrc-node-01
Restart=always

[Install]
WantedBy=multi-user.target

```

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl start influxdb3-wrapper
sudo systemctl enable influxdb3-wrapper

```

### 4. Shell Environment Shortcuts (`~/.bashrc`)

Add shortcuts to simplify query execution:

```bash
export INFLUXDB3_HOST_URL='http://172.25.1.15:8181'
export INFLUXDB3_AUTH_TOKEN='YOUR_ADMIN_TOKEN_HERE'

# Alias for quick InfluxQL queries
alias exec='curl -s -X POST $INFLUXDB3_HOST_URL/api/v3/query_influxql -H "Authorization: Bearer $INFLUXDB3_AUTH_TOKEN" -H "Content-Type: application/json" -d'

```

---

## 📡 Data Schema & Influx Line Protocol

Data points are written via standard HTTP `POST` requests using InfluxDB Line Protocol.

### Measurement Format

```text
status,hostname=<HOST_TAG>,location=<LOCATION_TAG> state=<0|1>i

```

* **Database:** `rrcvn_monitoring` (or `window_monitoring`)
* **Measurement:** `status`
* **Tags:**
* `hostname`: E.g., `SHOPFLOOR`
* `location`: E.g., `Window_1`, `Window_2`, `Window_3`, `Window_4`


* **Fields:**
* `state`: Integer (`1i` = Closed, `0i` = Open)



### Manual Test Write Command

```bash
influxdb3 write --database rrcvn_monitoring "status,hostname=SHOPFLOOR,location=Window_1 state=1i"

```

---

## Grafana Integration

1. Access Grafana at `[http://172.25.1.15:3000](http://172.25.1.15:3000)`.
2. Add a new **InfluxDB Data Source**.
3. Configure URL (`[http://172.25.1.15:8086](http://172.25.1.15:8086)` for v1 or `[http://172.25.1.15:8181](http://172.25.1.15:8181)` for v3 Core).
4. Point to database `rrcvn_monitoring` / `window_monitoring`.
5. Build State Timeline or Discrete Status panels to render window state history and set up alerts for open windows exceeding safe durations.

---

## Sensor Power Cut / Reboot Recovery Procedure

If the shopfloor experiences a power outage or a sensor unit loses connection, follow this procedure to ensure clean state initialization:

1. **Open the physical window/door** assigned to the target sensor module.
2. **Unplug the power cable** from the ESP board and wait **1–2 minutes**.
3. **Plug the power cable back in**.
4. **Wait 1–2 minutes** for the ESP to complete its boot sequence, associate with the Wi-Fi network, and send its initial telemetry packet.
5. **Verify data arrival** on the Grafana live dashboard.
6. **Close the window/door** once real-time updates are confirmed.
