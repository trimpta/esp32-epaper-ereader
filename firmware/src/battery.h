#pragma once
// Battery percentage from a resistor-divider ADC read. Disabled (returns -1) until
// PIN_BATTERY_ADC in config.h is set — see that comment for why it isn't wired by
// default and what to add. No display-panel changes involved either way.

namespace battery {

// -1 if PIN_BATTERY_ADC is unconfigured, otherwise a rough 0-100 estimate from a
// resting LiPo discharge curve (not a fuel gauge — don't expect lab accuracy).
int readPercent();

}  // namespace battery
