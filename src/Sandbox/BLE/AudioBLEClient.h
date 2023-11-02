#pragma once

#include "AudioBLEStream.h"
//#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace audio_tools {

class AudioBLEClient;
static AudioBLEClient *selfAudioBLEClient = nullptr;

/**
 * @brief A simple BLE client that implements the serial protocol, so that it
 * can be used to send and recevie audio. In BLE terminology this is a Central
 * @ingroup communications
 * @author Phil Schatzmann
 * @copyright GPLv3
 */

class AudioBLEClient : public AudioBLEStream,
                       public BLEClientCallbacks,
                       public BLEAdvertisedDeviceCallbacks {
public:
  AudioBLEClient(int mtu = BLE_BUFFER_SIZE) : AudioBLEStream(mtu) {
    selfAudioBLEClient = this;
  }



  /// starts a BLE client
  bool begin(const char *serverName, int seconds) {
    TRACEI();
    ble_server_name = serverName;
    // Init BLE device
    BLEDevice::init("client");

    // Retrieve a Scanner and set the callback we want to use to be informed
    // when we have detected a new device.
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(this);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(seconds);
    return true;
  }

  void end() override {
    TRACEI();
    flush();
    BLEDevice::deinit();
  }

  size_t readBytes(uint8_t *data, size_t dataSize) override {
    TRACED();
    if (!is_client_connected || !is_client_set_up) return 0;
    if (!ch01_char->canRead()) return 0;
    // changed to auto to be version independent (it changed from std::string to
    // String)
    auto str = ch01_char->readValue();
    if (str.length() > 0){
      memcpy(data, str.c_str(), str.length());
    }
    return str.length();
  }

  int available() override { return BLE_BUFFER_SIZE; }

  size_t write(const uint8_t *data, size_t dataSize) override {
    int result = 0;
    if (!is_client_connected || !is_client_set_up) return 0;
    if (ch02_char->canWrite()) {
      ch02_char->writeValue((uint8_t *)data, dataSize, false);
      result = dataSize;
    }
    return result;
  }

  int availableForWrite() override { return BLE_BUFFER_SIZE; }

  virtual bool connected() override { return is_client_connected; }

protected:
  // client
  BLEClient *p_client = nullptr;
  BLEAdvertising *p_advertising = nullptr;
  BLERemoteService *p_remote_service = nullptr;
  BLEAddress *p_server_address = nullptr;
  BLERemoteCharacteristic *ch01_char = nullptr; // read
  BLERemoteCharacteristic *ch02_char = nullptr; // write
  BLERemoteCharacteristic *info_char = nullptr;
  BLEAdvertisedDevice advertised_device;
  volatile bool is_client_connected = false;
  bool is_client_set_up = false;

  virtual void onConnect(BLEClient *pClient) {
    TRACEI();
    is_client_connected = true;
  }
  virtual void onDisconnect(BLEClient *pClient) {
    TRACEI();
    is_client_connected = false;
  };

  void writeAudioInfoCharacteristic(AudioInfo info) override {
    TRACEI();
    // send update via BLE
    info_char->writeValue((uint8_t *)&info, sizeof(AudioInfo));
  }

  // Scanning Results
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    TRACEI();
    // Check if the name of the advertiser matches
    if (advertisedDevice.getName() == ble_server_name) {
      TRACEI();
      advertised_device = advertisedDevice;
      // Scan can be stopped, we found what we are looking for
      advertised_device.getScan()->stop();
      // Address of advertiser is the one we need
      // p_server_address = new BLEAddress(advertisedDevice.getAddress());

      LOGI("Device '%s' found: Connecting!",
           advertised_device.toString().c_str());
      setupBLEClient();
    }
    delay(10);
  }

  static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
                             uint8_t *pData, size_t length, bool isNotify) {
    TRACEI();
    if (pBLERemoteCharacteristic->getUUID().toString() ==
        selfAudioBLEClient->BLE_INFO_UUID) {
      selfAudioBLEClient->setAudioInfo(pData, length);
    }
  }

  bool setupBLEClient() {
    TRACEI();

    if (p_client == nullptr)
      p_client = BLEDevice::createClient();

    // onConnect and on onDisconnect support
    p_client->setClientCallbacks(this);

    // Connect to the remove BLE Server.
    LOGI("Connecting to %s ...",
         advertised_device.getAddress().toString().c_str());
    // p_client->connect(advertised_device.getAddress(),BLE_ADDR_TYPE_RANDOM);
    p_client->connect(&advertised_device);
    if (!p_client->isConnected()) {
      LOGE("connect failed");
      return false;
    }

    LOGI("Setting mtu to %d", max_transfer_size);
    p_client->setMTU(max_transfer_size);

    // Obtain a reference to the service we are after in the remote BLE
    // server.
    if (p_remote_service == nullptr) {
      p_remote_service = p_client->getService(BLE_SERIAL_SERVICE_UUID);
      if (p_remote_service == nullptr) {
        LOGE("Failed to find our service UUID: %s", BLE_SERIAL_SERVICE_UUID);
        return (false);
      }
    }

    if (ch01_char == nullptr) {
      ch01_char = p_remote_service->getCharacteristic(BLE_CH1_UUID);
    }

    if (ch02_char == nullptr) {
      ch02_char = p_remote_service->getCharacteristic(BLE_CH2_UUID);
    }

    if (is_audio_info_active && info_char == nullptr) {
      info_char = p_remote_service->getCharacteristic(BLE_INFO_UUID);
      info_char->registerForNotify(notifyCallback);
    }
    LOGI("Connected to server: %s", is_client_connected ? "true" : "false");
    is_client_set_up = true;
    return is_client_connected;
  }
};

} // namespace audio_tools