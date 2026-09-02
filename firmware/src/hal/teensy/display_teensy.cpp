// Teensy display: the app's framebuffer goes to the ILI9341 panel over SPI1 through
// vindar/ILI9341_T4, which keeps its own copy of the screen and sends only the
// pixels that changed, by DMA, so a frame costs the main loop almost nothing
// (D-047, D-066). The driver's copy sits in RAM2 with the sound engine; the app's
// framebuffer and the diff buffers in RAM1, because RAM2 holds one 150 KB buffer
// beside the 207 KB engine but not two (D-089).
#include <Arduino.h>
#include <ILI9341_T4.h>

#include "hal/hal.h"
#include "hal/teensy/pins.h"
#include "hal/teensy/teensy_internal.h"

namespace {

constexpr uint32_t kSpiClockHz = 30000000;  // the driver's default; the panel's datasheet says 10 MHz (unverified)
constexpr int kDiffBufferBytes = 6000;
constexpr int kDiffGap = 4;
constexpr int kRefreshRateHz = 60;  // §7.3
constexpr int kVsyncSpacing = 1;
constexpr uint8_t kRotation = 1;  // landscape, 320 wide (3 if the image is upside down; unverified)
constexpr int kBacklightFull = 255;

ILI9341_T4::ILI9341Driver tft_(hal::pins::kDisplayCs, hal::pins::kDisplayDc, hal::pins::kDisplaySck,
                               hal::pins::kDisplayMosi, hal::pins::kDisplayMiso, hal::pins::kDisplayReset);
ILI9341_T4::DiffBuffStatic<kDiffBufferBytes> diff_a_;
ILI9341_T4::DiffBuffStatic<kDiffBufferBytes> diff_b_;
DMAMEM uint16_t driver_copy_[hal::kScreenWidth * hal::kScreenHeight];
uint16_t framebuffer_[hal::kScreenWidth * hal::kScreenHeight];
bool ready_ = false;
bool lit_ = false;

}  // namespace

namespace hal::teensy {

void display_init() {
  pinMode(pins::kDisplayBacklight, OUTPUT);
  analogWrite(pins::kDisplayBacklight, 0);  // on after boot, so the boost is not stalled (D-052)
  tft_.output(&Serial);
  ready_ = tft_.begin(kSpiClockHz);
  if (!ready_) {
    Serial.println("hal/teensy: the display did not answer");
    return;
  }
  tft_.setRotation(kRotation);
  tft_.setFramebuffer(driver_copy_);
  tft_.setDiffBuffers(&diff_a_, &diff_b_);
  tft_.setDiffGap(kDiffGap);
  tft_.setRefreshRate(kRefreshRateHz);
  tft_.setVSyncSpacing(kVsyncSpacing);
}

}  // namespace hal::teensy

namespace hal {

uint16_t* framebuffer() { return framebuffer_; }

// Skips the frame when the last transfer is still in flight rather than waiting.
void present() {
  if (!ready_) return;
  if (!lit_) {
    analogWrite(pins::kDisplayBacklight, kBacklightFull);
    lit_ = true;
  }
  if (tft_.asyncUpdateActive()) return;
  tft_.update(framebuffer_);
}

}  // namespace hal
