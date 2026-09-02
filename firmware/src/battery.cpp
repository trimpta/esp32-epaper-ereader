#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace {

struct CurvePoint {
  float voltage;
  int percent;
};

// Resting-voltage LiPo discharge curve — coarse by nature (real state of charge depends
// on load, temperature, and cell age too), but good enough for a "roughly how much is
// left" indicator rather than a precise fuel gauge.
const CurvePoint CURVE[] = {
    {3.30f, 0}, {3.60f, 10}, {3.70f, 30}, {3.80f, 50},
    {3.90f, 65}, {4.00f, 80}, {4.10f, 90}, {4.20f, 100},
};
const size_t CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);

int voltageToPercent(float v) {
  if (v <= CURVE[0].voltage) return CURVE[0].percent;
  if (v >= CURVE[CURVE_LEN - 1].voltage) return CURVE[CURVE_LEN - 1].percent;
  for (size_t i = 0; i + 1 < CURVE_LEN; i++) {
    if (v >= CURVE[i].voltage && v <= CURVE[i + 1].voltage) {
      float t = (v - CURVE[i].voltage) / (CURVE[i + 1].voltage - CURVE[i].voltage);
      return CURVE[i].percent + (int)(t * (CURVE[i + 1].percent - CURVE[i].percent));
    }
  }
  return 0;
}

}  // namespace

int battery::readPercent() {
  if (PIN_BATTERY_ADC < 0) return -1;

  // ESP32-S3 ADC is noisy — average a handful of samples rather than trusting one read.
  const int SAMPLES = 8;
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLES; i++) sum += analogRead(PIN_BATTERY_ADC);
  float raw = sum / (float)SAMPLES;

  // TODO(verify): assumes default 12-bit resolution and ~3.3V full-scale — confirm against
  // analogSetAttenuation()/analogReadResolution() if this reads consistently off once wired.
  float pinVoltage = (raw / 4095.0f) * 3.3f;
  float batteryVoltage = pinVoltage * BATTERY_DIVIDER_RATIO;
  return voltageToPercent(batteryVoltage);
}
