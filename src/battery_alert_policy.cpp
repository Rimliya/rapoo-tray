#include "battery_alert_policy.h"

#include <algorithm>

namespace BatteryAlert {

Settings NormalizeSettings(const Settings& input) {
    Settings settings = input;
    settings.warningThreshold = std::clamp(settings.warningThreshold, 5, 50);
    settings.criticalThreshold = std::clamp(settings.criticalThreshold, 1, settings.warningThreshold - 1);
    settings.reminderMinutes = std::clamp(settings.reminderMinutes, 5, 180);
    return settings;
}

Event Policy::Update(int battery, bool charging, int64_t nowSeconds, const Settings& input) {
    const Settings settings = NormalizeSettings(input);
    battery = std::clamp(battery, 0, 100);

    if (!settings.enabled) {
        Reset();
        return Event::None;
    }

    if (!charging && wasCharging_) {
        nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
    }
    wasCharging_ = charging;

    if (battery > settings.warningThreshold) {
        warningSent_ = false;
        criticalSent_ = false;
        belowThreshold_ = false;
        nextReminderAt_ = 0;
        return Event::None;
    }

    if (charging) {
        if (!belowThreshold_) nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
        belowThreshold_ = true;
        return Event::None;
    }

    if (!belowThreshold_) {
        belowThreshold_ = true;
        nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
    }

    if (battery <= settings.criticalThreshold && !criticalSent_) {
        warningSent_ = true;
        criticalSent_ = true;
        nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
        return Event::Critical;
    }

    if (battery <= settings.warningThreshold && !warningSent_) {
        warningSent_ = true;
        nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
        return Event::Warning;
    }

    if (settings.chargingReminderEnabled && nowSeconds >= nextReminderAt_ && nextReminderAt_ > 0) {
        nextReminderAt_ = nowSeconds + (int64_t)settings.reminderMinutes * 60;
        return Event::ChargeReminder;
    }

    return Event::None;
}

void Policy::Reset() {
    warningSent_ = false;
    criticalSent_ = false;
    belowThreshold_ = false;
    wasCharging_ = false;
    nextReminderAt_ = 0;
}

} // namespace BatteryAlert
