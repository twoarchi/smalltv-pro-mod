// StockClient.h — fetches ticker data from the configured source
// (Naver Finance, Yahoo Finance, cash.ch, or a custom webhook)
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "StockData.h"

#define NUM_STOCK_SETS 2

void  stocksInit(const Settings& s, uint8_t set = 0);        // (re)build list from settings
void  stocksService(const Settings& s);                      // call often; self-times the polling
void  stocksForceRefresh(uint8_t set = 0);                  // poll ASAP (e.g. after config save)
void  stocksForceRefreshAll();
void  stocksDeferNextFetch(uint8_t set, uint32_t delayMs);   // defer next fetch (debounce for rapid switching)
void  stocksPrioritize(uint8_t set, uint8_t idx);            // prioritize fetching a specific symbol (e.g. visible page)

uint8_t          stocksCount(uint8_t set = 0);
const StockData& stockAt(uint8_t i, uint8_t set = 0);
bool             stocksAnyValid(uint8_t set = 0);

// Seconds until symbol i is fetched again (0 = due now).
uint32_t stockRetryInSec(uint8_t i, uint8_t set = 0);

// Effective change for display.
bool stockDisplayChange(const StockData& d, const TickerSettings& t,
                        float& chg, float& pct, bool* onRange = nullptr);
