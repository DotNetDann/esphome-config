#include "dallas_component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace dallas {

static const char *const TAG = "dallas.sensor";

static const uint8_t DALLAS_MODEL_DS18S20 = 0x10;
static const uint8_t DALLAS_MODEL_DS1822 = 0x22;
static const uint8_t DALLAS_MODEL_DS18B20 = 0x28;
static const uint8_t DALLAS_MODEL_DS1825 = 0x3B;
static const uint8_t DALLAS_MODEL_DS28EA00 = 0x42;
static const uint8_t DALLAS_COMMAND_START_CONVERSION = 0x44;
static const uint8_t DALLAS_COMMAND_READ_SCRATCH_PAD = 0xBE;
static const uint8_t DALLAS_COMMAND_WRITE_SCRATCH_PAD = 0x4E;

static const uint8_t DALLAS_MODEL_DS2423 = 0x1D;
static const uint8_t DALLAS_DS2423_READ_MEMORY_COMMAND = 0xA5;
static const uint8_t DALLAS_DS2423_PAGE_ONE = 0xC0;
static const uint8_t DALLAS_DS2423_PAGE_TWO = 0xE0;

uint16_t DallasTemperatureSensor::millis_to_wait_for_conversion() const {
  switch (this->resolution_) {
    case 9:
      return 94;
    case 10:
      return 188;
    case 11:
      return 375;
    default:
      return 750;
  }
}

void DallasComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up DallasComponent...");

  pin_->setup();

  // clear bus with 480µs high, otherwise initial reset in search_vec() fails
  pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  delayMicroseconds(480);

  one_wire_ = new ESPOneWire(pin_);  // NOLINT(cppcoreguidelines-owning-memory)

  std::vector<uint64_t> raw_sensors;
  raw_sensors = this->one_wire_->search_vec();

  for (auto &address : raw_sensors) {
    auto *address8 = reinterpret_cast<uint8_t *>(&address);
    if (crc8(address8, 7) != address8[7]) {
      ESP_LOGW(TAG, "Dallas device 0x%s has invalid CRC.", format_hex(address).c_str());
      continue;
    }
    if (address8[0] != DALLAS_MODEL_DS18S20 && address8[0] != DALLAS_MODEL_DS1822 &&
        address8[0] != DALLAS_MODEL_DS18B20 && address8[0] != DALLAS_MODEL_DS1825 &&
        address8[0] != DALLAS_MODEL_DS28EA00 && address8[0] != DALLAS_MODEL_DS2423) {
      ESP_LOGW(TAG, "Unknown device type 0x%02X.", address8[0]);
      continue;
    }
    this->found_sensors_.push_back(address);
  }

  for (auto *sensor : this->sensors_) {
    if (sensor->get_index().has_value()) {
      if (*sensor->get_index() >= this->found_sensors_.size()) {
        this->status_set_error();
        continue;
      }
      sensor->set_address(this->found_sensors_[*sensor->get_index()]);
    }

    if (!sensor->setup_sensor()) {
      this->status_set_error();
    }
  }
}
void DallasComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "DallasComponent:");
  LOG_PIN("  Pin: ", this->pin_);
  LOG_UPDATE_INTERVAL(this);

  if (this->found_sensors_.empty()) {
    ESP_LOGW(TAG, "  Found no sensors!");
  } else {
    ESP_LOGD(TAG, "  Found sensors:");
    for (auto &address : this->found_sensors_) {
      ESP_LOGD(TAG, "    0x%s", format_hex(address).c_str());
    }
  }

  for (auto *sensor : this->sensors_) {
    LOG_SENSOR("  ", "Device", sensor);
    if (sensor->get_index().has_value()) {
      ESP_LOGCONFIG(TAG, "    Index %u", *sensor->get_index());
      if (*sensor->get_index() >= this->found_sensors_.size()) {
        ESP_LOGE(TAG, "Couldn't find sensor by index - not connected. Proceeding without it.");
        continue;
      }
    }
    ESP_LOGCONFIG(TAG, "    Address: %s", sensor->get_address_name().c_str());
    ESP_LOGCONFIG(TAG, "    Resolution: %u", sensor->get_resolution());
  }
}

void DallasComponent::register_sensor(DallasTemperatureSensor *sensor) { this->sensors_.push_back(sensor); }
void DallasComponent::update() {
  this->status_clear_warning();

  bool result;
  {
    InterruptLock lock;
    result = this->one_wire_->reset();
  }
  if (!result) {
    ESP_LOGE(TAG, "Requesting conversion failed");
    this->status_set_warning();
    for (auto *sensor : this->sensors_) {
      sensor->publish_state(NAN);
    }
    return;
  }

  {
    InterruptLock lock;
    this->one_wire_->skip();
    this->one_wire_->write8(DALLAS_COMMAND_START_CONVERSION);
  }

  for (auto *sensor : this->sensors_) {
    this->set_timeout(sensor->get_address_name(), sensor->millis_to_wait_for_conversion(), [this, sensor] {
            
      if (sensor->get_address8()[0] == DALLAS_MODEL_DS2423) {
        bool res = sensor->read_counter(1);
        
        if (!res) {
          ESP_LOGW(TAG, "'%s' - Reading bus failed!", sensor->get_name().c_str());
          sensor->publish_state(NAN);
          this->status_set_warning();
        }
        return;
      } 
    
      bool res = sensor->read_scratch_pad();

      if (!res) {
        ESP_LOGW(TAG, "'%s' - Resetting bus for read failed!", sensor->get_name().c_str());
        sensor->publish_state(NAN);
        this->status_set_warning();
        return;
      }
      if (!sensor->check_scratch_pad()) {
        sensor->publish_state(NAN);
        this->status_set_warning();
        return;
      }

      float tempc = sensor->get_temp_c();
      ESP_LOGD(TAG, "'%s': Got Temperature=%.1f°C", sensor->get_name().c_str(), tempc);
      sensor->publish_state(tempc);
    });
  }
}

void DallasTemperatureSensor::set_address(uint64_t address) { this->address_ = address; }
uint8_t DallasTemperatureSensor::get_resolution() const { return this->resolution_; }
void DallasTemperatureSensor::set_resolution(uint8_t resolution) { this->resolution_ = resolution; }
optional<uint8_t> DallasTemperatureSensor::get_index() const { return this->index_; }
void DallasTemperatureSensor::set_index(uint8_t index) { this->index_ = index; }
uint8_t *DallasTemperatureSensor::get_address8() { return reinterpret_cast<uint8_t *>(&this->address_); }
const std::string &DallasTemperatureSensor::get_address_name() {
  if (this->address_name_.empty()) {
    this->address_name_ = std::string("0x") + format_hex(this->address_);
  }

  return this->address_name_;
}
bool IRAM_ATTR DallasTemperatureSensor::read_counter(uint8_t counter) {
  auto *wire = this->parent_->one_wire_;

  uint8_t data[45];
  data[0] = DALLAS_DS2423_READ_MEMORY_COMMAND;
  data[1] = (counter == 2 ? DALLAS_DS2423_PAGE_TWO : DALLAS_DS2423_PAGE_ONE);
  data[2] = 0x01;

  {
    InterruptLock lock;

    if (!wire->reset()) {
      return false;
    }
  }

  {
    InterruptLock lock;

    wire->select(this->address_);
    wire->write8(data[0]);
    wire->write8(data[1]);
    wire->write8(data[2]);

    for (int j = 3; j < 45; j++) {
        data[j] = wire->read8();
    }
  }

#ifdef ESPHOME_LOG_LEVEL_VERY_VERBOSE
  ESP_LOGVV(TAG, "data: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", 
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9],
            data[10], data[11], data[12], data[13], data[14], data[15], data[16], data[17], data[18], data[19],
            data[20], data[21], data[22], data[23], data[24], data[25], data[26], data[27], data[28], data[29],
            data[30], data[31], data[32], data[33], data[34], data[35], data[36], data[37], data[38], data[39],
            data[40], data[41], data[42], data[43], data[44]);
#endif

  uint32_t count = (uint32_t)data[38];
  for (int j = 37; j >= 35; j--) {
      count = (count << 8) + (uint32_t)data[j];
  }
  
  ESP_LOGD(TAG, "'%s': Recieved count=%d", this->get_name().c_str(), count);
 
  uint16_t crc = get_crc16(data, 43);
  uint8_t *crcBytes = (uint8_t *)&crc;
  uint8_t crcLo = ~data[43];
  uint8_t crcHi = ~data[44];
  
  bool chksum_validity = (crcLo == crcBytes[0]);
  bool config_validity = (crcHi == crcBytes[1]);

  if (!chksum_validity) {
    ESP_LOGW(TAG, "'%s' - Memory checksum invalid!", this->get_name().c_str());
  } else if (!config_validity) {
    ESP_LOGW(TAG, "'%s' - Memory config register invalid!", this->get_name().c_str());
  } else {
      this->publish_state(count);
  }

  return chksum_validity && config_validity;
}
uint16_t DallasTemperatureSensor::get_crc16(uint8_t *data, uint8_t len)
{
      uint16_t crc=0;
      
      for (uint8_t i=0; i<len;i++) 
      {
            uint8_t inbyte = data[i];
            for (uint8_t j=0;j<8;j++) 
            {
                  uint8_t mix = (crc^ inbyte) & 0x01;
                  crc = crc >> 1;
                  if (mix) 
                        crc = crc ^ 0xA001;
                  
                  inbyte = inbyte >> 1;
            }
      }
      return crc;
}
bool IRAM_ATTR DallasTemperatureSensor::read_scratch_pad() {
  auto *wire = this->parent_->one_wire_;

  {
    InterruptLock lock;

    if (!wire->reset()) {
      return false;
    }
  }

  {
    InterruptLock lock;

    wire->select(this->address_);
    wire->write8(DALLAS_COMMAND_READ_SCRATCH_PAD);

    for (unsigned char &i : this->scratch_pad_) {
      i = wire->read8();
    }
  }

  return true;
}
bool DallasTemperatureSensor::setup_sensor() {
  if (this->get_address8()[0] == DALLAS_MODEL_DS2423) {
    return (this->read_counter(1));
  }

  bool r = this->read_scratch_pad();

  if (!r) {
    ESP_LOGE(TAG, "Reading scratchpad failed: reset");
    return false;
  }
  if (!this->check_scratch_pad())
    return false;

  if (this->scratch_pad_[4] == this->resolution_)
    return false;

  if (this->get_address8()[0] == DALLAS_MODEL_DS18S20) {
    // DS18S20 doesn't support resolution.
    ESP_LOGW(TAG, "DS18S20 doesn't support setting resolution.");
    return false;
  }

  switch (this->resolution_) {
    case 12:
      this->scratch_pad_[4] = 0x7F;
      break;
    case 11:
      this->scratch_pad_[4] = 0x5F;
      break;
    case 10:
      this->scratch_pad_[4] = 0x3F;
      break;
    case 9:
    default:
      this->scratch_pad_[4] = 0x1F;
      break;
  }

  auto *wire = this->parent_->one_wire_;
  {
    InterruptLock lock;
    if (wire->reset()) {
      wire->select(this->address_);
      wire->write8(DALLAS_COMMAND_WRITE_SCRATCH_PAD);
      wire->write8(this->scratch_pad_[2]);  // high alarm temp
      wire->write8(this->scratch_pad_[3]);  // low alarm temp
      wire->write8(this->scratch_pad_[4]);  // resolution
      wire->reset();

      // write value to EEPROM
      wire->select(this->address_);
      wire->write8(0x48);
    }
  }

  delay(20);  // allow it to finish operation
  wire->reset();
  return true;
}
bool DallasTemperatureSensor::check_scratch_pad() {
  bool chksum_validity = (crc8(this->scratch_pad_, 8) == this->scratch_pad_[8]);
  bool config_validity = false;

  switch (this->get_address8()[0]) {
    case DALLAS_MODEL_DS18B20:
      config_validity = ((this->scratch_pad_[4] & 0x9F) == 0x1F);
      break;
    default:
      config_validity = ((this->scratch_pad_[4] & 0x10) == 0x10);
  }

#ifdef ESPHOME_LOG_LEVEL_VERY_VERBOSE
  ESP_LOGVV(TAG, "Scratch pad: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X (%02X)", this->scratch_pad_[0],
            this->scratch_pad_[1], this->scratch_pad_[2], this->scratch_pad_[3], this->scratch_pad_[4],
            this->scratch_pad_[5], this->scratch_pad_[6], this->scratch_pad_[7], this->scratch_pad_[8],
            crc8(this->scratch_pad_, 8));
#endif
  if (!chksum_validity) {
    ESP_LOGW(TAG, "'%s' - Scratch pad checksum invalid!", this->get_name().c_str());
  } else if (!config_validity) {
    ESP_LOGW(TAG, "'%s' - Scratch pad config register invalid!", this->get_name().c_str());
  }
  return chksum_validity && config_validity;
}
float DallasTemperatureSensor::get_temp_c() {
  int16_t temp = (int16_t(this->scratch_pad_[1]) << 11) | (int16_t(this->scratch_pad_[0]) << 3);
  if (this->get_address8()[0] == DALLAS_MODEL_DS18S20) {
    int diff = (this->scratch_pad_[7] - this->scratch_pad_[6]) << 7;
    temp = ((temp & 0xFFF0) << 3) - 16 + (diff / this->scratch_pad_[7]);
  }

  return temp / 128.0f;
}
std::string DallasTemperatureSensor::unique_id() { return "dallas-" + str_lower_case(format_hex(this->address_)); }

}  // namespace dallas
}  // namespace esphome
