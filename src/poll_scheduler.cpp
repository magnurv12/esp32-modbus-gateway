#include "poll_scheduler.h"

#include <algorithm>
#include <utility>

#include "config.h"
#include "ws_stream.h"

namespace {

// Same chunking as modbusTask's executeRead (ModbusMaster's 64-word buffer).
constexpr uint16_t kChunkRegisters = 64;
constexpr uint16_t kChunkBits = 64 * 16;
// Slave turnaround + inter-frame gap per transaction, on top of the bytes.
constexpr float kTransactionOverheadMs = 10.0f;
constexpr uint32_t kBackoffBaseMs = 250;
constexpr uint32_t kBackoffMaxMs = 5000;

float frameMs(uint32_t bytes) {
  return bytes * 10.0f * 1000.0f / MODBUS_BAUD; // 8N1 = 10 bits per byte
}

// Estimated bus time to read `count` addresses, including chunking.
float readCostMs(ModbusTable table, uint16_t count) {
  bool bits = isBitTable(table);
  uint16_t chunk = bits ? kChunkBits : kChunkRegisters;
  float total = 0;
  for (uint32_t offset = 0; offset < count; offset += chunk) {
    uint16_t quantity = std::min<uint32_t>(chunk, count - offset);
    uint32_t responseBytes = 5 + (bits ? (quantity + 7) / 8 : quantity * 2);
    total += frameMs(8 + responseBytes) + kTransactionOverheadMs;
  }
  return total;
}

bool sameTarget(const Subscription &a, const Subscription &b) {
  return a.slave == b.slave && a.table == b.table;
}

} // namespace

int PollScheduler::findSubscription(uint32_t clientId, const char *id) const {
  for (size_t i = 0; i < subs_.size(); i++) {
    if (subs_[i].clientId == clientId && strcmp(subs_[i].id, id) == 0) return i;
  }
  return -1;
}

bool PollScheduler::rebuildBlocks(uint32_t now) {
  std::vector<size_t> order(subs_.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
    const Subscription &x = subs_[a], &y = subs_[b];
    if (x.slave != y.slave) return x.slave < y.slave;
    if (x.table != y.table) return x.table < y.table;
    return x.start < y.start;
  });

  // Merge overlapping/adjacent ranges on the same slave+table, within the
  // per-request read limit.
  std::vector<PollBlock> next;
  std::vector<int> assignment(subs_.size(), -1);
  for (size_t idx : order) {
    const Subscription &sub = subs_[idx];
    uint32_t subEnd = static_cast<uint32_t>(sub.start) + sub.count;
    uint16_t maxCount = isBitTable(sub.table) ? MODBUS_MAX_READ_BITS : MODBUS_MAX_READ_REGISTERS;

    bool merged = false;
    if (!next.empty()) {
      PollBlock &last = next.back();
      uint32_t lastEnd = static_cast<uint32_t>(last.start) + last.count;
      uint32_t mergedEnd = std::max(lastEnd, subEnd);
      if (last.slave == sub.slave && last.table == sub.table && sub.start <= lastEnd &&
          mergedEnd - last.start <= maxCount) {
        last.count = mergedEnd - last.start;
        last.intervalMs = std::min(last.intervalMs, sub.intervalMs);
        merged = true;
      }
    }
    if (!merged) {
      PollBlock block{};
      block.slave = sub.slave;
      block.table = sub.table;
      block.start = sub.start;
      block.count = sub.count;
      block.intervalMs = sub.intervalMs;
      next.push_back(block);
    }
    assignment[idx] = next.size() - 1;
  }
  if (next.size() > WS_MAX_BLOCKS) return false;

  // Bus budget: stretch every interval by the same factor when the
  // requested polling would take more than WS_BUS_BUDGET of the bus.
  float load = 0;
  for (const PollBlock &block : next) load += readCostMs(block.table, block.count) / block.intervalMs;
  float factor = load > WS_BUS_BUDGET ? load / WS_BUS_BUDGET : 1.0f;
  for (PollBlock &block : next) {
    block.intervalMs = static_cast<uint32_t>(ceilf(block.intervalMs * factor));
  }

  // Keep timing, backoff and cached values of blocks that didn't change.
  for (PollBlock &block : next) {
    block.nextDueAt = now;
    for (const PollBlock &old : blocks_) {
      if (old.slave == block.slave && old.table == block.table && old.start == block.start &&
          old.count == block.count) {
        block.nextDueAt = old.nextDueAt;
        block.lastReadAt = old.lastReadAt;
        block.failures = old.failures;
        block.hasValues = old.hasValues;
        block.values = old.values;
        break;
      }
    }
  }

  blocks_ = std::move(next);
  for (size_t i = 0; i < subs_.size(); i++) subs_[i].block = assignment[i];
  busLoad_ = load / factor;
  return true;
}

void PollScheduler::reportIntervals() {
  for (Subscription &sub : subs_) {
    uint32_t effective = blocks_[sub.block].intervalMs;
    if (sub.reportedIntervalMs != 0 && sub.reportedIntervalMs != effective) {
      sub.reportedIntervalMs = effective;
      wsSendInterval(sub, "interval");
    }
  }
}

void PollScheduler::subscribe(const SubscribeRequest &request, uint32_t now) {
  // Re-subscribing with an existing id replaces that subscription.
  int existing = findSubscription(request.clientId, request.id);
  bool hadPrevious = existing >= 0;
  Subscription previous{};
  if (hadPrevious) {
    previous = subs_[existing];
    subs_.erase(subs_.begin() + existing);
  }
  auto restorePrevious = [&]() {
    if (hadPrevious) subs_.push_back(previous);
    rebuildBlocks(now); // can't fail: back to a state that fitted before
  };

  size_t clientSubs = 0;
  uint32_t points = request.count;
  for (const Subscription &sub : subs_) {
    if (sub.clientId == request.clientId) clientSubs++;
    points += sub.count;
  }
  const char *error = nullptr;
  const char *message = nullptr;
  if (clientSubs >= WS_MAX_SUBSCRIPTIONS_PER_CLIENT) {
    error = "too_many_subscriptions";
    message = "Limit of subscriptions per connection reached";
  } else if (subs_.size() >= WS_MAX_SUBSCRIPTIONS) {
    error = "too_many_subscriptions";
    message = "Limit of subscriptions on the gateway reached";
  } else if (points > WS_MAX_POINTS) {
    error = "too_many_points";
    message = "Too many addresses subscribed on the gateway";
  }
  if (error != nullptr) {
    restorePrevious();
    wsSendError(request.clientId, request.id, error, message);
    return;
  }

  Subscription sub{};
  sub.clientId = request.clientId;
  strlcpy(sub.id, request.id, sizeof(sub.id));
  sub.slave = request.slave;
  sub.table = request.table;
  sub.start = request.start;
  sub.count = request.count;
  sub.intervalMs = request.intervalMs;
  sub.block = -1;
  subs_.push_back(sub);

  if (!rebuildBlocks(now)) {
    subs_.pop_back();
    restorePrevious();
    wsSendError(request.clientId, request.id, "too_many_blocks",
                "Too many distinct ranges polled on the gateway");
    return;
  }

  Subscription &added = subs_.back();
  added.reportedIntervalMs = blocks_[added.block].intervalMs;
  wsSendInterval(added, "subscribed");
  reportIntervals();

  // A block that already has fresh values answers the new subscriber
  // right away instead of waiting for the next read.
  const PollBlock &block = blocks_[added.block];
  if (block.hasValues) pushBlockToSubscriber(added, block, now);
}

void PollScheduler::unsubscribe(uint32_t clientId, const char *id, uint32_t now) {
  int index = findSubscription(clientId, id);
  if (index < 0) {
    wsSendError(clientId, id, "unknown_subscription", "No subscription with this id on this connection");
    return;
  }
  subs_.erase(subs_.begin() + index);
  rebuildBlocks(now);
  reportIntervals();
  wsSendUnsubscribed(clientId, id);
}

void PollScheduler::removeClient(uint32_t clientId, uint32_t now) {
  size_t before = subs_.size();
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [clientId](const Subscription &sub) { return sub.clientId == clientId; }),
              subs_.end());
  if (subs_.size() == before) return;
  rebuildBlocks(now);
  reportIntervals();
}

int PollScheduler::dueBlock(uint32_t now, uint32_t &waitMs) const {
  int best = -1;
  int32_t bestDelta = INT32_MAX;
  for (size_t i = 0; i < blocks_.size(); i++) {
    int32_t delta = static_cast<int32_t>(blocks_[i].nextDueAt - now);
    if (delta < bestDelta) {
      bestDelta = delta;
      best = i;
    }
  }
  if (best < 0) {
    waitMs = UINT32_MAX;
    return -1;
  }
  if (bestDelta > 0) {
    waitMs = bestDelta;
    return -1;
  }
  waitMs = 0;
  return best;
}

void PollScheduler::pushBlockToSubscriber(Subscription &sub, const PollBlock &block, uint32_t now) {
  const uint16_t *slice = block.values.data() + (sub.start - block.start);

  // Slow client (send queue full): skip, and resend the full state once
  // it catches up -- only the latest values matter, not every change.
  if (!wsCanSend(sub.clientId)) {
    sub.synced = false;
    return;
  }

  if (!sub.synced) {
    sub.lastSent.assign(slice, slice + sub.count);
    sub.synced = true;
    sub.lastErrorSent = 0;
    sub.lastSentAt = now;
    wsSendSnapshot(sub, slice, block.lastReadAt);
    return;
  }

  // Merged with a faster subscription: don't push more often than asked
  // -- except right after a write, which clients expect to see at once.
  if (!sub.urgent && now - sub.lastSentAt < sub.intervalMs) return;
  sub.urgent = false;

  std::vector<std::pair<uint16_t, uint16_t>> changes;
  for (uint16_t i = 0; i < sub.count; i++) {
    if (slice[i] != sub.lastSent[i]) {
      changes.emplace_back(sub.start + i, slice[i]);
      sub.lastSent[i] = slice[i];
    }
  }
  if (changes.empty()) return;
  sub.lastSentAt = now;
  wsSendUpdate(sub, changes, block.lastReadAt);
}

void PollScheduler::onBlockRead(int index, uint8_t result, const uint16_t *values, uint32_t now) {
  PollBlock &block = blocks_[index];

  if (result == MODBUS_RESULT_OK) {
    block.failures = 0;
    block.values.assign(values, values + block.count);
    block.hasValues = true;
    block.lastReadAt = now;
    block.nextDueAt = now + block.intervalMs;
    for (Subscription &sub : subs_) {
      if (sub.block == index) pushBlockToSubscriber(sub, block, now);
    }
    return;
  }

  // Back off a failing block so a dead slave doesn't eat the bus.
  if (block.failures < 10) block.failures++;
  uint32_t backoff = std::min(kBackoffBaseMs << (block.failures - 1), kBackoffMaxMs);
  uint32_t retryIn = std::max(block.intervalMs, backoff);
  block.hasValues = false;
  block.nextDueAt = now + retryIn;
  for (Subscription &sub : subs_) {
    if (sub.block != index) continue;
    sub.synced = false; // next successful read sends a fresh snapshot
    if (sub.lastErrorSent != result && wsCanSend(sub.clientId)) {
      sub.lastErrorSent = result;
      wsSendModbusError(sub, result, retryIn);
    }
  }
}

void PollScheduler::invalidate(uint8_t slave, ModbusTable table, uint16_t start, uint16_t count,
                               uint32_t now) {
  uint32_t end = static_cast<uint32_t>(start) + count;
  for (size_t i = 0; i < blocks_.size(); i++) {
    PollBlock &block = blocks_[i];
    uint32_t blockEnd = static_cast<uint32_t>(block.start) + block.count;
    if (block.slave != slave || block.table != table || start >= blockEnd || block.start >= end) {
      continue;
    }
    block.hasValues = false;
    block.nextDueAt = now;
    for (Subscription &sub : subs_) {
      if (sub.block == static_cast<int>(i)) sub.urgent = true;
    }
  }
}

bool PollScheduler::readCached(uint8_t slave, ModbusTable table, uint16_t start, uint16_t count,
                               uint32_t now, uint32_t maxAgeMs, uint16_t *out, uint32_t &ageMs) const {
  uint32_t end = static_cast<uint32_t>(start) + count;
  for (const PollBlock &block : blocks_) {
    if (!block.hasValues || block.slave != slave || block.table != table) continue;
    if (start < block.start || end > static_cast<uint32_t>(block.start) + block.count) continue;
    uint32_t age = now - block.lastReadAt;
    if (age > std::min(maxAgeMs, block.intervalMs)) continue;
    std::copy_n(block.values.begin() + (start - block.start), count, out);
    ageMs = age;
    return true;
  }
  return false;
}
