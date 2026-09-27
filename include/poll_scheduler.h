#pragma once

#include <Arduino.h>

#include <vector>

#include "modbus_types.h"

// Live-polling state for the WebSocket stream. Owned by modbusTask and
// only ever touched from it, so it needs no locking.
//
// Clients subscribe to (slave, table, start, count, interval). Identical,
// overlapping or adjacent subscriptions on the same slave+table are merged
// into one PollBlock, so the bus reads each address once no matter how
// many clients watch it. Ranges separated by a gap are never merged: the
// slave may not define the addresses in between and would answer the
// whole read with exception 02.

constexpr size_t WS_SUBSCRIPTION_ID_MAX = 32;
constexpr uint32_t WS_MIN_INTERVAL_MS = 100;
constexpr uint32_t WS_MAX_INTERVAL_MS = 60000;
constexpr uint32_t WS_DEFAULT_INTERVAL_MS = 500;

constexpr size_t WS_MAX_SUBSCRIPTIONS_PER_CLIENT = 8;
constexpr size_t WS_MAX_SUBSCRIPTIONS = 32;
constexpr size_t WS_MAX_BLOCKS = 16;
// Sum of `count` over all subscriptions -- bounds the per-subscription
// "last sent" copies (2 bytes per address).
constexpr uint32_t WS_MAX_POINTS = 4000;
// Share of bus time live polling may take; the rest stays free for REST
// requests. Above it, every block's interval is stretched proportionally.
constexpr float WS_BUS_BUDGET = 0.7f;

struct Subscription {
  uint32_t clientId;
  char id[WS_SUBSCRIPTION_ID_MAX + 1];
  uint8_t slave;
  ModbusTable table;
  uint16_t start;
  uint16_t count;
  uint32_t intervalMs;           // as requested
  uint32_t reportedIntervalMs;   // effective interval last told to the client
  int block;                     // index into blocks_
  bool synced;                   // client holds a full snapshot matching lastSent
  uint8_t lastErrorSent;         // Modbus result of the last error pushed, 0 = none
  bool urgent;                   // a write touched it: push the next read unthrottled
  uint32_t lastSentAt;
  std::vector<uint16_t> lastSent;
};

struct PollBlock {
  uint8_t slave;
  ModbusTable table;
  uint16_t start;
  uint16_t count;
  uint32_t intervalMs;   // effective (after the bus budget)
  uint32_t nextDueAt;
  uint32_t lastReadAt;   // millis() of the last successful read
  uint8_t failures;      // consecutive failures, drives the backoff
  bool hasValues;        // `values` is a valid recent read
  std::vector<uint16_t> values;
};

struct SubscribeRequest {
  uint32_t clientId;
  char id[WS_SUBSCRIPTION_ID_MAX + 1];
  uint8_t slave;
  ModbusTable table;
  uint16_t start;
  uint16_t count;
  uint32_t intervalMs;
};

class PollScheduler {
public:
  // Adds or replaces (same client + id) a subscription. Replies to the
  // client itself ("subscribed" or an "error").
  void subscribe(const SubscribeRequest &request, uint32_t now);
  void unsubscribe(uint32_t clientId, const char *id, uint32_t now);
  void removeClient(uint32_t clientId, uint32_t now);

  // Index of the block that is most overdue, or -1. `waitMs` is set to the
  // time until the next block becomes due (UINT32_MAX when idle).
  int dueBlock(uint32_t now, uint32_t &waitMs) const;
  const PollBlock &block(int index) const { return blocks_[index]; }

  // Result of reading block `index` (values == nullptr on failure).
  // Pushes snapshots / deltas / errors to the subscribed clients.
  void onBlockRead(int index, uint8_t result, const uint16_t *values, uint32_t now);

  // A write succeeded: re-read overlapping blocks right away and stop
  // serving their cached values.
  void invalidate(uint8_t slave, ModbusTable table, uint16_t start, uint16_t count, uint32_t now);

  // Copies [start, start+count) from a block read less than `maxAgeMs` ago.
  bool readCached(uint8_t slave, ModbusTable table, uint16_t start, uint16_t count,
                  uint32_t now, uint32_t maxAgeMs, uint16_t *out, uint32_t &ageMs) const;

  size_t subscriptionCount() const { return subs_.size(); }
  size_t blockCount() const { return blocks_.size(); }
  float busLoad() const { return busLoad_; }

private:
  int findSubscription(uint32_t clientId, const char *id) const;
  // Recomputes blocks_ from subs_ (merge + bus budget). With
  // `enforceLimit`, returns false and leaves everything unchanged if the
  // result would exceed WS_MAX_BLOCKS. Removals don't enforce it: taking
  // out a subscription that bridged two ranges can split a block, and
  // refusing would keep polling a range nobody watches. The overshoot is
  // bounded (every block has at least one subscription, and those are
  // capped by WS_MAX_SUBSCRIPTIONS) and lasts until enough subscriptions
  // leave; meanwhile new subscriptions that need a block are refused.
  bool rebuildBlocks(uint32_t now, bool enforceLimit);
  void reportIntervals();
  void pushBlockToSubscriber(Subscription &sub, const PollBlock &block, uint32_t now);

  std::vector<Subscription> subs_;
  std::vector<PollBlock> blocks_;
  float busLoad_ = 0;
};
