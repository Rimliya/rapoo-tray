#pragma once

#include <cstdint>

namespace BatteryAlert {

struct Settings {
    bool enabled = true;
    int warningThreshold = 20;
    int criticalThreshold = 10;
    bool chargingReminderEnabled = true;
    int reminderMinutes = 30;
};

enum class Event {
    None,
    Warning,
    Critical,
    ChargeReminder,
};

Settings NormalizeSettings(const Settings& settings);

class Policy {
public:
    Event Update(int battery, bool charging, int64_t nowSeconds, const Settings& settings);
    void Reset();

private:
    bool warningSent_ = false;
    bool criticalSent_ = false;
    bool belowThreshold_ = false;
    bool wasCharging_ = false;
    int64_t nextReminderAt_ = 0;
};

} // namespace BatteryAlert
