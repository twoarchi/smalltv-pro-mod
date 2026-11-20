#include "TickerMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "Net.h"
#include "StockClient.h"

TickerMode g_tickerMode(0, MODE_STOCKS, "stocks");
TickerMode g_tickerMode2(1, MODE_STOCKS2, "stocks2");

// ---- sparkline ------------------------------------------------------------
// livePt (NAN = none) is appended as the newest point so the chart ends at the
// live price rather than the last historical close.
// ---- candlestick chart ---------------------------------------------------
static void drawCandlestick(Arduino_GFX* gfx, const StockData& d,
                            int top, int bottom, uint16_t upC, uint16_t downC) {
  if (d.candleCount < 2) return;
  uint8_t n = d.candleCount;

  float mn = d.candles[0].low, mx = d.candles[0].high;
  for (uint8_t i = 0; i < n; i++) {
    if (d.candles[i].low < mn)  mn = d.candles[i].low;
    if (d.candles[i].high > mx) mx = d.candles[i].high;
  }
  if (d.prevClose > 0) {
    if (d.prevClose < mn) mn = d.prevClose;
    if (d.prevClose > mx) mx = d.prevClose;
  }
  float span = mx - mn;

  // 위아래 마진(8%) 및 초단기 최소 변동폭(주가의 0.8%) 확보 -> 분봉에서 1호가만 움직여도 과도하게 확대 왜곡되는 현상 방지
  float pad = span * 0.08f;
  float minSpan = (d.price > 0) ? (d.price * 0.008f) : 1.0f;
  if (span < minSpan) {
    float extra = (minSpan - span) / 2.0f;
    mn -= extra;
    mx += extra;
    span = mx - mn;
  } else {
    mn -= pad;
    mx += pad;
    span = mx - mn;
  }
  if (span <= 0) span = 1;

  // 위아래 여백을 2px씩 두어 최고가/최저가 봉이 차트 영역을 벗어나지 않도록 방지
  top += 2;
  bottom -= 2;

  const int padL = 4, padR = 4;
  int plotW = TFT_WIDTH - padL - padR;
  int plotH = bottom - top;

  // 1. 날짜 구분선 (수직 점선)
  if (d.dateSplitCount > 0) {
    for (uint8_t s = 0; s < d.dateSplitCount; s++) {
      int splitIdx = d.dateSplits[s];
      if (splitIdx > 0 && splitIdx < n) {
        int splitX = padL + (int)((long)plotW * splitIdx / n);
        for (int vy = top; vy <= bottom; vy += 6) {
          gfx->drawFastVLine(splitX, vy, 3, C_GRAY);
        }
      }
    }
  } else if (d.dateSplitIdx > 0 && d.dateSplitIdx < n) {
    int splitX = padL + (int)((long)plotW * d.dateSplitIdx / n);
    for (int vy = top; vy <= bottom; vy += 6) {
      gfx->drawFastVLine(splitX, vy, 3, C_GRAY);
    }
  }

  // 2. 어제 종가 가격선 (수평 점선)
  if (d.prevClose > 0 && d.prevClose >= mn && d.prevClose <= mx) {
    int prevCloseY = bottom - (int)((d.prevClose - mn) / span * plotH);
    for (int hx = padL; hx <= TFT_WIDTH - padR; hx += 6) {
      gfx->drawFastHLine(hx, prevCloseY, 3, C_DGRAY);
    }
  }

  // 3. 봉 그리기 (화면 전체에 균등하게 펼침)
  float candleSlot = (float)plotW / n;
  int bw = (int)(candleSlot * 0.65f);
  if (bw < 2) bw = 2;   // 최소 2px 몸통
  if (bw > 22) bw = 22; // 봉 개수가 적을 때도 시원하고 듬직하게 보이도록 상한 조정

  for (uint8_t i = 0; i < n; i++) {
    const auto& c = d.candles[i];
    int cx = padL + (int)(candleSlot * i + candleSlot / 2.0f);

    int yHigh  = bottom - (int)((c.high - mn) / span * plotH);
    int yLow   = bottom - (int)((c.low - mn) / span * plotH);
    int yOpen  = bottom - (int)((c.open - mn) / span * plotH);
    int yClose = bottom - (int)((c.close - mn) / span * plotH);

    // 좌표 경계 클램핑
    if (yHigh < top) yHigh = top;
    if (yLow > bottom) yLow = bottom;

    bool isUp = (c.close >= c.open);
    uint16_t cColor = isUp ? upC : downC;

    // 꼬리선 (High - Low)
    int wickLen = yLow - yHigh + 1;
    if (wickLen < 1) wickLen = 1;
    gfx->drawFastVLine(cx, yHigh, wickLen, cColor);

    // 몸통 (Open - Close)
    int bodyTop = isUp ? yClose : yOpen;
    int bodyH = abs(yClose - yOpen) + 1;
    if (bodyH < 2) bodyH = 2; // 십자선 도지(Doji)일 때도 최소 2px 그려 보이도록
    int bx = cx - bw / 2;
    gfx->fillRect(bx, bodyTop, bw, bodyH, cColor);
  }
}

static void drawSparkline(Arduino_GFX* gfx, const StockData& d,
                          int top, int bottom, uint16_t color, float livePt) {
  if (d.sparkCount < 2) return;
  bool live = !isnan(livePt);
  uint8_t n = d.sparkCount + (live ? 1 : 0);
  float mn = d.spark[0], mx = d.spark[0];
  for (uint8_t i = 1; i < d.sparkCount; i++) {
    if (d.spark[i] < mn) mn = d.spark[i];
    if (d.spark[i] > mx) mx = d.spark[i];
  }
  if (live) {
    if (livePt < mn) mn = livePt;
    if (livePt > mx) mx = livePt;
  }
  if (d.prevClose > 0) {
    if (d.prevClose < mn) mn = d.prevClose;
    if (d.prevClose > mx) mx = d.prevClose;
  }
  float span = mx - mn;
  if (span <= 0) span = 1;

  const int padL = 6, padR = 6;
  int plotW = TFT_WIDTH - padL - padR;
  int plotH = bottom - top;

  // 1. 날짜 구분선 (수직 점선)
  if (d.dateSplitCount > 0) {
    for (uint8_t s = 0; s < d.dateSplitCount; s++) {
      int splitIdx = d.dateSplits[s];
      if (splitIdx > 0 && splitIdx < n) {
        int splitX = padL + (int)((long)plotW * splitIdx / (n - 1));
        for (int vy = top; vy <= bottom; vy += 6) {
          gfx->drawFastVLine(splitX, vy, 3, C_GRAY);
        }
      }
    }
  } else if (d.dateSplitIdx > 0 && d.dateSplitIdx < n) {
    int splitX = padL + (int)((long)plotW * d.dateSplitIdx / (n - 1));
    for (int vy = top; vy <= bottom; vy += 6) {
      gfx->drawFastVLine(splitX, vy, 3, C_GRAY);
    }
  }

  // 2. 어제 종가 가격선 (수평 점선)
  if (d.prevClose > 0 && d.prevClose >= mn && d.prevClose <= mx) {
    int prevCloseY = bottom - (int)((d.prevClose - mn) / span * plotH);
    for (int hx = padL; hx <= TFT_WIDTH - padR; hx += 6) {
      gfx->drawFastHLine(hx, prevCloseY, 3, C_DGRAY);
    }
  }

  int prevX = 0, prevY = 0;
  for (uint8_t i = 0; i < n; i++) {
    float v = (i < d.sparkCount) ? d.spark[i] : livePt;
    int x = padL + (int)((long)plotW * i / (n - 1));
    int y = bottom - (int)((v - mn) / span * plotH);
    if (i > 0) {
      gfx->drawLine(prevX, prevY, x, y, color);
      gfx->drawLine(prevX, prevY + 1, x, y + 1, color); // 2px thick
    }
    prevX = x;
    prevY = y;
  }
}

// ---- number formatting ----------------------------------------------------
static void fmtPrice(float v, char* out, size_t n) {
  float a = fabsf(v);
  if (a >= 1000) {
    long whole = (long)roundf(v);
    if (fabsf(v - (float)whole) < 0.001f) {
      char raw[24];
      snprintf(raw, sizeof(raw), "%ld", whole);
      int len = strlen(raw);
      int commas = (len > 0) ? (len - 1) / 3 : 0;
      if (len + commas >= (int)n) {
        snprintf(out, n, "%ld", whole);
        return;
      }
      int outIdx = len + commas;
      out[outIdx--] = 0;
      int count = 0;
      for (int i = len - 1; i >= 0; i--) {
        out[outIdx--] = raw[i];
        count++;
        if (count == 3 && i > 0) {
          out[outIdx--] = ',';
          count = 0;
        }
      }
      return;
    } else {
      snprintf(out, n, "%.2f", v);
    }
  }
  else if (a >= 1)    snprintf(out, n, "%.2f", v);
  else if (a >= 0.01) snprintf(out, n, "%.4f", v);
  else                snprintf(out, n, "%.6f", v);
}

// ---- one ticker page ------------------------------------------------------
static void drawStock(const StockData& d, uint8_t pageIndex, uint8_t pageCount,
                      const Settings& s, const TickerSettings& ts) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  // No data yet for this symbol.
  if (!d.valid) {
    gfxDrawCentered(d.symbol[0] ? d.symbol : "----", 80, 3, C_WHITE);
    gfxDrawCentered(d.error ? "fetch error" : "loading...", 120, 2, C_GRAY);
    if (ts.showPageDots) {
      int total = pageCount * 10 - 4;
      int x0 = (TFT_WIDTH - total) / 2;
      for (uint8_t i = 0; i < pageCount; i++)
        gfx->fillCircle(x0 + i * 10 + 2, 230, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    }
    return;
  }

  // Trend color — from the displayed change (chart-range or 1-day basis)
  float chg = 0, pct = 0;
  bool onRange = false;
  bool hasChange = stockDisplayChange(d, ts, chg, pct, &onRange);
  bool up = hasChange ? (chg >= 0) : true;
  uint16_t upC   = ts.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = ts.colorInverted ? C_GREEN : C_RED;
  uint16_t trendC = !hasChange ? C_WHITE : (up ? upC : downC);

  int y = 6;

  // Page dots (top)
  if (ts.showPageDots) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 2, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 14;
  }

  // Format price string in advance to compute synchronized font size
  char num[20];
  fmtPrice(d.price, num, sizeof(num));
  char priceLine[28];
  bool isKr = (strstr(d.currency, "KRW") != nullptr);
  if (isKr) {
    snprintf(priceLine, sizeof(priceLine), "W%s", num);
  } else {
    snprintf(priceLine, sizeof(priceLine), "%s%s", d.currency, num);
  }

  const char* label = d.name[0] ? d.name : d.symbol;

  // Synchronized font size for both Name & Price (max size 3, auto-fit together)
  uint8_t szName = gfxFitSize(label, 226, 3);
  uint8_t szPrice = gfxFitSize(priceLine, 226, 3);
  uint8_t commonSz = min(szName, szPrice);
  if (commonSz < 2) commonSz = 2;

  // 1. Name / symbol (Highlighted in distinct cyan color, tightened line spacing)
  if (ts.showName) {
    gfxDrawCentered(label, y, commonSz, C_CYAN);
    y += (8 * commonSz) + 3; // tight spacing between name and price
  }

  // 2. Price (same font size as name, crisp white with official ₩ won symbol)
  if (ts.showPrice) {
    int totalW = gfxTextW(priceLine, commonSz);
    int px = (TFT_WIDTH - totalW) / 2;
    if (px < 0) px = 0;
    gfxDrawCentered(priceLine, y, commonSz, C_WHITE);
    if (isKr) {
      // Overlay horizontal crossbar onto 'W' to form the official '₩' symbol!
      int wonBarY = y + (int)(3.5f * commonSz);
      int barH = (commonSz >= 3) ? 2 : 1;
      int barX = px - 1;
      int barW = 5 * commonSz + 2;
      gfx->fillRect(barX, wonBarY, barW, barH, C_WHITE);
    }
    y += (8 * commonSz) + 5;
  }

  // 3. Change line: [arrow] +chg (+pct%) with enlarged font size for better readability
  if (ts.showChange && hasChange) {
    char line[40];
    if (fabsf(chg) >= 1000 && fabsf(chg - roundf(chg)) < 0.001f) {
      char chgStr[20];
      fmtPrice(fabsf(chg), chgStr, sizeof(chgStr));
      snprintf(line, sizeof(line), "%s%s (%+.2f%%)", chg >= 0 ? "+" : "-", chgStr, pct);
    } else {
      snprintf(line, sizeof(line), "%+.2f (%+.2f%%)", chg, pct);
    }
    // Allow up to size 3 for enhanced legibility
    uint8_t sz = gfxFitSize(line, 220, 3);
    int tw = gfxTextW(line, sz);
    int ah = 8 * sz;
    int aw = ah;
    int totalW = aw + 4 + tw;
    int x = (TFT_WIDTH - totalW) / 2;
    if (x < 2) x = 2;
    int ax = x, ay = y;
    if (up)
      gfx->fillTriangle(ax, ay + ah, ax + aw, ay + ah, ax + aw / 2, ay, trendC);
    else
      gfx->fillTriangle(ax, ay, ax + aw, ay, ax + aw / 2, ay + ah, trendC);
    gfx->setTextSize(sz);
    gfx->setTextColor(trendC);
    gfx->setCursor(x + aw + 4, ay);
    gfx->print(line);
    y = ay + ah + 6;
  }

  // Position P/L vs the cost basis
  if (ts.showPortfolio && d.qty > 0 && d.cost > 0) {
    float plPct = (d.price / d.cost - 1.0f) * 100.0f;
    char pl[40];
    snprintf(pl, sizeof(pl), "P/L %+.0f (%+.1f%%)", (d.price - d.cost) * d.qty, plPct);
    uint8_t sz = gfxFitSize(pl, 220, 2);
    gfxDrawCentered(pl, y, sz, plPct >= 0 ? upC : downC);
    y += 8 * sz + 4;
  }

  // Chart (takes remaining height cleanly)
  if (ts.showChart && d.sparkCount >= 2) {
    int top = y + 2;
    if (top < 112) top = 112;
    int bottom = 230;
    float livePt = onRange ? d.price : NAN;
    if (top < bottom - 10) {
      if (ts.chartStyle == "candle" && d.candleCount >= 2) {
        drawCandlestick(gfx, d, top, bottom, upC, downC);
      } else {
        drawSparkline(gfx, d, top, bottom, trendC, livePt);
      }
    }
  }

  // Range label
  if (ts.showRangeLabel && d.rangeLabel[0]) {
    int sz = 2;
    int tw = gfxTextW(d.rangeLabel, sz);
    gfx->setTextSize(sz);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - tw - 4, 4);
    gfx->print(d.rangeLabel);
  }

  // Updated-ago
  if (ts.showUpdatedAgo && d.lastOkMs) {
    uint32_t ago = (millis() - d.lastOkMs) / 1000;
    char buf[12];
    if (ago < 100) snprintf(buf, sizeof(buf), "%lus", (unsigned long)ago);
    else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(ago / 60));
    gfx->setTextSize(2);
    gfx->setTextColor(d.error ? C_RED : C_DGRAY);
    gfx->setCursor(4, 224);
    gfx->print(buf);
  }

  if (d.error) gfx->fillCircle(6, 6, 3, C_RED);
}

// ---- portfolio page --------------------------------------------------------
static void fmtVal(float v, char* out, size_t n) {
  float a = fabsf(v);
  if      (a >= 1000000) snprintf(out, n, "%.2fM", v / 1000000);
  else if (a >= 10000)   snprintf(out, n, "%.1fk", v / 1000);
  else if (a >= 100)     snprintf(out, n, "%.0f", v);
  else                   snprintf(out, n, "%.2f", v);
}

static bool hasPortfolioPage(const Settings& s, const TickerSettings& ts, uint8_t setIdx) {
  if (!ts.showPortfolio) return false;
  for (uint8_t i = 0; i < stocksCount(setIdx); i++)
    if (stockAt(i, setIdx).qty > 0) return true;
  return false;
}

static void drawPortfolio(uint8_t pageIndex, uint8_t pageCount, const Settings& s, const TickerSettings& ts, uint8_t setIdx) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  int y = 6;
  if (ts.showPageDots) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 3, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 20;
  }

  gfxDrawCentered("Portfolio", y, 3, C_WHITE);
  y += 32;

  uint16_t upC   = ts.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = ts.colorInverted ? C_GREEN : C_RED;

  float totV[3] = {0, 0, 0}, totC[3] = {0, 0, 0};
  char  totCur[3][6];
  uint8_t curN = 0;

  uint8_t n = stocksCount(setIdx);
  for (uint8_t i = 0; i < n; i++) {
    const StockData& d = stockAt(i, setIdx);
    if (!d.valid || d.qty <= 0 || d.cost <= 0) continue;
    float val = d.price * d.qty;
    float cost = d.cost * d.qty;
    float plPct = (cost > 0) ? (val / cost - 1.0f) * 100.0f : 0;

    int b = -1;
    for (uint8_t k = 0; k < curN; k++)
      if (!strcmp(totCur[k], d.currency)) { b = k; break; }
    if (b < 0 && curN < 3) {
      b = curN++;
      strlcpy(totCur[b], d.currency, sizeof(totCur[b]));
    }
    if (b >= 0) { totV[b] += val; totC[b] += cost; }

    if (y <= 170) {
      const char* lbl = d.name[0] ? d.name : d.symbol;
      char pl[10];
      snprintf(pl, sizeof(pl), "%+.0f%%", plPct);
      char num[12];
      fmtVal(val, num, sizeof(num));
      char vstr[20];
      snprintf(vstr, sizeof(vstr), "%s%s", d.currency, num);

      gfx->setTextSize(2);
      gfx->setTextColor(C_WHITE);
      gfx->setCursor(4, y);
      gfx->print(lbl);

      gfx->setTextColor(plPct >= 0 ? upC : downC);
      gfx->setCursor(110, y);
      gfx->print(pl);

      gfx->setTextColor(C_WHITE);
      int vw = gfxTextW(vstr, 2);
      gfx->setCursor(TFT_WIDTH - vw - 4, y);
      gfx->print(vstr);
      y += 20;
    }
  }

  y += 4;
  gfx->drawFastHLine(4, y, TFT_WIDTH - 8, C_DGRAY);
  y += 6;
  for (uint8_t k = 0; k < curN && y <= 214; k++) {
    char vbuf[12];
    fmtVal(totV[k], vbuf, sizeof(vbuf));
    char line[20];
    snprintf(line, sizeof(line), "%s%s", totCur[k], vbuf);
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(4, y);
    gfx->print("Total");
    gfx->setCursor(TFT_WIDTH - gfxTextW(line, 2) - 4, y);
    gfx->print(line);
    if (totC[k] > 0) {
      float plPct = (totV[k] / totC[k] - 1.0f) * 100.0f;
      char pbuf[10];
      snprintf(pbuf, sizeof(pbuf), "%+.1f%%", plPct);
      gfx->setTextColor(plPct >= 0 ? upC : downC);
      gfx->setCursor(78, y);
      gfx->print(pbuf);
    }
    y += 18;
  }
}

// ---- DisplayMode ----------------------------------------------------------
void TickerMode::begin(const Settings& s) {
  stocksInit(s, setIdx_);
  curPage_ = 0;
  lastRotate_ = millis();
  renderedLastOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
}

void TickerMode::invalidate(const Settings& s) {
  stocksInit(s, setIdx_);
  stocksForceRefresh(setIdx_);
  renderedLastOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void TickerMode::wake(const Settings& s) {
  needRender_ = true;
  lastRotate_ = millis();
  // Defer next network fetch by 800ms so rapid button tapping won't trigger blocking fetches
  stocksDeferNextFetch(setIdx_, 800);
  render(s);
  needRender_ = false;
}

void TickerMode::render(const Settings& s) {
  const TickerSettings& ts = (setIdx_ == 1) ? s.ticker2 : s.ticker;
  uint8_t n = stocksCount(setIdx_);
  if (n == 0) {
    const char* noMsg = (setIdx_ == 1) ? "No tickers (2)" : "No tickers (1)";
    gfxMessage(noMsg, netIP().c_str(), C_YELLOW);
    return;
  }

  bool allWebhook = true;
  for (uint8_t i = 0; i < n; i++)
    if (stockAt(i, setIdx_).source != SRC_WEBHOOK) { allWebhook = false; break; }
  if (allWebhook && ts.webhookUrl.length() < 8) {
    gfxMessage("Set webhook", netIP().c_str(), C_YELLOW);
    return;
  }

  uint8_t pages = n + (hasPortfolioPage(s, ts, setIdx_) ? 1 : 0);
  if (curPage_ >= pages) curPage_ = 0;
  if (curPage_ >= n) drawPortfolio(curPage_, pages, s, ts, setIdx_);
  else               drawStock(stockAt(curPage_, setIdx_), curPage_, pages, s, ts);
}

void TickerMode::service(const Settings& s) {
  // 1. Render immediately if pending, BEFORE running any blocking network operations
  if (needRender_) {
    needRender_ = false;
    render(s);
  }

  // 2. Background network service
  stocksService(s);

  // 3. Page rotation & refresh tracking
  const TickerSettings& ts = (setIdx_ == 1) ? s.ticker2 : s.ticker;
  uint8_t n = stocksCount(setIdx_);
  uint8_t pages = n + (hasPortfolioPage(s, ts, setIdx_) ? 1 : 0);

  if (pages > 1 && millis() - lastRotate_ >= (uint32_t)ts.rotateSec * 1000UL) {
    curPage_ = (curPage_ + 1) % pages;
    lastRotate_ = millis();
    needRender_ = true;
  }

  if (n > 0 && curPage_ < n) {
    const StockData& d = stockAt(curPage_, setIdx_);
    if (d.lastOkMs != renderedLastOk_ || d.error != renderedError_) {
      needRender_ = true;
      renderedLastOk_ = d.lastOkMs;
      renderedError_ = d.error;
    }
  } else if (n > 0 && pages > n) {
    uint32_t h = 0;
    bool err = false;
    for (uint8_t i = 0; i < n; i++) {
      h += stockAt(i, setIdx_).lastOkMs;
      err |= stockAt(i, setIdx_).error;
    }
    if (h != renderedLastOk_ || err != renderedError_) {
      needRender_ = true;
      renderedLastOk_ = h;
      renderedError_ = err;
    }
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}
