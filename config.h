#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  config.h — Per-Device Configuration for Code RED IoT Swarm
//  Edit this file before flashing each ESP32 unit.
// =============================================================================

// -----------------------------------------------------------------------------
//  DEVICE IDENTITY
//  Assign a unique ID to each ESP32 unit: 1, 2, or 3
// -----------------------------------------------------------------------------
#define DEVICE_ID         1           // ← Change to 2 or 3 for other units

// -----------------------------------------------------------------------------
//  WIFI CREDENTIALS
// -----------------------------------------------------------------------------
#define WIFI_SSID         "YOUR_WIFI_SSID"
#define WIFI_PASSWORD     "YOUR_WIFI_PASSWORD"

// -----------------------------------------------------------------------------
//  UDP NETWORK SETTINGS
// -----------------------------------------------------------------------------
#define UDP_PORT          2910        // Port used by all swarm devices
#define BROADCAST_IP      "192.168.0.255"  // Local subnet broadcast address
                                           // Adjust subnet if your router differs

// -----------------------------------------------------------------------------
//  RASPBERRY PI SERVER (for JSON log packets)
//  Set this to your RPi's static/local IP address
// -----------------------------------------------------------------------------
#define RPI_IP            "192.168.0.100"  // ← Replace with your RPi's IP
#define RPI_UDP_PORT      2910

// -----------------------------------------------------------------------------
//  TIMING
// -----------------------------------------------------------------------------
#define SWARM_BROADCAST_INTERVAL_MS   100   // Binary packet broadcast rate
#define JSON_REPORT_INTERVAL_MS       1000  // JSON log send rate to RPi

// -----------------------------------------------------------------------------
//  PIN DEFINITIONS
// -----------------------------------------------------------------------------
#define PHOTORESISTOR_PIN   34    // ADC input — light sensor
#define LED_PWM_PIN         23    // PWM output — master indicator LED

// Only used on DEVICE_ID == 1 (ESP32 with LED Bar Graph)
#define LED_BAR_PIN_1       13
#define LED_BAR_PIN_2       12
#define LED_BAR_PIN_3       14
#define LED_BAR_PIN_4       27
#define LED_BAR_PIN_5       26
#define LED_BAR_PIN_6       25
#define LED_BAR_PIN_7       33
#define LED_BAR_PIN_8       32
#define LED_BAR_PIN_9       35
#define LED_BAR_PIN_10      34    // Note: shares ADC — remap if needed

// -----------------------------------------------------------------------------
//  PACKET PROTOCOL CONSTANTS  (do not change — must match RPi and all ESPs)
// -----------------------------------------------------------------------------
#define PACKET_HEADER         0xF0
#define PACKET_FOOTER         0x0F
#define PACKET_LENGTH         14

#define PKT_LIGHT_UPDATE      0x01
#define PKT_RESET_SWARM       0x02
#define PKT_DEFINE_SERVER     0x03

// -----------------------------------------------------------------------------
//  ADC LIGHT SENSOR CALIBRATION
//  Adjust these if your photoresistor values differ in your environment
// -----------------------------------------------------------------------------
#define LIGHT_ADC_MIN         0
#define LIGHT_ADC_MAX         4095    // ESP32 ADC is 12-bit

// -----------------------------------------------------------------------------
//  DEBUG
//  Set to 1 to enable Serial debug output (disable before final deployment)
// -----------------------------------------------------------------------------
#define DEBUG_SERIAL          1
#define SERIAL_BAUD_RATE      115200

#endif // CONFIG_H
