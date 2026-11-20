#include "StockClient.h"
#include "Platform.h"
#include "../../WebPortal.h"
#include <ArduinoJson.h>
#include <math.h>

struct StockSet {
  StockData stocks[MAX_SYMBOLS];
  uint8_t   count = 0;
  bool      refreshing = false;
  uint8_t   fetchIdx = 0;
  uint8_t   fetchPhase = 0;
};

static StockSet g_sets[NUM_STOCK_SETS];
static uint8_t  g_serviceSet = 0;

// ---------------------------------------------------------------------------
void stocksInit(const Settings& s, uint8_t set) {
  if (set >= NUM_STOCK_SETS) return;
  const TickerSettings& ts = (set == 1) ? s.ticker2 : s.ticker;
  StockSet& ss = g_sets[set];
  ss.count = ts.symbolCount;
  for (uint8_t i = 0; i < ss.count; i++) {
    ss.stocks[i].clear();
    strlcpy(ss.stocks[i].symbol, ts.symbols[i].symbol, MAX_SYMBOL_LEN);
    ss.stocks[i].source = ts.symbols[i].source;
    ss.stocks[i].qty  = ts.symbols[i].qty;
    ss.stocks[i].cost = ts.symbols[i].cost;
    ss.stocks[i].userNamed = (ts.symbols[i].name[0] != 0);
    strlcpy(ss.stocks[i].name,
            ss.stocks[i].userNamed ? ts.symbols[i].name : ts.symbols[i].symbol,
            MAX_NAME_LEN);
    ss.stocks[i].nextTryMs = millis();     // every symbol is due right away
  }
  ss.refreshing = false;
}

void stocksForceRefresh(uint8_t set) {
  if (set >= NUM_STOCK_SETS) return;
  StockSet& ss = g_sets[set];
  uint32_t now = millis();
  for (uint8_t i = 0; i < ss.count; i++) {
    ss.stocks[i].nextTryMs = now;
    ss.stocks[i].fails = 0;
  }
  ss.refreshing = false;
}

void stocksForceRefreshAll() {
  for (uint8_t i = 0; i < NUM_STOCK_SETS; i++) stocksForceRefresh(i);
}

void stocksDeferNextFetch(uint8_t set, uint32_t delayMs) {
  if (set >= NUM_STOCK_SETS) return;
  StockSet& ss = g_sets[set];
  uint32_t target = millis() + delayMs;
  for (uint8_t i = 0; i < ss.count; i++) {
    if ((int32_t)(target - ss.stocks[i].nextTryMs) > 0) {
      ss.stocks[i].nextTryMs = target;
    }
  }
  ss.refreshing = false;
}

uint8_t stocksCount(uint8_t set) {
  return (set < NUM_STOCK_SETS) ? g_sets[set].count : 0;
}

const StockData& stockAt(uint8_t i, uint8_t set) {
  static StockData dummy;
  if (set >= NUM_STOCK_SETS || i >= g_sets[set].count) return dummy;
  return g_sets[set].stocks[i];
}

uint32_t stockRetryInSec(uint8_t i, uint8_t set) {
  if (set >= NUM_STOCK_SETS || i >= g_sets[set].count) return 0;
  int32_t left = (int32_t)(g_sets[set].stocks[i].nextTryMs - millis());
  return left > 0 ? (uint32_t)left / 1000UL : 0;
}

bool stocksAnyValid(uint8_t set) {
  if (set >= NUM_STOCK_SETS) return false;
  for (uint8_t i = 0; i < g_sets[set].count; i++)
    if (g_sets[set].stocks[i].valid) return true;
  return false;
}

bool stockDisplayChange(const StockData& d, const TickerSettings& t,
                        float& chg, float& pct, bool* onRange) {
  if (t.changeOnRange && d.sparkCount >= 1 && d.spark[0] > 0 && d.price > 0) {
    chg = d.price - d.spark[0];
    pct = chg / d.spark[0] * 100.0f;
    if (onRange) *onRange = true;
    return true;
  }
  if (onRange) *onRange = false;
  chg = d.change;
  pct = d.changePct;
  return d.hasChange;
}

// ---- Korean Stock detection & helpers -------------------------------------
static bool isKoreanCode(const char* sym, char* outCode = nullptr, size_t outSize = 0) {
  if (!sym) return false;
  while (*sym == ' ') sym++;
  char clean[16];
  strlcpy(clean, sym, sizeof(clean));
  char* dot = strchr(clean, '.');
  if (dot) *dot = 0; // strip .KS, .KQ, .KR, etc.

  size_t len = strlen(clean);
  if (len != 6) return false;
  for (size_t i = 0; i < 6; i++) {
    if (!isalnum((unsigned char)clean[i])) return false;
  }
  if (outCode && outSize >= 7) {
    strlcpy(outCode, clean, outSize);
  }
  return true;
}

static const char* lookupKoreanName(const char* code) {
  if (!code) return nullptr;
  if (!strcmp(code, "005930")) return "Samsung Elec";
  if (!strcmp(code, "000660")) return "SK hynix";
  if (!strcmp(code, "373220")) return "LG Energy";
  if (!strcmp(code, "207940")) return "Samsung Bio";
  if (!strcmp(code, "005380")) return "Hyundai Motor";
  if (!strcmp(code, "068270")) return "Celltrion";
  if (!strcmp(code, "000270")) return "Kia";
  if (!strcmp(code, "005490")) return "POSCO";
  if (!strcmp(code, "035420")) return "NAVER";
  if (!strcmp(code, "035720")) return "Kakao";
  if (!strcmp(code, "105560")) return "KB Financial";
  if (!strcmp(code, "055550")) return "Shinhan Fin";
  if (!strcmp(code, "012330")) return "Hyundai Mobis";
  if (!strcmp(code, "028260")) return "Samsung C&T";
  if (!strcmp(code, "032830")) return "Samsung Life";
  if (!strcmp(code, "051910")) return "LG Chem";
  if (!strcmp(code, "006400")) return "Samsung SDI";
  if (!strcmp(code, "122630")) return "KODEX 2X";
  if (!strcmp(code, "0195S0")) return "TIGER Hynix2X";
  if (!strcmp(code, "0195R0")) return "TIGER Sam2X";
  return nullptr;
}

// ---- URL helpers ----------------------------------------------------------
static String urlEncode(const char* src) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (const char* p = src; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static String buildWebhookUrl(const TickerSettings& t, const char* symbol) {
  String url = t.webhookUrl;
  char sep = (url.indexOf('?') >= 0) ? '&' : '?';
  url += sep;
  url += "symbol=" + urlEncode(symbol);
  url += "&range=" + urlEncode(t.range.c_str());
  url += "&points=" + String(t.points);
  return url;
}

static const char* yahooInterval(const String& r) {
  if (r == "1m")  return "1m";
  if (r == "5m")  return "5m";
  if (r == "30m") return "30m";
  if (r == "60m") return "60m";
  if (r == "1d")  return "5m";
  if (r == "5d")  return "30m";
  if (r == "1mo" || r == "3mo" || r == "6mo" || r == "ytd") return "1d";
  if (r == "1y"  || r == "2y")  return "1wk";
  if (r == "5y"  || r == "10y" || r == "max") return "1mo";
  return "1d";
}

static const char* yahooRange(const String& r) {
  if (r == "1m" || r == "5m") return "1d";
  if (r == "30m") return "5d";
  if (r == "60m") return "1mo";
  return r.c_str();
}

static String buildYahooUrl(const TickerSettings& t, const char* host, const char* symbol) {
  String range = t.range;
  range.toLowerCase();
  if (range.length() == 0) range = "1d";
  String url = F("https://");
  url += host;
  url += F(YAHOO_CHART_PATH);
  url += urlEncode(symbol);
  url += F("?range=");
  url += yahooRange(range);
  url += F("&interval=");
  url += yahooInterval(range);
  return url;
}

static void yahooCurrency(const char* code, char* out, size_t n) {
  if (!code || !code[0]) { out[0] = 0; return; }
  if (!strcmp(code, "USD")) { strlcpy(out, "$", n); return; }
  snprintf(out, n, "%s ", code);
}

static uint16_t cashRangeDays(const String& r) {
  if (r == "1d")  return 2;
  if (r == "5d")  return 5;
  if (r == "1mo") return 22;
  if (r == "3mo") return 65;
  if (r == "6mo" || r == "ytd") return 130;
  if (r == "1y")  return 260;
  return 400;
}

static String buildCashUrl(const String& query) {
  String url = F("https://" CASH_GQL_HOST CASH_GQL_PATH "?query=");
  url += urlEncode(query.c_str());
  return url;
}

static String buildCashQuoteUrl(const char* symbol) {
  String q = F("query{quoteList(listingKeys:\"");
  q += symbol;
  q += F("\"){quoteList{edges{node{...on Instrument{"
         "lval iNetVperprV perfPercentage mCur mShortName}}}}}}");
  return buildCashUrl(q);
}

static String buildCashChartUrl(const TickerSettings& t, const char* symbol) {
  String q = F("query{integration{solid{chart(listingKey:\"");
  q += symbol;
  q += F("\" frequency:\"1d\" from:\"2020-01-01\" to:\"2100-01-01\" max:\"");
  q += String(cashRangeDays(t.range));
  q += F("\"){timeserie{prices{close}}}}}}");
  return buildCashUrl(q);
}

static String buildGithubUrl(const char* symbol) {
  String url = F(GH_QUOTES_BASE);
  url += urlEncode(symbol);
  url += F(".json");
  return url;
}

static const char* naverTimeframe(const String& r) {
  if (r == "1m" || r == "5m" || r == "30m" || r == "60m") return "minute";
  if (r == "1w" || r == "1wk" || r == "week") return "week";
  if (r == "1mo" || r == "3mo" || r == "6mo" || r == "1y" || r == "month") return "month";
  return "day";
}

static uint16_t naverCount(const String& r) {
  if (r == "1m")  return 25;
  if (r == "5m")  return 35;
  if (r == "30m") return 45;
  if (r == "60m") return 60;
  if (r == "1d")  return 20;
  if (r == "5d")  return 5;
  if (r == "1mo") return 20;
  if (r == "3mo") return 60;
  if (r == "6mo") return 120;
  if (r == "1y")  return 240;
  return 25;
}


static bool fetchNaverForeignMinute(const Settings& s, const TickerSettings& ts, const char* symbol, StockData& d) {
  String url = F("https://api.stock.naver.com/chart/foreign/item/");
  url += symbol;
  url += F("?periodType=day");

  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048, nullptr, 512, true));
  if (!client) return false;

  HTTPClient http;
  http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK || g_abortFetch) {
    http.end();
    return false;
  }

  WiFiClient* stream = (WiFiClient*)http.getStreamPtr();
  if (!stream) {
    http.end();
    return false;
  }

  uint8_t targetPoints = (ts.points >= 5 && ts.points <= MAX_SPARK_POINTS) ? ts.points : 30;
  int interval = 1;
  if (ts.range == "5m") interval = 5;
  else if (ts.range == "30m") interval = 30;
  else if (ts.range == "60m") interval = 60;

  StockData::Candle todayRing[MAX_SPARK_POINTS];
  uint8_t todayCount = 0;
  StockData::Candle yestRing[MAX_SPARK_POINTS];
  uint8_t yestCount = 0;

  float curGroupOpen = 0, curGroupHigh = -1e9f, curGroupLow = 1e9f, curGroupClose = 0;
  int groupCount = 0;
  bool inYesterday = false;

  char buf[256];
  uint32_t lastPoll = millis();
  uint32_t loopStart = millis();
  stream->setTimeout(150);

  while (http.connected() || stream->available()) {
    if (g_abortFetch || (millis() - loopStart > 3500)) {
      break;
    }
    if (millis() - lastPoll > 30) {
      buttonPoll();
      webPortalLoop();
      yield();
      lastPoll = millis();
      if (g_abortFetch) break;
    }

    int len = stream->readBytesUntil('{', buf, sizeof(buf) - 1);
    if (len <= 0 && !stream->available()) break;
    if (len > 0) {
      buf[len] = 0;
      if (strchr(buf, ']')) break;
    } else {
      buf[0] = 0;
    }

    // Detect switch from priceInfos (today) to lastPriceInfos (yesterday)
    if (!inYesterday && strstr(buf, "lastPriceInfos") != nullptr) {
      if (groupCount > 0) {
        StockData::Candle c = {curGroupOpen, curGroupHigh, curGroupLow, curGroupClose};
        if (todayCount < MAX_SPARK_POINTS) todayRing[todayCount++] = c;
      }
      inYesterday = true;
      curGroupOpen = curGroupClose = 0;
      curGroupHigh = -1e9f;
      curGroupLow = 1e9f;
      groupCount = 0;
    }

    int objLen = stream->readBytesUntil('}', buf, sizeof(buf) - 1);
    if (objLen <= 0 && !stream->available()) break;
    if (objLen <= 0) continue;
    buf[objLen] = 0;

    char* pCur = strstr(buf, "\"currentPrice\":");
    if (!pCur) pCur = strstr(buf, "\"closePrice\":");
    if (!pCur) continue;

    float cl = atof(pCur + (strstr(pCur, "\"currentPrice\":") ? 15 : 13));
    if (cl <= 0) continue;

    char* pOpen = strstr(buf, "\"openPrice\":");
    char* pHigh = strstr(buf, "\"highPrice\":");
    char* pLow  = strstr(buf, "\"lowPrice\":");

    float op = pOpen ? atof(pOpen + 12) : cl;
    float hi = pHigh ? atof(pHigh + 12) : cl;
    float lo = pLow  ? atof(pLow + 11)  : cl;

    if (groupCount == 0) curGroupOpen = op;
    if (hi > curGroupHigh) curGroupHigh = hi;
    if (lo < curGroupLow)  curGroupLow = lo;
    curGroupClose = cl;
    groupCount++;

    if (groupCount >= interval) {
      StockData::Candle c = {curGroupOpen, curGroupHigh, curGroupLow, curGroupClose};
      if (inYesterday) {
        if (yestCount < MAX_SPARK_POINTS) yestRing[yestCount++] = c;
      } else {
        if (todayCount < MAX_SPARK_POINTS) todayRing[todayCount++] = c;
      }
      groupCount = 0;
      curGroupHigh = -1e9f;
      curGroupLow = 1e9f;
    }
  }
  http.end();

  if (groupCount > 0) {
    StockData::Candle c = {curGroupOpen, curGroupHigh, curGroupLow, curGroupClose};
    if (inYesterday) {
      if (yestCount < MAX_SPARK_POINTS) yestRing[yestCount++] = c;
    } else {
      if (todayCount < MAX_SPARK_POINTS) todayRing[todayCount++] = c;
    }
  }

  // Combine: targetPoints
  uint8_t todayTaken = (todayCount > targetPoints) ? targetPoints : todayCount;
  uint8_t remYest = targetPoints - todayTaken;
  uint8_t yestTaken = (yestCount > remYest) ? remYest : yestCount;

  d.sparkCount = 0;
  d.candleCount = 0;
  d.dateSplitIdx = -1;

  // Add yesterday candles (tail of yestRing)
  if (yestTaken > 0) {
    uint8_t startYest = yestCount - yestTaken;
    for (uint8_t i = startYest; i < yestCount; i++) {
      d.candles[d.candleCount++] = yestRing[i];
      d.spark[d.sparkCount++] = yestRing[i].close;
    }
  }

  // Date boundary line: drawn precisely where yesterday ends and today's session begins
  if (yestTaken > 0 && todayTaken > 0) {
    d.dateSplitIdx = d.candleCount;
    d.dateSplitCount = 1;
    d.dateSplits[0] = (int8_t)d.candleCount;
  }

  // Add today candles (tail of todayRing)
  if (todayTaken > 0) {
    uint8_t startToday = todayCount - todayTaken;
    for (uint8_t i = startToday; i < todayCount; i++) {
      d.candles[d.candleCount++] = todayRing[i];
      d.spark[d.sparkCount++] = todayRing[i].close;
    }
  }

  // Update latest point with live price
  if (d.sparkCount > 0) {
    d.spark[d.sparkCount - 1] = d.price;
    if (d.candleCount > 0) {
      d.candles[d.candleCount - 1].close = d.price;
      if (d.price > d.candles[d.candleCount - 1].high) d.candles[d.candleCount - 1].high = d.price;
      if (d.price < d.candles[d.candleCount - 1].low)  d.candles[d.candleCount - 1].low = d.price;
    }
  }

  return (d.candleCount >= 2);
}

static bool fetchNaverForeign(const Settings& s, const TickerSettings& ts, const char* symbol, StockData& d) {
  if (g_abortFetch) return false;

  // 1. Basic quote
  String quoteUrl = F("https://api.stock.naver.com/stock/");
  quoteUrl += symbol;
  quoteUrl += F("/basic");

  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048, nullptr, 512, true));
  if (!client) return false;

  HTTPClient http;
  http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, quoteUrl)) return false;
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK || g_abortFetch) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument filter;
  filter["closePrice"] = true;
  filter["compareToPreviousClosePrice"] = true;
  filter["fluctuationsRatio"] = true;
  filter["stockName"] = true;
  filter["currencyType"]["code"] = true;
  filter["marketStatus"] = true;
  filter["overMarketPriceInfo"]["overMarketStatus"] = true;
  filter["overMarketPriceInfo"]["overPrice"] = true;
  filter["overMarketPriceInfo"]["compareToPreviousClosePrice"] = true;
  filter["overMarketPriceInfo"]["fluctuationsRatio"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload.c_str(), DeserializationOption::Filter(filter));
  if (err) return false;

  // 1-1. Check Pre-Market / After-Market first
  const char* overStatus = doc["overMarketPriceInfo"]["overMarketStatus"] | "";
  const char* overPriceStr = doc["overMarketPriceInfo"]["overPrice"] | "";
  bool useOverMarket = (!strcmp(overStatus, "OPEN") && overPriceStr[0] != 0);

  const char* priceStr = useOverMarket ? overPriceStr : (doc["closePrice"] | "");
  if (!priceStr[0]) return false;

  char pbuf[32];
  size_t pi = 0;
  for (size_t i = 0; priceStr[i] && pi < sizeof(pbuf)-1; i++) {
    if (priceStr[i] != ',') pbuf[pi++] = priceStr[i];
  }
  pbuf[pi] = 0;
  float price = atof(pbuf);
  if (price <= 0) return false;
  d.price = price;

  const char* diffStr = useOverMarket ? 
    (doc["overMarketPriceInfo"]["compareToPreviousClosePrice"] | "0") :
    (doc["compareToPreviousClosePrice"] | "0");
  pi = 0;
  for (size_t i = 0; diffStr[i] && pi < sizeof(pbuf)-1; i++) {
    if (diffStr[i] != ',') pbuf[pi++] = diffStr[i];
  }
  pbuf[pi] = 0;
  float diff = atof(pbuf);

  const char* pctStr = useOverMarket ?
    (doc["overMarketPriceInfo"]["fluctuationsRatio"] | "0") :
    (doc["fluctuationsRatio"] | "0");
  float pct = atof(pctStr);

  d.change = diff;
  d.changePct = pct;
  d.hasChange = true;
  d.prevClose = price - diff;

  const char* cur = doc["currencyType"]["code"] | "USD";
  if (!strcmp(cur, "USD")) strlcpy(d.currency, "$", sizeof(d.currency));
  else snprintf(d.currency, sizeof(d.currency), "%s ", cur);

  if (!d.userNamed) {
    const char* nm = doc["stockName"] | symbol;
    strlcpy(d.name, nm, MAX_NAME_LEN);
  }

  String rUpper = ts.range;
  rUpper.toUpperCase();
  strlcpy(d.rangeLabel, rUpper.c_str(), sizeof(d.rangeLabel));

  if (g_abortFetch) return false;

  // Check if minute timeframe (1m, 5m, 30m, 60m)
  bool isMinute = (ts.range == "1m" || ts.range == "5m" || ts.range == "30m" || ts.range == "60m");
  if (isMinute) {
    if (fetchNaverForeignMinute(s, ts, symbol, d)) {
      d.valid = true;
      d.error = false;
      d.lastOkMs = millis();
      return true;
    }
  }

  // 2. Candle chart (dayCandle, weekCandle, monthCandle, yearCandle)
  String chartUrl = F("https://api.stock.naver.com/chart/foreign/item/");
  chartUrl += symbol;
  if (ts.range == "1y" || ts.range == "year") {
    chartUrl += F("?periodType=yearCandle");
  } else if (ts.range == "1mo" || ts.range == "month") {
    chartUrl += F("?periodType=monthCandle");
  } else if (ts.range == "1w" || ts.range == "week" || ts.range == "5d") {
    chartUrl += F("?periodType=weekCandle");
  } else {
    chartUrl += F("?periodType=dayCandle");
  }

  std::unique_ptr<NetClient> chartClient(platformMakeSecureClient(2048, nullptr, 512, true));
  if (chartClient && http.begin(*chartClient, chartUrl)) {
    http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
    http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    int ccode = http.GET();
    if (ccode == HTTP_CODE_OK && !g_abortFetch) {
      String chartPayload = http.getString();
      JsonDocument cfilter;
      cfilter["priceInfos"][0]["openPrice"] = true;
      cfilter["priceInfos"][0]["highPrice"] = true;
      cfilter["priceInfos"][0]["lowPrice"] = true;
      cfilter["priceInfos"][0]["closePrice"] = true;

      JsonDocument cdoc;
      if (!deserializeJson(cdoc, chartPayload.c_str(), DeserializationOption::Filter(cfilter))) {
        JsonArrayConst arr = cdoc["priceInfos"].as<JsonArrayConst>();
        if (!arr.isNull() && arr.size() > 0) {
          uint16_t total = arr.size();
          uint16_t want = ts.points;
          if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
          if (want < 2) want = 20;

          d.sparkCount = 0;
          d.candleCount = 0;
          d.dateSplitIdx = -1;
          uint16_t startIdx = (total > want) ? (total - want) : 0;
          for (uint16_t i = startIdx; i < total; i++) {
            JsonObjectConst pt = arr[i];
            float op = pt["openPrice"] | 0.0f;
            float hi = pt["highPrice"] | 0.0f;
            float lo = pt["lowPrice"] | 0.0f;
            float cl = pt["closePrice"] | 0.0f;
            if (cl > 0) {
              if (d.sparkCount < MAX_SPARK_POINTS) d.spark[d.sparkCount++] = cl;
              if (d.candleCount < MAX_SPARK_POINTS) d.candles[d.candleCount++] = {op, hi, lo, cl};
            }
          }
          d.dateSplitIdx = -1;
          if (d.sparkCount > 0) {
            d.spark[d.sparkCount - 1] = d.price;
            if (d.candleCount > 0) {
              d.candles[d.candleCount - 1].close = d.price;
              if (d.price > d.candles[d.candleCount - 1].high) d.candles[d.candleCount - 1].high = d.price;
              if (d.price < d.candles[d.candleCount - 1].low)  d.candles[d.candleCount - 1].low = d.price;
            }
          }
        }
      }
    }
    http.end();
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

static bool fetchNaverMinute(const Settings& s, const TickerSettings& ts, const char* cleanCode, StockData& d) {
  String url = F("https://api.stock.naver.com/chart/domestic/item/");
  url += cleanCode;
  url += F("/minute");

  time_t now = time(nullptr);
  if (now > 1700000000) {
    int daysBack = 2;
    if (ts.range == "30m" || ts.range == "60m") {
      daysBack = 6;
    }
    time_t start = now - (daysBack * 86400);
    struct tm tmStart, tmNow;
    localtime_r(&start, &tmStart);
    localtime_r(&now, &tmNow);

    char param[80];
    snprintf(param, sizeof(param),
             "?startDateTime=%04d%02d%02d090000&endDateTime=%04d%02d%02d%02d%02d00",
             tmStart.tm_year + 1900, tmStart.tm_mon + 1, tmStart.tm_mday,
             tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
             tmNow.tm_hour, tmNow.tm_min);
    url += param;
  } else {
    url += F("?range=1");
  }

  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048, nullptr, 512, true));
  if (!client) return false;

  HTTPClient http;
  http.setTimeout((s.httpTimeout > 3500) ? 3500 : s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK || g_abortFetch) {
    http.end();
    return false;
  }

  WiFiClient* stream = (WiFiClient*)http.getStreamPtr();
  if (!stream) {
    http.end();
    return false;
  }

  // Target points from user setting (e.g. 20, 30, 40 points)
  uint8_t targetPoints = (ts.points >= 5 && ts.points <= MAX_SPARK_POINTS) ? ts.points : 30;

  // Resample interval: 1m -> 1, 5m -> 5, 30m -> 30, 60m -> 60
  int interval = 1;
  if (ts.range == "5m") interval = 5;
  else if (ts.range == "30m") interval = 30;
  else if (ts.range == "60m") interval = 60;

  // Temporary ring buffer for candles & date tracking
  StockData::Candle ring[MAX_SPARK_POINTS];
  uint32_t ringDates[MAX_SPARK_POINTS];
  uint8_t ringCount = 0;
  uint8_t ringHead = 0;

  auto pushRing = [&](const StockData::Candle& c, uint32_t dt) {
    if (ringCount < targetPoints) {
      ring[ringCount] = c;
      ringDates[ringCount] = dt;
      ringCount++;
    } else {
      ring[ringHead] = c;
      ringDates[ringHead] = dt;
      ringHead = (ringHead + 1) % targetPoints;
    }
  };

  // Stream parsing line by line / buffer scanning
  // JSON format: [{"localDateTime":"...","currentPrice":...,"openPrice":...,"highPrice":...,"lowPrice":...,"accumulatedTradingVolume":...},...]
  float curGroupOpen = 0, curGroupHigh = -1e9f, curGroupLow = 1e9f, curGroupClose = 0;
  int groupCount = 0;
  uint32_t lastDate = 0;
  uint32_t curDate = 0;

  char buf[256];
  uint32_t lastPoll = millis();
  uint32_t loopStart = millis();
  stream->setTimeout(150);

  while (http.connected() || stream->available()) {
    if (g_abortFetch || (millis() - loopStart > 4500)) {
      break;
    }
    if (millis() - lastPoll > 30) {
      buttonPoll();
      webPortalLoop();
      yield();
      lastPoll = millis();
      if (g_abortFetch) break;
    }

    int len = stream->readBytesUntil('{', buf, sizeof(buf) - 1);
    if (len <= 0 && !stream->available()) break;
    if (len > 0) {
      buf[len] = 0;
      if (strchr(buf, ']')) break;
    } else {
      buf[0] = 0;
    }

    int objLen = stream->readBytesUntil('}', buf, sizeof(buf) - 1);
    if (objLen <= 0 && !stream->available()) break;
    if (objLen <= 0) continue;
    buf[objLen] = 0;

    // Search for localDateTime, currentPrice, openPrice, highPrice, lowPrice
    char* pDt = strstr(buf, "\"localDateTime\":\"");
    if (pDt) {
      char dateBuf[9];
      memcpy(dateBuf, pDt + 17, 8);
      dateBuf[8] = 0;
      curDate = (uint32_t)strtoul(dateBuf, nullptr, 10);
    }

    // Date boundary check: if trading day changed, flush previous day's partial group
    if (lastDate != 0 && curDate != 0 && curDate != lastDate) {
      if (groupCount > 0) {
        pushRing({curGroupOpen, curGroupHigh, curGroupLow, curGroupClose}, lastDate);
        groupCount = 0;
        curGroupHigh = -1e9f;
        curGroupLow = 1e9f;
      }
    }
    if (curDate != 0) lastDate = curDate;

    char* pCur = strstr(buf, "\"currentPrice\":");
    char* pOpen = strstr(buf, "\"openPrice\":");
    char* pHigh = strstr(buf, "\"highPrice\":");
    char* pLow = strstr(buf, "\"lowPrice\":");

    if (pCur && pOpen && pHigh && pLow) {
      float cl = atof(pCur + 15);
      float op = atof(pOpen + 12);
      float hi = atof(pHigh + 12);
      float lo = atof(pLow + 11);

      if (cl > 0) {
        if (groupCount == 0) curGroupOpen = op;
        if (hi > curGroupHigh) curGroupHigh = hi;
        if (lo < curGroupLow)  curGroupLow = lo;
        curGroupClose = cl;
        groupCount++;

        if (groupCount >= interval) {
          pushRing({curGroupOpen, curGroupHigh, curGroupLow, curGroupClose}, curDate);
          groupCount = 0;
          curGroupHigh = -1e9f;
          curGroupLow = 1e9f;
        }
      }
    }
  }
  http.end();

  // If leftover in group, push it
  if (groupCount > 0) {
    pushRing({curGroupOpen, curGroupHigh, curGroupLow, curGroupClose}, lastDate);
  }

  // Populate d.candles & d.spark in chronological order
  d.sparkCount = 0;
  d.candleCount = 0;
  d.dateSplitIdx = -1;
  d.dateSplitCount = 0;

  uint32_t candleDates[MAX_SPARK_POINTS];
  for (uint8_t i = 0; i < ringCount; i++) {
    uint8_t idx = (ringHead + i) % ringCount;
    d.candles[d.candleCount++] = ring[idx];
    d.spark[d.sparkCount++] = ring[idx].close;
    candleDates[i] = ringDates[idx];
  }

  // Find all date boundaries (whenever trading day changes in multi-day data)
  for (uint8_t i = 1; i < ringCount; i++) {
    if (candleDates[i] != candleDates[i - 1] && candleDates[i] > 0 && candleDates[i - 1] > 0) {
      if (d.dateSplitCount < StockData::MAX_DATE_SPLITS) {
        d.dateSplits[d.dateSplitCount++] = (int8_t)i;
      }
      d.dateSplitIdx = (int8_t)i;
    }
  }

  if (d.sparkCount > 0) {
    d.spark[d.sparkCount - 1] = d.price;
    if (d.candleCount > 0) {
      d.candles[d.candleCount - 1].close = d.price;
      if (d.price > d.candles[d.candleCount - 1].high) d.candles[d.candleCount - 1].high = d.price;
      if (d.price < d.candles[d.candleCount - 1].low)  d.candles[d.candleCount - 1].low = d.price;
    }
  }

  // Edge case safety: if only 1 candle exists (e.g. right at market open 09:00:10), duplicate it so candleCount >= 2
  if (d.candleCount == 1) {
    d.candles[1] = d.candles[0];
    d.candleCount = 2;
    d.spark[1] = d.spark[0];
    d.sparkCount = 2;
  }

  return (d.candleCount >= 2);
}

static bool fetchNaver(const Settings& s, const TickerSettings& ts, const char* cleanCode, StockData& d) {
  if (g_abortFetch) return false;

  // Step 1: Real-time basic quote
  String quoteUrl = F("https://m.stock.naver.com/api/stock/");
  quoteUrl += cleanCode;
  quoteUrl += F("/basic");

  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048, nullptr, 512, true));
  if (!client) return false;

  HTTPClient http;
  http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, quoteUrl)) return false;
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK || g_abortFetch) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument filter;
  filter["closePrice"] = true;
  filter["compareToPreviousClosePrice"] = true;
  filter["fluctuationsRatio"] = true;
  filter["stockName"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload.c_str(), DeserializationOption::Filter(filter));
  if (err) return false;

  const char* priceStr = doc["closePrice"] | "";
  if (!priceStr[0]) return false;

  char pbuf[32];
  size_t pi = 0;
  for (size_t i = 0; priceStr[i] && pi < sizeof(pbuf)-1; i++) {
    if (priceStr[i] != ',') pbuf[pi++] = priceStr[i];
  }
  pbuf[pi] = 0;
  float price = atof(pbuf);
  if (price <= 0) return false;
  d.price = price;

  const char* diffStr = doc["compareToPreviousClosePrice"] | "0";
  pi = 0;
  for (size_t i = 0; diffStr[i] && pi < sizeof(pbuf)-1; i++) {
    if (diffStr[i] != ',') pbuf[pi++] = diffStr[i];
  }
  pbuf[pi] = 0;
  float diff = atof(pbuf);

  const char* pctStr = doc["fluctuationsRatio"] | "0";
  float pct = atof(pctStr);

  d.change = diff;
  d.changePct = pct;
  d.hasChange = true;
  d.prevClose = price - diff;

  strlcpy(d.currency, "KRW ", sizeof(d.currency));

  if (!d.userNamed) {
    const char* kn = lookupKoreanName(cleanCode);
    if (kn) {
      strlcpy(d.name, kn, MAX_NAME_LEN);
    } else {
      strlcpy(d.name, cleanCode, MAX_NAME_LEN);
    }
  }

  String rUpper = ts.range;
  rUpper.toUpperCase();
  strlcpy(d.rangeLabel, rUpper.c_str(), sizeof(d.rangeLabel));

  // Step 2: Check if minute timeframe (1m, 5m, 30m, 60m)
  bool isMinute = (ts.range == "1m" || ts.range == "5m" || ts.range == "30m" || ts.range == "60m");
  if (isMinute) {
    if (fetchNaverMinute(s, ts, cleanCode, d)) {
      d.valid = true;
      d.error = false;
      d.lastOkMs = millis();
      return true;
    }
  }

  // Step 2 (fallback / day/week/month): Historical chart points via fchart.stock.naver.com (EUC-KR XML)
  uint16_t want = ts.points;
  if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
  if (want < 2) want = 20;

  const char* tf = naverTimeframe(ts.range);
  uint16_t cnt = naverCount(ts.range);
  if (cnt < want) cnt = want;

  // If minute candles requested but unavailable (e.g. pre-market 08:00-09:00 or market closed),
  // fallback to daily candles ('day') because fchart 'minute' is unaggregated and lacks OHLC (nulls).
  if (isMinute) {
    tf = "day";
    cnt = want;
  }

  String chartUrl = F("https://fchart.stock.naver.com/sise.nhn?symbol=");
  chartUrl += cleanCode;
  chartUrl += F("&timeframe=");
  chartUrl += tf;
  chartUrl += F("&count=");
  chartUrl += String(cnt);
  chartUrl += F("&requestType=0");

  if (g_abortFetch) return false;

  std::unique_ptr<NetClient> chartClient(platformMakeSecureClient(2048, nullptr, 512, true));
  if (chartClient && http.begin(*chartClient, chartUrl)) {
    http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
    http.setUserAgent(F(YAHOO_USER_AGENT));
    int ccode = http.GET();
    if (ccode == HTTP_CODE_OK && !g_abortFetch) {
      String chartXml = http.getString();
      d.sparkCount = 0;
      d.candleCount = 0;
      d.dateSplitIdx = -1;

      // Parse <item data="YYYYMMDD|open|high|low|close|vol" />
      int idx = 0;
      char lastDate[10] = "";
      while ((idx = chartXml.indexOf("<item data=\"", idx)) >= 0) {
        idx += 12; // length of '<item data="'
        int endQ = chartXml.indexOf('"', idx);
        if (endQ < 0) break;
        String line = chartXml.substring(idx, endQ);
        idx = endQ;

        // split by '|'
        int p1 = line.indexOf('|');
        if (p1 < 0) continue;
        int p2 = line.indexOf('|', p1 + 1);
        if (p2 < 0) continue;
        int p3 = line.indexOf('|', p2 + 1);
        if (p3 < 0) continue;
        int p4 = line.indexOf('|', p3 + 1);
        if (p4 < 0) continue;
        int p5 = line.indexOf('|', p4 + 1);

        float op = line.substring(p1 + 1, p2).toFloat();
        float hi = line.substring(p2 + 1, p3).toFloat();
        float lo = line.substring(p3 + 1, p4).toFloat();
        String closePart = (p5 >= 0) ? line.substring(p4 + 1, p5) : line.substring(p4 + 1);
        float cl = closePart.toFloat();

        // Safety fallback against nulls or zero prices
        if (op <= 0) op = cl;
        if (hi <= 0) hi = cl;
        if (lo <= 0) lo = cl;

        if (d.sparkCount < MAX_SPARK_POINTS && cl > 0) {
          String datePart = line.substring(0, p1);
          d.dateSplitIdx = -1;
          strlcpy(lastDate, datePart.c_str(), sizeof(lastDate));
          d.spark[d.sparkCount++] = cl;
          if (d.candleCount < MAX_SPARK_POINTS) {
            d.candles[d.candleCount++] = {op, hi, lo, cl};
          }
        }
      }
      if (d.sparkCount > 0) {
        d.spark[d.sparkCount - 1] = d.price;
        if (d.candleCount > 0) {
          d.candles[d.candleCount - 1].close = d.price;
          if (d.price > d.candles[d.candleCount - 1].high) d.candles[d.candleCount - 1].high = d.price;
          if (d.price < d.candles[d.candleCount - 1].low) d.candles[d.candleCount - 1].low = d.price;
        }
      }
    }
    http.end();
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: custom webhook contract ---------------------------------------
static bool parseWebhook(const TickerSettings& ts, StockData& d, Stream& stream) {
  JsonDocument filter;
  filter["symbol"] = true;
  filter["name"] = true;
  filter["price"] = true;
  filter["currency"] = true;
  filter["change"] = true;
  filter["changePct"] = true;
  filter["range"] = true;
  filter["ok"] = true;
  filter["spark"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;
  if (!doc["price"].is<float>() && !doc["price"].is<int>()) return false;

  float price = doc["price"].as<float>();
  if (isnan(price)) return false;
  d.price = price;

  if (!d.userNamed && doc["name"].is<const char*>())
    strlcpy(d.name, doc["name"].as<const char*>(), MAX_NAME_LEN);
  strlcpy(d.currency, doc["currency"] | "", sizeof(d.currency));

  const char* rng = doc["range"] | ts.range.c_str();
  strlcpy(d.rangeLabel, rng, sizeof(d.rangeLabel));

  bool hasChg = doc["change"].is<float>() || doc["change"].is<int>();
  bool hasPct = doc["changePct"].is<float>() || doc["changePct"].is<int>();
  d.hasChange = hasChg || hasPct;
  if (hasChg) d.change = doc["change"].as<float>();
  if (hasPct) d.changePct = doc["changePct"].as<float>();
  if (d.hasChange) {
    if (!hasPct && hasChg) {
      float prev = price - d.change;
      d.changePct = (prev != 0) ? (d.change / prev * 100.0f) : 0;
    } else if (hasPct && !hasChg) {
      d.change = price * d.changePct / (100.0f + d.changePct);
    }
    d.prevClose = price - d.change;
  }

  d.sparkCount = 0;
  if (doc["spark"].is<JsonArrayConst>()) {
    for (JsonVariantConst v : doc["spark"].as<JsonArrayConst>()) {
      if (!v.is<float>() && !v.is<int>()) continue;
      if (d.sparkCount >= MAX_SPARK_POINTS) break;
      d.spark[d.sparkCount++] = v.as<float>();
    }
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: Yahoo Finance chart payload -----------------------------------
static bool parseYahoo(const TickerSettings& ts, StockData& d, Stream& stream) {
  JsonDocument filter;
  JsonObject fmeta = filter["chart"]["result"][0]["meta"].to<JsonObject>();
  fmeta["regularMarketPrice"] = true;
  fmeta["chartPreviousClose"] = true;
  fmeta["previousClose"]      = true;
  fmeta["currency"]           = true;
  fmeta["shortName"]          = true;
  fmeta["longName"]           = true;
  JsonObject fq = filter["chart"]["result"][0]["indicators"]["quote"][0].to<JsonObject>();
  fq["open"] = true;
  fq["high"] = true;
  fq["low"] = true;
  fq["close"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonObjectConst res  = doc["chart"]["result"][0];
  JsonObjectConst meta = res["meta"];
  if (meta.isNull()) return false;
  if (!meta["regularMarketPrice"].is<float>() &&
      !meta["regularMarketPrice"].is<int>()) return false;

  float price = meta["regularMarketPrice"].as<float>();
  if (isnan(price)) return false;
  d.price = price;

  yahooCurrency(meta["currency"] | "", d.currency, sizeof(d.currency));

  if (!d.userNamed) {
    const char* nm = meta["shortName"] | (meta["longName"] | (const char*)d.symbol);
    if (nm && nm[0]) strlcpy(d.name, nm, MAX_NAME_LEN);
  }

  // Change vs the previous close.
  float prev = NAN;
  if (meta["chartPreviousClose"].is<float>() || meta["chartPreviousClose"].is<int>())
    prev = meta["chartPreviousClose"].as<float>();
  else if (meta["previousClose"].is<float>() || meta["previousClose"].is<int>())
    prev = meta["previousClose"].as<float>();

  if (!isnan(prev) && prev != 0) {
    d.prevClose = prev;
    d.change = price - prev;
    d.changePct = d.change / prev * 100.0f;
    d.hasChange = true;
  } else {
    d.hasChange = false;
  }

  String rl = ts.range;
  rl.toUpperCase();
  strlcpy(d.rangeLabel, rl.c_str(), sizeof(d.rangeLabel));

  d.sparkCount = 0;
  d.candleCount = 0;
  JsonArrayConst opens  = res["indicators"]["quote"][0]["open"];
  JsonArrayConst highs  = res["indicators"]["quote"][0]["high"];
  JsonArrayConst lows   = res["indicators"]["quote"][0]["low"];
  JsonArrayConst closes = res["indicators"]["quote"][0]["close"];
  uint16_t want = ts.points;
  if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
  if (!closes.isNull() && want >= 2) {
    uint16_t valid = 0;
    for (JsonVariantConst v : closes) if (!v.isNull()) valid++;
    if (valid >= 2) {
      uint16_t totalCount = closes.size();
      uint16_t i = 0, k = 0;
      float last = 0;
      for (uint16_t idx = 0; idx < totalCount; idx++) {
        JsonVariantConst vc = closes[idx];
        if (vc.isNull()) continue;
        last = vc.as<float>();
        float op = (!opens.isNull() && !opens[idx].isNull()) ? opens[idx].as<float>() : last;
        float hi = (!highs.isNull() && !highs[idx].isNull()) ? highs[idx].as<float>() : last;
        float lo = (!lows.isNull() && !lows[idx].isNull())   ? lows[idx].as<float>()  : last;

        uint16_t target = (valid <= want) ? i : (uint16_t)(((uint32_t)k * (valid - 1) + (want - 1) / 2) / (want - 1));
        if (i == target && (valid <= want || k < want)) {
          if (d.sparkCount < MAX_SPARK_POINTS) {
            d.spark[d.sparkCount++] = last;
          }
          if (d.candleCount < MAX_SPARK_POINTS) {
            d.candles[d.candleCount++] = {op, hi, lo, last};
          }
          k++;
        }
        i++;
      }
      if (d.sparkCount > 0) {
        d.spark[d.sparkCount - 1] = d.price;
        if (d.candleCount > 0) {
          d.candles[d.candleCount - 1].close = d.price;
          if (d.price > d.candles[d.candleCount - 1].high) d.candles[d.candleCount - 1].high = d.price;
          if (d.price < d.candles[d.candleCount - 1].low)  d.candles[d.candleCount - 1].low = d.price;
        }
      }
    }
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: cash.ch quote ---------------------------------------------------
static bool parseCashQuote(const TickerSettings& ts, StockData& d, Stream& stream) {
  JsonDocument filter;
  JsonObject node =
      filter["data"]["quoteList"]["quoteList"]["edges"][0]["node"].to<JsonObject>();
  node["lval"]           = true;
  node["iNetVperprV"]    = true;
  node["perfPercentage"] = true;
  node["mCur"]           = true;
  node["mShortName"]     = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonObjectConst n = doc["data"]["quoteList"]["quoteList"]["edges"][0]["node"];
  const char* lval = n["lval"] | "";
  if (!lval[0]) return false;
  float price = atof(lval);
  if (isnan(price) || price <= 0) return false;
  d.price = price;

  yahooCurrency(n["mCur"] | "", d.currency, sizeof(d.currency));

  if (!d.userNamed) {
    const char* nm = n["mShortName"] | (const char*)d.symbol;
    if (nm && nm[0]) strlcpy(d.name, nm, MAX_NAME_LEN);
  }

  const char* chg = n["iNetVperprV"]    | "";
  const char* pct = n["perfPercentage"] | "";
  d.hasChange = chg[0] || pct[0];
  if (d.hasChange) {
    d.change    = atof(chg);
    d.changePct = pct[0] ? atof(pct) : 0;
    if (!pct[0]) {
      float prev = price - d.change;
      d.changePct = (prev != 0) ? (d.change / prev * 100.0f) : 0;
    }
    d.prevClose = price - d.change;
  }

  String rl = ts.range;
  rl.toUpperCase();
  strlcpy(d.rangeLabel, rl.c_str(), sizeof(d.rangeLabel));

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: cash.ch daily-close series --------------------------------------
static bool parseCashChart(const TickerSettings& ts, StockData& d, Stream& stream) {
  JsonDocument filter;
  filter["data"]["integration"]["solid"]["chart"]["timeserie"]["prices"][0]["close"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonArrayConst prices =
      doc["data"]["integration"]["solid"]["chart"]["timeserie"]["prices"];
  if (prices.isNull()) return false;

  uint16_t want = ts.points;
  if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
  if (want < 2) return true;

  uint16_t valid = 0;
  for (JsonObjectConst p : prices)
    if (p["close"].is<float>() || p["close"].is<int>()) valid++;
  if (valid < 2) return false;

  d.sparkCount = 0;
  if (valid <= want) {
    for (JsonObjectConst p : prices) {
      if (!p["close"].is<float>() && !p["close"].is<int>()) continue;
      if (d.sparkCount >= MAX_SPARK_POINTS) break;
      d.spark[d.sparkCount++] = p["close"].as<float>();
    }
  } else {
    uint16_t i = 0, k = 0;
    float last = 0;
    for (JsonObjectConst p : prices) {
      if (!p["close"].is<float>() && !p["close"].is<int>()) continue;
      last = p["close"].as<float>();
      uint16_t target =
          (uint16_t)(((uint32_t)k * (valid - 1) + (want - 1) / 2) / (want - 1));
      if (i == target && k < want) { d.spark[d.sparkCount++] = last; k++; }
      i++;
    }
    if (d.sparkCount > 0) d.spark[d.sparkCount - 1] = last;
  }
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
enum ParseKind : uint8_t {
  PARSE_WEBHOOK,
  PARSE_YAHOO,
  PARSE_CASH_QUOTE,
  PARSE_CASH_CHART,
  PARSE_GITHUB,
  PARSE_NAVER
};

static TlsSession g_cashSession;

static bool fetchUrl(const Settings& s, const TickerSettings& ts, const String& url, ParseKind kind, StockData& d) {
  if (g_abortFetch) return false;
  bool https = url.startsWith("https://");
  bool cash = (kind == PARSE_CASH_QUOTE || kind == PARSE_CASH_CHART);

  std::unique_ptr<NetClient> client;
  if (https) {
    if (cash) {
      if (platformMaxFreeBlock() < 16000) return false;
      client.reset(platformMakeSecureClient(512, &g_cashSession, 512, /*cheapCiphers=*/false));
    } else {
      uint16_t rx = (kind == PARSE_GITHUB) ? GH_QUOTES_RXBUF : 4096;
      if (ESP.getFreeHeap() < (uint32_t)rx + 12000) return false;
      client.reset(platformMakeSecureClient(rx, nullptr, 512, /*cheapCiphers=*/true));
    }
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout((s.httpTimeout > 2500) ? 2500 : s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  if (kind == PARSE_YAHOO || kind == PARSE_NAVER) {
    http.setUserAgent(F(YAHOO_USER_AGENT));
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  } else if (kind == PARSE_GITHUB) {
    http.setUserAgent(F(FW_NAME));
  } else if (kind != PARSE_WEBHOOK) {
    http.setUserAgent(F(CASH_USER_AGENT));
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  bool ok;
  switch (kind) {
    case PARSE_NAVER:      ok = false;                                       break;
    case PARSE_YAHOO:      ok = parseYahoo(ts, d, http.getStream());        break;
    case PARSE_CASH_QUOTE: ok = parseCashQuote(ts, d, http.getStream());    break;
    case PARSE_CASH_CHART: ok = parseCashChart(ts, d, http.getStream());    break;
    default:               ok = parseWebhook(ts, d, http.getStream());      break;
  }
  http.end();
  return ok;
}

// ---- fetch one symbol, one network request per call -----------------------
static bool stepSymbol(const Settings& s, const TickerSettings& ts, StockData& d, uint8_t& fetchPhase) {
  if (g_abortFetch) return false;
  g_abortFetch = false;
  char cleanCode[8];
  bool isKr = isKoreanCode(d.symbol, cleanCode, sizeof(cleanCode));

  if (d.source == SRC_NAVER) {
    if (isKr) {
      if (!fetchNaver(s, ts, cleanCode, d)) d.error = true;
    } else {
      // Foreign stock on Naver (e.g. SOXX.O, SPY, NVDA.O)
      if (!fetchNaverForeign(s, ts, d.symbol, d)) d.error = true;
    }
    return true;
  }

  if (isKr) {
    if (!fetchNaver(s, ts, cleanCode, d)) d.error = true;
    return true;
  }

  if (d.source == SRC_YAHOO) {
    if (fetchPhase == 0) {
      if (fetchUrl(s, ts, buildYahooUrl(ts, YAHOO_CHART_HOST1, d.symbol), PARSE_YAHOO, d)) return true;
      fetchPhase = 1;
      return false;
    }
    if (!fetchUrl(s, ts, buildYahooUrl(ts, YAHOO_CHART_HOST2, d.symbol), PARSE_YAHOO, d)) d.error = true;
    return true;
  }

  if (d.source == SRC_CASH) {
    if (fetchPhase == 0) {
      if (fetchUrl(s, ts, buildCashQuoteUrl(d.symbol), PARSE_CASH_QUOTE, d)) { fetchPhase = 2; return false; }
      fetchPhase = 1;
      return false;
    }
    if (fetchPhase == 1) {
      if (!fetchUrl(s, ts, buildCashQuoteUrl(d.symbol), PARSE_CASH_QUOTE, d)) { d.error = true; return true; }
      fetchPhase = 2;
      return false;
    }
    if ((ts.showChart || (ts.changeOnRange && ts.showChange)) && ts.points >= 2)
      fetchUrl(s, ts, buildCashChartUrl(ts, d.symbol), PARSE_CASH_CHART, d);
    return true;
  }

  if (d.source == SRC_GHUB) {
    if (!fetchUrl(s, ts, buildGithubUrl(d.symbol), PARSE_GITHUB, d)) d.error = true;
    return true;
  }

  if (ts.webhookUrl.length() < 8) { d.error = true; return true; }
  if (!fetchUrl(s, ts, buildWebhookUrl(ts, d.symbol), PARSE_WEBHOOK, d)) d.error = true;
  return true;
}

static uint32_t symbolPeriodMs(const TickerSettings& ts, const StockData& d) {
  uint32_t poll = (uint32_t)ts.pollSec * 1000UL;
  if (!d.error || d.fails > TICKER_RETRY_MAX) return poll;
  uint32_t back = (uint32_t)TICKER_RETRY_SEC * 1000UL;
  for (uint8_t i = 1; i < d.fails; i++) back <<= 1;
  return back < poll ? back : poll;
}

static bool symbolDue(const StockData& d) {
  return (int32_t)(millis() - d.nextTryMs) >= 0;
}

// ---------------------------------------------------------------------------
void stocksService(const Settings& s) {
  for (uint8_t tryCount = 0; tryCount < NUM_STOCK_SETS; tryCount++) {
    uint8_t set = g_serviceSet;
    g_serviceSet = (g_serviceSet + 1) % NUM_STOCK_SETS;
    const TickerSettings& ts = (set == 1) ? s.ticker2 : s.ticker;
    StockSet& ss = g_sets[set];
    if (ss.count == 0) continue;

    if (!ss.refreshing) {
      uint8_t first = ss.count;
      for (uint8_t i = 0; i < ss.count; i++)
        if (symbolDue(ss.stocks[i])) { first = i; break; }
      if (first >= ss.count) continue;
      ss.refreshing = true;
      ss.fetchIdx = first;
      ss.fetchPhase = 0;
    }

    while (ss.fetchIdx < ss.count && !symbolDue(ss.stocks[ss.fetchIdx])) ss.fetchIdx++;

    if (ss.fetchIdx < ss.count) {
      StockData& d = ss.stocks[ss.fetchIdx];
      if (stepSymbol(s, ts, d, ss.fetchPhase)) {
        if (d.error) { if (d.fails < 250) d.fails++; }
        else         { d.fails = 0; }
        d.nextTryMs = millis() + symbolPeriodMs(ts, d);
        ss.fetchIdx++;
        ss.fetchPhase = 0;
      }
      return;
    }

    ss.refreshing = false;
  }
}
