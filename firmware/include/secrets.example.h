// Copy this file to secrets.h (which is git-ignored) and fill in your own values.
// Nothing in this file is real.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// AWS IoT Core: the account's device data endpoint, and this scooter's identity.
#define MQTT_HOST "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com"
#define MQTT_PORT 8883
#define ORG_ID "campus-demo"
#define DEVICE_ID "SC-0001"   // must equal the IoT thing name and the MQTT client id

// Amazon Root CA 1, then the device certificate and private key created for the thing.
static const char AWS_ROOT_CA[] = R"PEM(
-----BEGIN CERTIFICATE-----
paste Amazon Root CA 1 here
-----END CERTIFICATE-----
)PEM";

static const char DEVICE_CERT[] = R"PEM(
-----BEGIN CERTIFICATE-----
paste the device certificate here
-----END CERTIFICATE-----
)PEM";

static const char DEVICE_KEY[] = R"PEM(
paste the device private key here (never commit it)
)PEM";
