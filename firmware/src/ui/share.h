#pragma once

#include <cstdint>

#include "ui/draw.h"
#include "ui/overlay.h"

// The share view (§9.3, §10.2, D-093): a QR of the player URL with the code after
// `#`, at 3 px per module inside a light square that is its four-module quiet zone,
// a line saying what the QR is for, and the RT2 code in the room round it: down the
// right of the QR, then across beneath it, broken only after a `:` or a `-`. The
// lineage is the overlay's footer. The QR is encoded once, when the code changes,
// into a fixed buffer (§12 rule 4).
namespace ui {

// Where a scanned loop opens. A placeholder on a reserved domain until the player
// has a home; at most 41 bytes so a 230-character code still fits version 10 (D-013).
constexpr const char* kPlayerUrlPrefix = "https://rota.example/#";
constexpr const char* kScanHint = "scan with a phone to play";

constexpr int kQrModulePx = 3;
constexpr int kQrQuietModules = 4;
constexpr int kQrMaxVersion = 10;    // 57 modules, 271 bytes at level L (D-013)
constexpr int kQrBufferBytes = 408;  // qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion); checked in share.cpp
constexpr int kShareLeft = kMargin;
constexpr int kShareTop = kContentTop;
constexpr int kShareColumns = 25;           // characters per row beneath the QR
constexpr int kShareTextGap = kGlyphAdvance;  // between the QR's square and the rows beside it
constexpr int kShareCodeGap = 4;            // between the square, the hint and the rows beneath

struct QrCode {
  uint8_t modules[kQrBufferBytes];
  bool ok;

  int size() const;  // modules per side; 0 when the encode failed
  bool dark(int x, int y) const;
};

// Encodes kPlayerUrlPrefix + code: byte mode, level L raised when the version
// allows, versions 1–10. False, with size 0, when it does not fit.
bool encode_share_qr(const char* code, QrCode& out);

struct ShareModel {
  const char* code;
  const QrCode* qr;
};

// `bottom` is ui::content_bottom(): the rows beneath the QR stop above it.
void draw_share_view(uint16_t* framebuffer, const ShareModel& model, int bottom);

}  // namespace ui
