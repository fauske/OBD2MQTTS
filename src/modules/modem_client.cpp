#include "../../include/modules/modem_client.h"
#include "config.h"
#include "serial_diagnostics.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t MODEM_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t NETWORK_CHECK_INTERVAL_MS = 5000;
constexpr uint32_t DATA_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t DIAGNOSTIC_INTERVAL_MS = 15000;
constexpr size_t CA_BUFFER_SIZE = 8192;
constexpr char CA_PREFERENCE_NAMESPACE[] = "obd2ca";
constexpr char CA_PREFERENCE_KEY[] = "fnv1a";

bool wait_for_modem_line(const char *expected, uint32_t timeout_ms) {
    char line[192] = {};
    size_t line_length = 0;
    const uint32_t started_ms = millis();

    while (millis() - started_ms < timeout_ms) {
        while (Serial1.available()) {
            const char value = static_cast<char>(Serial1.read());
            if (value == '\r') continue;

            if (value == '\n') {
                line[line_length] = '\0';
                if (strcmp(line, expected) == 0) return true;
                if (strstr(line, "ERROR") != nullptr) return false;
                line_length = 0;
                continue;
            }

            if (line_length + 1 < sizeof(line)) {
                line[line_length++] = value;
            } else {
                line_length = 0;
            }
        }
        delay(1);
    }

    return false;
}

bool send_modem_command(const char *command, const char *expected,
                        uint32_t timeout_ms) {
    while (Serial1.available()) Serial1.read();
    Serial1.print("AT");
    Serial1.print(command);
    Serial1.print("\r\n");
    return wait_for_modem_line(expected, timeout_ms);
}

uint32_t certificate_hash(const char *data, size_t length) {
    uint32_t hash = 2166136261UL;
    for (size_t index = 0; index < length; ++index) {
        hash ^= static_cast<uint8_t>(data[index]);
        hash *= 16777619UL;
    }
    return hash;
}
}

ModemClient::ModemClient()
    : _modem(Serial1),
      _secure_client(_modem),
      _mqtt_client(_secure_client) {
}

void ModemClient::begin() {
    Serial1.begin(SIM7080_BAUD_RATE, SERIAL_8N1,
                  SIM7080_RX_PIN, SIM7080_TX_PIN);
    pinMode(SIM7080_PWRKEY_PIN, OUTPUT);
    pinMode(SIM7080_DTR_PIN, OUTPUT);
    pinMode(SIM7080_RI_PIN, INPUT);
    digitalWrite(SIM7080_DTR_PIN, LOW);

    _mqtt_client.setServer(MQTT_HOST, MQTT_PORT);
    _mqtt_client.setBufferSize(512);
    _mqtt_client.setKeepAlive(60);
    _mqtt_client.setSocketTimeout(15);

    if (CELLULAR_APN[0] == '\0' ||
        strcmp(CELLULAR_APN, "YOUR_CARRIER_APN") == 0) {
        report_error("Set CELLULAR_APN in local secrets.h before LTE can connect");
        return;
    }

    SERIAL_DIAG_PRINTF("[LTE] SIM7080 UART ready; WiFi is not used for MQTT\n");
    _state = State::INITIALIZING;
    _last_reconnect_ms = 0;
    _last_network_check_ms = 0;
    _last_data_attempt_ms = 0;
    _last_mqtt_attempt_ms = 0;
    _last_time_sync_ms = 0;
    _last_status_log_ms = 0;
}

int ModemClient::mqtt_state() {
    return _mqtt_client.state();
}

void ModemClient::update() {
    switch (_state) {
        case State::INITIALIZING:
            do_init();
            break;
        case State::WAITING_NETWORK:
            do_wait_network();
            break;
        case State::CONNECTING_DATA:
            do_connect_data();
            break;
        case State::CONNECTING_MQTT:
            do_connect_mqtt();
            break;
        case State::READY:
            if (!_mqtt_client.connected()) {
                _state = State::CONNECTING_MQTT;
                _last_mqtt_attempt_ms = 0;
            } else {
                _modem.maintain();
                _mqtt_client.loop();
            }
            break;
        default:
            break;
    }
}

void ModemClient::do_init() {
    if (millis() - _last_reconnect_ms < MODEM_RETRY_INTERVAL_MS) return;
    _last_reconnect_ms = millis();

    if (!_modem.testAT(1000)) {
        if (_power_key_attempts < 6) {
            SERIAL_DIAG_PRINTF("[LTE] No AT response; pulsing modem PWRKEY (attempt %u)\n",
                               static_cast<unsigned>(_power_key_attempts + 1));
            digitalWrite(SIM7080_PWRKEY_PIN, LOW);
            delay(100);
            digitalWrite(SIM7080_PWRKEY_PIN, HIGH);
            delay(1100);
            digitalWrite(SIM7080_PWRKEY_PIN, LOW);
            ++_power_key_attempts;
        } else if (millis() - _last_status_log_ms >= DIAGNOSTIC_INTERVAL_MS) {
            _last_status_log_ms = millis();
            report_error("SIM7080 did not respond on UART; check SIM, antenna, power and pins");
            _state = State::INITIALIZING;
        }
        return;
    }

    SERIAL_DIAG_PRINTF("[LTE] SIM7080 answers AT; initializing modem\n");
    if (!_modem.init(CELLULAR_SIM_PIN)) {
        report_error("TinyGSM modem initialization failed");
        return;
    }

    if (_modem.getSimStatus() != SIM_READY) {
        report_error("SIM is not ready; check SIM insertion or PIN configuration");
        return;
    }

    if (!_modem.setNetworkMode(2) || !_modem.setPreferredMode(3)) {
        report_error("Could not select automatic Cat-M/NB-IoT mode");
        return;
    }

    if (!install_ca_certificate()) {
        report_error("Trusted CA provisioning failed; refusing unverified TLS");
        return;
    }
    if (!_secure_client.setCertificate(String(_ca_certificate_name))) {
        report_error("Could not bind trusted CA to SIM7080 TLS socket");
        return;
    }

    _state = State::WAITING_NETWORK;
    _last_network_check_ms = 0;
    SERIAL_DIAG_PRINTF("[LTE] Modem ready; waiting for carrier registration\n");
}

void ModemClient::do_wait_network() {
    if (millis() - _last_network_check_ms < NETWORK_CHECK_INTERVAL_MS) return;
    _last_network_check_ms = millis();

    const SIM70xxRegStatus registration = _modem.getRegistrationStatus();
    _network_registered = registration == REG_OK_HOME || registration == REG_OK_ROAMING;
    _signal_quality = _modem.getSignalQuality();

    if (_network_registered) {
        SERIAL_DIAG_PRINTF("[LTE] Registered on cellular network; signal quality=%d\n",
                           _signal_quality);
        _state = State::CONNECTING_DATA;
        _last_data_attempt_ms = 0;
    } else if (millis() - _last_status_log_ms >= DIAGNOSTIC_INTERVAL_MS) {
        _last_status_log_ms = millis();
        SERIAL_DIAG_PRINTF("[LTE] Waiting for network registration; status=%d signal=%d\n",
                           static_cast<int>(registration), _signal_quality);
    }
}

void ModemClient::do_connect_data() {
    if (!_network_registered) {
        _state = State::WAITING_NETWORK;
        return;
    }

    if (millis() - _last_data_attempt_ms < DATA_RETRY_INTERVAL_MS) return;
    _last_data_attempt_ms = millis();
    SERIAL_DIAG_PRINTF("[LTE] Activating packet-data APN\n");

    _data_connected = _modem.gprsConnect(CELLULAR_APN,
                                         CELLULAR_APN_USER,
                                         CELLULAR_APN_PASSWORD);
    if (_data_connected) {
        SERIAL_DIAG_PRINTF("[LTE] Packet data connected; starting MQTTS\n");
        _state = State::CONNECTING_MQTT;
        _last_mqtt_attempt_ms = 0;
    } else {
        SERIAL_DIAG_PRINTF("[LTE] Packet-data activation failed; retrying\n");
        _state = State::WAITING_NETWORK;
        _last_network_check_ms = millis();
    }
}

void ModemClient::do_connect_mqtt() {
    if (millis() - _last_mqtt_attempt_ms < MQTT_RETRY_INTERVAL_MS) return;
    _last_mqtt_attempt_ms = millis();

    if (!_time_synced) {
        if (millis() - _last_time_sync_ms < NTP_RETRY_INTERVAL_MS) return;
        _last_time_sync_ms = millis();
        const byte ntp_result = _modem.NTPServerSync(CELLULAR_NTP_SERVER, 0);
        if (ntp_result != 1) {
            SERIAL_DIAG_PRINTF("[LTE] NTP failed (%s); deferring MQTTS for certificate time validation\n",
                               _modem.ShowNTPError(ntp_result).c_str());
            return;
        }
        _time_synced = true;
        SERIAL_DIAG_PRINTF("[LTE] Modem clock synchronized; certificate validity can be checked\n");
    }

    if (connect_mqtt()) {
        _state = State::READY;
        SERIAL_DIAG_PRINTF("[MQTTS] Connected over SIM7080 LTE with trusted CA\n");
    } else {
        SERIAL_DIAG_PRINTF("[MQTTS] Connect failed: rc=%d; retrying over LTE\n",
                           _mqtt_client.state());
    }
}

bool ModemClient::install_ca_certificate() {
    const char *certificate = MQTT_CA_CERT;
    static char normalized_certificate[CA_BUFFER_SIZE];
    size_t normalized_length = 0;

    if (certificate == nullptr || strstr(certificate, "BEGIN CERTIFICATE") == nullptr ||
        strstr(certificate, "END CERTIFICATE") == nullptr) {
        SERIAL_DIAG_PRINTF("[MQTTS] MQTT_CA_CERT is missing or is not PEM; refusing insecure connection\n");
        return false;
    }

    for (size_t index = 0; certificate[index] != '\0'; ++index) {
        const char value = certificate[index];
        if (value == ' ' || value == '\t' || value == '\r') continue;
        if (normalized_length + 1 >= sizeof(normalized_certificate)) return false;
        normalized_certificate[normalized_length++] = value;
    }
    normalized_certificate[normalized_length] = '\0';

    const uint32_t hash = certificate_hash(normalized_certificate, normalized_length);
    snprintf(_ca_certificate_name, sizeof(_ca_certificate_name),
             "ca-%08lX.pem", static_cast<unsigned long>(hash));

    Preferences preferences;
    if (!preferences.begin(CA_PREFERENCE_NAMESPACE, false)) return false;
    if (preferences.getUInt(CA_PREFERENCE_KEY, 0) == hash) {
        preferences.end();
        SERIAL_DIAG_PRINTF("[MQTTS] Trusted CA already provisioned in SIM7080\n");
        return true;
    }
    preferences.end();

    if (normalized_length == 0 || normalized_length > 9000) return false;

    if (!send_modem_command("+CFSINIT", "OK", 10000)) return false;

    char command[96];
    snprintf(command, sizeof(command), "+CFSWFILE=3,\"%s\",0,%u,9000",
             _ca_certificate_name, static_cast<unsigned>(normalized_length));
    if (!send_modem_command(command, "DOWNLOAD", 5000)) {
        send_modem_command("+CFSTERM", "OK", 3000);
        return false;
    }

    Serial1.write(reinterpret_cast<const uint8_t *>(normalized_certificate),
                   normalized_length);
    Serial1.flush();
    if (!wait_for_modem_line("OK", 10000)) {
        send_modem_command("+CFSTERM", "OK", 3000);
        return false;
    }

    snprintf(command, sizeof(command),
             "+CSSLCFG=\"CONVERT\",2,\"%s\"", _ca_certificate_name);
    if (!send_modem_command(command, "OK", 15000)) {
        send_modem_command("+CFSTERM", "OK", 3000);
        return false;
    }

    snprintf(command, sizeof(command), "+CFSDFILE=3,\"%s\"",
             _ca_certificate_name);
    const bool deleted = send_modem_command(command, "OK", 5000);
    const bool terminated = send_modem_command("+CFSTERM", "OK", 5000);
    if (!deleted || !terminated) return false;

    if (!preferences.begin(CA_PREFERENCE_NAMESPACE, false)) return false;
    const bool stored = preferences.putUInt(CA_PREFERENCE_KEY, hash) == sizeof(hash);
    preferences.end();
    if (!stored) return false;

    SERIAL_DIAG_PRINTF("[MQTTS] Trusted CA uploaded and converted in SIM7080\n");
    return true;
}

bool ModemClient::connect_mqtt() {
    if (_mqtt_client.connected()) return true;

    if (_mqtt_client.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        return true;
    }

    return false;
}

void ModemClient::report_error(const char *message) {
    _state = State::ERROR;
    SERIAL_DIAG_PRINTF("[LTE] ERROR: %s\n", message);
}

bool ModemClient::publish_telemetry(const BmsData &bms) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["soc_bms"] = bms.soc_bms;
    doc["soc_display"] = bms.soc_display;
    doc["pack_v"] = bms.pack_v;
    doc["pack_a"] = bms.pack_a;
    doc["aux_v"] = bms.aux_v;
    doc["charging"] = bms.charging;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_TELEMETRY, payload, false);
}

bool ModemClient::publish_power(const PowerData &power) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["car_12v"] = power.car_12v;
    doc["board_v"] = power.board_v;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_STATUS, payload, false);
}

bool ModemClient::publish_status(const SystemStatus &status) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["ble_connected"] = status.ble_connected;
    doc["obd_ready"] = status.obd_ready;
    doc["mqtt_online"] = status.mqtt_online;
    doc["rssi"] = status.cell_rssi;
    doc["uptime_s"] = status.uptime_s;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_STATUS, payload, true);
}
