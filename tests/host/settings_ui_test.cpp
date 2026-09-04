#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <TFT_eSPI.h>

#include "SettingsUi.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

struct Fixture {
  TFT_eSPI display;
  SettingsUi ui{display};
  TimeSettings active;
  uint32_t now = 100;

  Fixture() {
    ui.open(now++, active);
    ui.handleRelease(now++);
  }

  SettingsAction tap(int16_t x, int16_t y) {
    const SettingsAction action = ui.handleTouch({x, y}, now++);
    ui.handleRelease(now++);
    return action;
  }

  void openTime() {
    check(tap(160, 72) == SettingsAction::None && ui.view() == SettingsView::Time,
          "Time row opens Time without performing an external action");
  }

  void openCountries() {
    check(tap(160, 164) == SettingsAction::None && ui.view() == SettingsView::Countries,
          "Change timezone opens country selection");
  }
};

// Break caught: a gear press or held finger accidentally activates a newly shown row.
void testEntryAndEveryContactActivateOnlyOnce() {
  TFT_eSPI display;
  SettingsUi ui(display);
  TimeSettings active;
  ui.open(100, active);
  check(ui.view() == SettingsView::Root && ui.takeFullRenderRequest(),
        "opening Settings requests its root screen");
  check(!ui.takeFullRenderRequest(), "render request is consumed once");
  check(ui.handleTouch({160, 72}, 101) == SettingsAction::None &&
            ui.view() == SettingsView::Root,
        "entry contact cannot activate the root Time row");
  ui.handleRelease(102);
  ui.handleTouch({160, 72}, 103);
  check(ui.view() == SettingsView::Time && ui.takeFullRenderRequest(),
        "fresh contact opens Time");
  ui.handleTouch({240, 74}, 104);
  check(ui.draft().format == ClockFormat::TwelveHour && !ui.takeFullRenderRequest(),
        "same held contact cannot change the format after navigating");
  ui.handleRelease(105);
  ui.handleTouch({240, 74}, 106);
  check(ui.draft().format == ClockFormat::TwentyFourHour && ui.takeFullRenderRequest(),
        "fresh contact changes format and requests rendering");
  ui.handleTouch({80, 74}, 107);
  check(ui.draft().format == ClockFormat::TwentyFourHour && !ui.takeFullRenderRequest(),
        "sliding a held contact across buttons cannot select another format");
  ui.handleRelease(108);
  ui.handleTouch({319, 239}, 109);
  ui.handleTouch({80, 74}, 110);
  check(ui.draft().format == ClockFormat::TwentyFourHour,
        "a contact starting outside buttons cannot slide onto a control");
}

// Break caught: draft edits leak into active settings or survive explicit cancellation.
void testDraftCancelAndRootRoutes() {
  Fixture f;
  f.openTime();
  f.tap(240, 74);
  check(f.active.format == ClockFormat::TwelveHour &&
            f.ui.draft().format == ClockFormat::TwentyFourHour,
        "format selection changes only the draft");
  f.openCountries();
  f.tap(160, 135);  // UTC is the fourth country entry.
  check(f.ui.view() == SettingsView::Time && std::strcmp(f.ui.draft().zoneId, "UTC") == 0,
        "UTC country entry selects UTC directly");
  check(f.tap(30, 18) == SettingsAction::None && f.ui.view() == SettingsView::Root,
        "Time Back returns to Settings root");
  check(sameTimeSettings(f.ui.draft(), f.active), "Time Back discards all draft edits");
  f.openTime();
  check(sameTimeSettings(f.ui.draft(), f.active), "reopening Time starts with active settings");
  f.tap(30, 18);
  check(f.tap(160, 128) == SettingsAction::OpenWifi && !f.ui.isOpen(),
        "Wi-Fi root row hands off to Wi-Fi setup and closes Settings");
  f.ui.open(f.now++, f.active);
  f.ui.handleRelease(f.now++);
  check(f.tap(30, 18) == SettingsAction::Exit && !f.ui.isOpen(),
        "Settings root Back exits the modal");
}

// Break caught: save applies prematurely, errors erase edits, or successful saves fail to rebase.
void testSaveHandoffFailureRetryAndRebase() {
  Fixture f;
  f.openTime();
  f.tap(240, 74);
  const SettingsAction action = f.ui.handleTouch({264, 220}, f.now++);
  check(action == SettingsAction::Save && f.ui.view() == SettingsView::Time,
        "Save hands off draft while leaving Time available for the result");
  check(f.active.format == ClockFormat::TwelveHour, "Save action alone does not change active state");
  check(f.ui.handleTouch({264, 220}, f.now++) == SettingsAction::None,
        "holding Save cannot emit another save action");
  f.ui.saveResult(false);
  check(f.ui.view() == SettingsView::Time &&
            f.ui.draft().format == ClockFormat::TwentyFourHour,
        "failed save preserves the draft on Time");
  f.ui.render();
  check(f.display.drewContaining("retry"), "failed save renders an actionable retry message");
  check(f.ui.handleTouch({264, 220}, f.now++) == SettingsAction::None,
        "a failed save does not release the original touch gate");
  f.ui.handleRelease(f.now++);
  check(f.tap(264, 220) == SettingsAction::Save, "fresh touch can retry a failed save");
  f.active = f.ui.draft();  // The sketch owns persistence and application.
  f.ui.saveResult(true);
  check(f.ui.view() == SettingsView::Root && f.ui.takeFullRenderRequest(),
        "successful save returns to Settings root");
  f.ui.handleRelease(f.now++);
  f.openTime();
  f.tap(80, 74);
  f.tap(30, 18);
  check(f.ui.draft().format == ClockFormat::TwentyFourHour,
        "later cancellation restores the newly saved value");
  f.ui.render();
  check(!f.display.drewContaining("retry"), "save success clears the error");
  f.ui.close();
  f.ui.saveResult(true);
  check(!f.ui.isOpen(), "a late save result cannot reopen closed Settings");
}

// Break caught: navigating back inside the picker erases a format edit or skips a level.
void testPickerBackPreservesDraftAndSelection() {
  Fixture f;
  f.openTime();
  f.tap(240, 74);
  f.openCountries();
  f.tap(160, 54);
  check(f.ui.view() == SettingsView::Zones, "United States opens its city list");
  f.ui.render();
  check(f.display.drew("United States") && f.display.drew("Chicago") && f.display.drew("*"),
        "country picker opens on and marks the current city");
  f.tap(30, 18);
  check(f.ui.view() == SettingsView::Countries, "city Back returns one level to countries");
  f.tap(30, 18);
  check(f.ui.view() == SettingsView::Time &&
            f.ui.draft().format == ClockFormat::TwentyFourHour,
        "country Back returns to Time and retains format edits");
  check(std::strcmp(f.ui.draft().zoneId, "America/Chicago") == 0,
        "backing out of the picker retains the prior timezone");
}

// Break caught: paging repeats a city, omits a tail page, or revisit loses selected-page context.
void testPaginationSelectionAndRevisit() {
  Fixture f;
  f.openTime();
  f.openCountries();
  f.tap(160, 54);
  f.ui.render();
  check(f.display.drew("1 / 6"), "United States first page shows six available pages");
  f.tap(264, 220);
  f.ui.render();
  check(f.display.drew("2 / 6") && !f.display.drew("Chicago"),
        "Next displays the second city page");
  f.tap(54, 220);
  f.ui.render();
  check(f.display.drew("1 / 6") && f.display.drew("Chicago"),
        "Previous returns to the earlier city page");
  for (int page = 0; page < 8; ++page) f.tap(264, 220);
  f.ui.render();
  check(f.display.drew("6 / 6") && f.display.drew("Sitka"),
        "Next is bounded at the final page and preserves the tail city");
  f.tap(160, 108);  // Sitka is the third row on the last United States page.
  check(f.ui.view() == SettingsView::Time &&
            std::strcmp(f.ui.draft().zoneId, "America/Sitka") == 0,
        "selecting the tail page city changes the draft and returns to Time");
  f.openCountries();
  f.tap(160, 54);
  f.ui.render();
  check(f.display.drew("6 / 6") && f.display.drew("Sitka") && f.display.drew("*"),
        "revisiting a country opens the selected city's page with its marker");
  f.tap(30, 18);
  f.tap(160, 81);
  f.ui.render();
  check(f.ui.view() == SettingsView::Zones && f.display.drew("Canada"),
        "Canada has its own city list");
  f.tap(30, 18);
  f.tap(160, 108);
  f.ui.render();
  check(f.ui.view() == SettingsView::Zones && f.display.drew("Mexico"),
        "Mexico has its own city list");
}

// Break caught: modal timeouts preserve unsaved settings, ignore picker screens, or fail at wrap.
void testTimeoutEveryViewAndWrapSafeActivity() {
  for (int depth = 0; depth < 4; ++depth) {
    Fixture f;
    if (depth >= 1) { f.openTime(); f.tap(240, 74); }
    if (depth >= 2) f.openCountries();
    if (depth >= 3) f.tap(160, 54);
    const uint32_t lastActivity = f.now - 1;
    check(f.ui.poll(lastActivity + 59999) == SettingsAction::None && f.ui.isOpen(),
          "modal remains open before the inactivity boundary");
    check(f.ui.poll(lastActivity + 60000) == SettingsAction::Exit && !f.ui.isOpen(),
          "every Settings view exits at sixty seconds of inactivity");
    check(sameTimeSettings(f.ui.draft(), f.active), "timeout discards pending edits");
    check(f.ui.poll(lastActivity + 60001) == SettingsAction::None,
          "closed Settings cannot emit repeated exits");
  }
  TFT_eSPI display;
  SettingsUi ui(display);
  TimeSettings active;
  ui.open(0xfffff000u, active);
  ui.handleRelease(0xfffff001u);
  ui.handleTouch({319, 239}, 0xfffffff0u);
  check(ui.poll(0xfffffff0u + 59999u) == SettingsAction::None,
        "touch activity extends timeout across millis rollover");
  check(ui.poll(0xfffffff0u + 60000u) == SettingsAction::Exit,
        "unsigned elapsed time expires correctly after millis rollover");
}
}  // namespace

int main() {
  testEntryAndEveryContactActivateOnlyOnce();
  testDraftCancelAndRootRoutes();
  testSaveHandoffFailureRetryAndRebase();
  testPickerBackPreservesDraftAndSelection();
  testPaginationSelectionAndRevisit();
  testTimeoutEveryViewAndWrapSafeActivity();
  std::puts("settings_ui_test: all passed");
  return 0;
}
