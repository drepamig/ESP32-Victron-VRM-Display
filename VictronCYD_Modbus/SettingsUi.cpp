#include "SettingsUi.h"

#include <cstdio>
#include <cstring>

#include <TFT_eSPI.h>

namespace {
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kBorder = 0x4C9F;
constexpr uint16_t kSelected = 0x2D9F;
constexpr uint16_t kMuted = 0x8410;

constexpr SettingsUiRect kBackBounds{4, 4, 56, 28};
constexpr SettingsUiRect kTimeBounds{4, 52, 312, 40};
constexpr SettingsUiRect kWifiBounds{4, 108, 312, 40};
constexpr SettingsUiRect kTwelveHourBounds{4, 58, 148, 32};
constexpr SettingsUiRect kTwentyFourHourBounds{164, 58, 152, 32};
constexpr SettingsUiRect kChangeZoneBounds{4, 148, 312, 32};
constexpr SettingsUiRect kSaveBounds{212, 204, 104, 32};
constexpr SettingsUiRect kPreviousBounds{4, 204, 99, 32};
constexpr SettingsUiRect kNextBounds{212, 204, 104, 32};

struct Country {
  const char* code;
  const char* label;
};
constexpr Country kCountries[]{
    {"US", "United States"}, {"CA", "Canada"}, {"MX", "Mexico"}, {"UTC", "UTC"}};
constexpr size_t kCountryCount = sizeof(kCountries) / sizeof(kCountries[0]);

SettingsUiRect rowBounds(size_t row) {
  return {4, static_cast<int16_t>(42 + row * 27), 312, 24};
}

// Font 1 keeps long IANA location labels readable within the same fixed-width rows.
uint8_t labelFont(const char* label) {
  return std::strlen(label) > 28 ? 1 : 2;
}
}  // namespace

bool SettingsUiRect::contains(const TouchPoint& point) const {
  return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
}

SettingsUi::SettingsUi(TFT_eSPI& display) : display_(display) {}

void SettingsUi::open(uint32_t nowMs, const TimeSettings& active) {
  active_ = active;
  draft_ = active;
  lastActivityMs_ = nowMs;
  countryIndex_ = 0;
  zonePage_ = 0;
  savePending_ = false;
  saveFailed_ = false;
  changeView(SettingsView::Root);
}

void SettingsUi::close() {
  draft_ = active_;
  view_ = SettingsView::Closed;
  awaitRelease_ = false;
  fullRenderRequested_ = false;
  savePending_ = false;
  saveFailed_ = false;
}

bool SettingsUi::isOpen() const { return view_ != SettingsView::Closed; }
SettingsView SettingsUi::view() const { return view_; }
const TimeSettings& SettingsUi::draft() const { return draft_; }

void SettingsUi::changeView(SettingsView next) {
  view_ = next;
  awaitRelease_ = true;
  fullRenderRequested_ = true;
}

void SettingsUi::handleRelease(uint32_t nowMs) {
  awaitRelease_ = false;
  if (isOpen()) lastActivityMs_ = nowMs;
}

SettingsAction SettingsUi::poll(uint32_t nowMs) {
  if (isOpen() && nowMs - lastActivityMs_ >= kInactivityMs) {
    close();
    return SettingsAction::Exit;
  }
  return SettingsAction::None;
}

bool SettingsUi::takeFullRenderRequest() {
  const bool requested = fullRenderRequested_;
  fullRenderRequested_ = false;
  return requested;
}

size_t SettingsUi::countryZoneCount() const {
  size_t count = 0;
  for (size_t index = 0; index < timeZoneCount(); ++index) {
    if (std::strcmp(timeZoneAt(index)->country, kCountries[countryIndex_].code) == 0) ++count;
  }
  return count;
}

const TimeZone* SettingsUi::countryZoneAt(size_t requested) const {
  size_t found = 0;
  for (size_t index = 0; index < timeZoneCount(); ++index) {
    const TimeZone* zone = timeZoneAt(index);
    if (std::strcmp(zone->country, kCountries[countryIndex_].code) != 0) continue;
    if (found++ == requested) return zone;
  }
  return nullptr;
}

void SettingsUi::selectCountry(size_t country) {
  countryIndex_ = country;
  zonePage_ = 0;
  for (size_t index = 0; index < countryZoneCount(); ++index) {
    if (std::strcmp(countryZoneAt(index)->id, draft_.zoneId) == 0) {
      zonePage_ = index / kRowsPerPage;
      break;
    }
  }
  changeView(SettingsView::Zones);
}

SettingsAction SettingsUi::handleTouch(const TouchPoint& point, uint32_t nowMs) {
  if (!isOpen()) return SettingsAction::None;
  lastActivityMs_ = nowMs;
  if (awaitRelease_ || savePending_) return SettingsAction::None;
  awaitRelease_ = true;

  if (kBackBounds.contains(point)) {
    if (view_ == SettingsView::Root) {
      close();
      return SettingsAction::Exit;
    }
    if (view_ == SettingsView::Time) {
      draft_ = active_;
      saveFailed_ = false;
      changeView(SettingsView::Root);
    } else {
      changeView(view_ == SettingsView::Zones ? SettingsView::Countries : SettingsView::Time);
    }
    return SettingsAction::None;
  }

  if (view_ == SettingsView::Root) {
    if (kTimeBounds.contains(point)) {
      draft_ = active_;
      saveFailed_ = false;
      changeView(SettingsView::Time);
    } else if (kWifiBounds.contains(point)) {
      close();
      return SettingsAction::OpenWifi;
    }
    return SettingsAction::None;
  }

  if (view_ == SettingsView::Time) {
    ClockFormat selected = draft_.format;
    if (kTwelveHourBounds.contains(point)) selected = ClockFormat::TwelveHour;
    if (kTwentyFourHourBounds.contains(point)) selected = ClockFormat::TwentyFourHour;
    if (selected != draft_.format) {
      draft_.format = selected;
      saveFailed_ = false;
      fullRenderRequested_ = true;
    }
    if (kChangeZoneBounds.contains(point)) {
      changeView(SettingsView::Countries);
    } else if (kSaveBounds.contains(point)) {
      savePending_ = true;
      return SettingsAction::Save;
    }
    return SettingsAction::None;
  }

  if (view_ == SettingsView::Countries) {
    for (size_t country = 0; country < kCountryCount; ++country) {
      if (!rowBounds(country).contains(point)) continue;
      if (std::strcmp(kCountries[country].code, "UTC") == 0) {
        const TimeZone* utc = findTimeZone("UTC");
        if (utc != nullptr) {
          draft_.zoneId = utc->id;
          saveFailed_ = false;
          changeView(SettingsView::Time);
        }
      } else {
        selectCountry(country);
      }
      return SettingsAction::None;
    }
    return SettingsAction::None;
  }

  for (size_t row = 0; row < kRowsPerPage; ++row) {
    if (!rowBounds(row).contains(point)) continue;
    const TimeZone* zone = countryZoneAt(zonePage_ * kRowsPerPage + row);
    if (zone != nullptr) {
      draft_.zoneId = zone->id;
      saveFailed_ = false;
      changeView(SettingsView::Time);
    }
    return SettingsAction::None;
  }
  if (kPreviousBounds.contains(point) && zonePage_ > 0) {
    --zonePage_;
    fullRenderRequested_ = true;
  } else if (kNextBounds.contains(point) &&
             (zonePage_ + 1) * kRowsPerPage < countryZoneCount()) {
    ++zonePage_;
    fullRenderRequested_ = true;
  }
  return SettingsAction::None;
}

void SettingsUi::saveResult(bool success) {
  if (view_ != SettingsView::Time || !savePending_) return;
  savePending_ = false;
  saveFailed_ = !success;
  if (success) {
    active_ = draft_;
    changeView(SettingsView::Root);
  } else {
    fullRenderRequested_ = true;
  }
}

void SettingsUi::drawButton(const SettingsUiRect& bounds, const char* label, bool selected,
                            bool enabled, uint8_t font) {
  const uint16_t background = selected && enabled ? kSelected : kPanel;
  display_.fillRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 4, background);
  display_.drawRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 4, kBorder);
  display_.setTextColor(enabled ? TFT_WHITE : kMuted, background);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(label, bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, font);
}

void SettingsUi::drawHeader(const char* title) {
  drawButton(kBackBounds, "Back");
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(title, 190, 18, 2);
}

void SettingsUi::renderTime() {
  drawHeader("Time");
  display_.setTextColor(kMuted, kBackground);
  display_.setTextDatum(TL_DATUM);
  display_.drawString("Clock format", 4, 38, 2);
  drawButton(kTwelveHourBounds, "12h", draft_.format == ClockFormat::TwelveHour);
  drawButton(kTwentyFourHourBounds, "24h", draft_.format == ClockFormat::TwentyFourHour);
  const TimeZone* zone = findTimeZone(draft_.zoneId);
  const char* label = zone != nullptr ? zone->label : draft_.zoneId;
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.setTextDatum(TL_DATUM);
  display_.drawString(label, 8, 105, labelFont(label));
  display_.setTextColor(kMuted, kBackground);
  display_.drawString(draft_.zoneId, 8, 127, 1);
  drawButton(kChangeZoneBounds, "Change timezone");
  if (saveFailed_) {
    display_.setTextDatum(TL_DATUM);
    display_.setTextColor(TFT_RED, kBackground);
    display_.drawString("Save failed. Tap Save to retry.", 4, 186, 1);
  }
  drawButton(kSaveBounds, "Save");
}

void SettingsUi::renderCountries() {
  drawHeader("Time zone");
  const TimeZone* selected = findTimeZone(draft_.zoneId);
  for (size_t country = 0; country < kCountryCount; ++country) {
    drawButton(rowBounds(country), kCountries[country].label,
               selected != nullptr && std::strcmp(selected->country, kCountries[country].code) == 0);
  }
}

void SettingsUi::renderZones() {
  drawHeader(kCountries[countryIndex_].label);
  for (size_t row = 0; row < kRowsPerPage; ++row) {
    const TimeZone* zone = countryZoneAt(zonePage_ * kRowsPerPage + row);
    if (zone == nullptr) break;
    const SettingsUiRect bounds = rowBounds(row);
    const bool selected = std::strcmp(zone->id, draft_.zoneId) == 0;
    drawButton(bounds, zone->label, selected, true, labelFont(zone->label));
    if (selected) {
      display_.setTextDatum(MC_DATUM);
      display_.drawString("*", 306, bounds.y + bounds.height / 2, 2);
    }
  }
  const size_t count = countryZoneCount();
  drawButton(kPreviousBounds, "Previous", false, zonePage_ > 0);
  drawButton(kNextBounds, "Next", false, (zonePage_ + 1) * kRowsPerPage < count);
  char page[24];
  std::snprintf(page, sizeof(page), "%u / %u", static_cast<unsigned>(zonePage_ + 1),
                static_cast<unsigned>((count + kRowsPerPage - 1) / kRowsPerPage));
  display_.setTextColor(kMuted, kBackground);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(page, 158, 220, 1);
}

void SettingsUi::render() {
  if (!isOpen()) return;
  fullRenderRequested_ = false;
  display_.fillScreen(kBackground);
  switch (view_) {
    case SettingsView::Root:
      drawHeader("Settings");
      drawButton(kTimeBounds, "Time");
      drawButton(kWifiBounds, "Wi-Fi");
      break;
    case SettingsView::Time: renderTime(); break;
    case SettingsView::Countries: renderCountries(); break;
    case SettingsView::Zones: renderZones(); break;
    case SettingsView::Closed: break;
  }
}
