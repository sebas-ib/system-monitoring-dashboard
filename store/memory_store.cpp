//
// Created by Sebastian Ibarra on 10/8/25.
//
// MemoryStore: thread-safe, per-metric, in-memory time series storage.
// - Each metric maps to a Series holding a RingBuffer<Sample> plus a per-series mutex.
// - Writes (append) create a Series lazily and then append a (ts_ms, value) sample.
// - Reads (query, count) lock only the target Series.
// - The map is protected by map_mtx_ and is locked only while accessing the map.
//
// Concurrency notes:
// - Lock order is consistent and minimal: we briefly lock map_mtx_ to find or create
//   the series, release it, then lock the Series::mtx to operate on its ring.
// - Series is constructed in-place using try_emplace(metric, per_metric_capacity_)
//   to avoid copying/moving (e.g., std::mutex cannot be moved/copied).
//
// Complexity notes (amortized):
// - append: O(1) average for hash map access + O(1) RingBuffer append.
// - query: O(N_metric) in number of retained samples for that metric (bounded by capacity).
// - count: O(1).
//
#include "store/memory_store.h"
#include <algorithm>   // std::max
#include <utility>     // std::move

/**
 * Compute the per-metric capacity based on how many seconds to keep and the sampling period.
 * We clamp both 'keep_seconds' and 'sample_period_s' to at least 1 to avoid division by zero
 * and to guarantee a capacity of at least 1 sample per metric.
 */
MemoryStore::MemoryStore(std::size_t keep_seconds, std::size_t sample_period_s) {
    // Capacity = keep_seconds / sample_period_s (rounded down), but at least 1.
    per_metric_capacity_ = std::max<std::size_t>(
            1, keep_seconds / std::max<std::size_t>(1, sample_period_s)
    );
    // Store the effective sample period (also clamped to >= 1).
    sample_period_s_ = std::max<std::size_t>(1, sample_period_s);
}

/**
 * Append a new sample (ts_ms, value) into the ring buffer for the given metric.
 * If the metric does not exist yet, lazily create a Series with the configured capacity.
 *
 * Thread-safety:
 * - Locks the map only while performing the lookup/creation.
 * - Locks the specific Series only while appending to its ring.
 */
void MemoryStore::append(const std::string &metric, std::int64_t ts_ms, double value) {
    Series* s = nullptr;

    // Acquire map lock to find or create the Series entry.
    {
        std::scoped_lock lk(map_mtx_);

        // try_emplace constructs the mapped value in place if missing.
        // Arguments: (key, Series constructor args...)
        // Here Series(capacity) is constructed directly, avoiding copies/moves.
        auto [it, inserted] = series_.try_emplace(metric, per_metric_capacity_);
        (void)inserted; // not needed further; creation is idempotent for our purposes
        s = &it->second;
    }

    // At this point, 's' points to a valid Series. Lock the series and append.
    {
        std::scoped_lock lk(s->mtx);
        // RingBuffer::append overwrites the oldest element when full.
        s->ring.append(Sample{ts_ms, value});
    }
}

/**
 * Return samples in the inclusive time range [from_ms, to_ms] for 'metric'.
 * If the metric does not exist, returns an empty vector.
 *
 * Thread-safety:
 * - Briefly locks the map to find the Series pointer.
 * - Locks the Series while performing the range extraction.
 *
 * Range semantics:
 * - Inclusive on both ends. If you want half-open intervals, adjust RingBuffer::range.
 */
std::vector<Sample> MemoryStore::query(const std::string &metric, std::int64_t from_ms, std::int64_t to_ms) const {
    // Find Series without holding the Series lock yet.
    const Series* s = find_series_(metric);
    if (!s) return {};

    // Lock the Series to read a consistent snapshot from the ring.
    std::scoped_lock ls(s->mtx);
    return s->ring.range(from_ms, to_ms);
}

/**
 * Return the number of samples currently retained for 'metric'.
 * If the metric does not exist, returns 0.
 *
 * Thread-safety:
 * - Brief map lookup, then Series lock to read size().
 */
std::size_t MemoryStore::count(const std::string &metric) const {
    const Series* s = find_series_(metric);
    if (!s) return 0;

    std::scoped_lock ls(s->mtx);
    return s->ring.size();
}

/**
 * ensure_series_:
 * - Non-const accessor that returns a reference to the Series for 'metric',
 *   creating it if missing with the configured per-metric capacity.
 * - Used when the caller intends to modify the Series.
 *
 * Thread-safety:
 * - Locks the map while accessing/creating the Series entry.
 */
MemoryStore::Series &MemoryStore::ensure_series_(const std::string &metric) {
    std::scoped_lock lk(map_mtx_);
    auto [it, _] = series_.try_emplace(metric, per_metric_capacity_);
    return it->second;
}

/**
 * find_series_ (const):
 * - Const lookup helper. Returns a pointer to the Series for 'metric' or nullptr if not found.
 * - Does not lock the Series; callers decide if/when to lock the Series for read/write.
 *
 * Thread-safety:
 * - Locks the map while searching.
 */
const MemoryStore::Series *MemoryStore::find_series_(const std::string &metric) const {
    std::scoped_lock lk(map_mtx_);
    auto it = series_.find(metric);
    return (it == series_.end()) ? nullptr : &it->second;
}
