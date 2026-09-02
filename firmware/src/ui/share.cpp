#include "ui/share.h"

#include <cstdio>
#include <cstring>

#include "qrcodegen.h"
#include "ui/color.h"
#include "ui/font.h"

namespace ui {

static_assert(kQrBufferBytes == qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion), "the QR buffer must hold version 10");

namespace {

constexpr int kQrTextCapacity = 320;  // the prefix, a 238-character code and NUL
constexpr int kRowCapacity = kShareColumns + 1;
constexpr int kMaxRows = 32;  // beside a version-10 QR 13 rows fit, beneath a small one 14
constexpr int kFlowCount = 2;
const char* const kEllipsis = "...";
const char* const kNoQr = "no qr";

constexpr uint16_t kBackground = rgb565(kScreenBackground);
constexpr uint16_t kText = rgb565(kScreenText);
constexpr uint16_t kLegend565 = rgb565(kLegend);

// A run of rows for the code: down the right of the QR, or across beneath it.
struct Flow {
  int x;
  int y;
  int columns;
  int rows;
};

struct Row {
  int x;
  int y;
  int columns;
  int length;
  char text[kRowCapacity];
};

bool is_break(char c) { return c == ':' || c == '-'; }

// Lays the code out as whole tokens, a token ending with its `:` or `-`, into the
// flows in order. A token wider than a flow moves the rest of the code on to the
// next flow. `complete` is false when tokens remain with nowhere to go.
int lay_out(const char* code, const Flow* flows, Row* rows, bool& complete) {
  int row_count = 0;
  int flow_index = 0;
  int rows_in_flow = 0;
  Row* row = nullptr;
  const char* at = code;
  complete = true;
  while (*at != '\0') {
    int length = 0;
    while (at[length] != '\0' && !is_break(at[length])) ++length;
    if (at[length] != '\0') ++length;
    for (;;) {
      if (flow_index >= kFlowCount || row_count >= kMaxRows) {
        complete = false;
        return row_count;
      }
      const Flow& flow = flows[flow_index];
      if (length > flow.columns || flow.rows == 0) {  // no row of this flow could hold it
        ++flow_index;
        rows_in_flow = 0;
        row = nullptr;
        continue;
      }
      if (row != nullptr && row->length + length <= row->columns) break;
      if (rows_in_flow >= flow.rows) {
        ++flow_index;
        rows_in_flow = 0;
        row = nullptr;
        continue;
      }
      row = &rows[row_count++];
      *row = Row{flow.x, flow.y + rows_in_flow * kLineHeight, flow.columns, 0, {}};
      ++rows_in_flow;
      break;
    }
    std::memcpy(row->text + row->length, at, static_cast<size_t>(length));
    row->length += length;
    row->text[row->length] = '\0';
    at += length;
  }
  return row_count;
}

// The QR carries the whole code; the text says it goes on.
void mark_incomplete(Row& row) {
  const int ellipsis = static_cast<int>(std::strlen(kEllipsis));
  const int at = row.length + ellipsis <= row.columns ? row.length : row.columns - ellipsis;
  std::memcpy(row.text + at, kEllipsis, static_cast<size_t>(ellipsis) + 1);
  row.length = at + ellipsis;
}

}  // namespace

int QrCode::size() const { return ok ? qrcodegen_getSize(modules) : 0; }

bool QrCode::dark(int x, int y) const { return ok && qrcodegen_getModule(modules, x, y); }

bool encode_share_qr(const char* code, QrCode& out) {
  char text[kQrTextCapacity];
  uint8_t scratch[kQrBufferBytes];
  std::snprintf(text, sizeof text, "%s%s", kPlayerUrlPrefix, code);
  out.ok = qrcodegen_encodeText(text, scratch, out.modules, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, kQrMaxVersion,
                                qrcodegen_Mask_AUTO, true);
  return out.ok;
}

void draw_share_view(uint16_t* framebuffer, const ShareModel& model, int bottom) {
  Canvas canvas{framebuffer};
  fill(canvas, kBackground);
  const int modules = model.qr->size();
  if (modules == 0) {
    draw_text(canvas, kShareLeft, kShareTop, kNoQr, kLegend565);
    draw_text(canvas, kShareLeft, kShareTop + kLineHeight, model.code, kText);
    return;
  }
  const int side = (modules + 2 * kQrQuietModules) * kQrModulePx;
  fill_rect(canvas, kShareLeft, kShareTop, side, side, kText);
  for (int y = 0; y < modules; ++y) {
    for (int x = 0; x < modules; ++x) {
      if (!model.qr->dark(x, y)) continue;
      fill_rect(canvas, kShareLeft + (kQrQuietModules + x) * kQrModulePx, kShareTop + (kQrQuietModules + y) * kQrModulePx,
                kQrModulePx, kQrModulePx, kBackground);
    }
  }

  int below = kShareTop + side + kShareCodeGap;
  if (below + kLineHeight <= bottom) {  // what the square is for, in the legend's voice
    draw_text(canvas, kShareLeft, below, kScanHint, kLegend565);
    below += kLineHeight + kShareCodeGap;
  }
  const int beside_x = kShareLeft + side + kShareTextGap;
  const Flow flows[kFlowCount] = {
      {beside_x, kShareTop, (kWidth - kMargin - beside_x) / kGlyphAdvance, side / kLineHeight},
      {kShareLeft, below, kShareColumns, below >= bottom ? 0 : (bottom - below) / kLineHeight},
  };
  Row rows[kMaxRows];
  bool complete = true;
  const int count = lay_out(model.code, flows, rows, complete);
  if (!complete && count > 0) mark_incomplete(rows[count - 1]);
  for (int i = 0; i < count; ++i) draw_text(canvas, rows[i].x, rows[i].y, rows[i].text, kText);
  if (!complete && count == 0) draw_text(canvas, beside_x, kShareTop, kEllipsis, kText);
}

}  // namespace ui
