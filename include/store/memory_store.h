//
// Created by Sebastian Ibarra on 10/8/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_MEMORY_STORE_H
#define SYSTEM_MONITORING_DASHBOARD_MEMORY_STORE_H

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

struct Sample {
    std::int64_t ts_ms{};
    double value{};
};

template<typename T>
class RingBuffer {

public:
    explicit RingBuffer(std::size_t cap) : buffer_(cap), cap_(cap), head_(0), tail_(0), size_(0) {}

    bool empty() const { return size_ == 0; }

    bool full() const { return size_ == cap_; }

    std::size_t size() const { return size_; }

    std::size_t capacity() const { return cap_; }

    void append(const T &x) {
        // Add element to head of buffer, move head forward
        buffer_[head_] = x;
        head_ = (head_ + 1) % cap_;

        if (size_ < cap_) {
            // If there is room increment size
            size_++;
        } else {
            // If buffer is full drop last element by incrementing tail
            tail_ = (tail_ + 1) % cap_;
        }
    }

    std::vector<T> snapshot() {
        // Declare out vector size of buffer;
        std::vector<T> out;
        out.reserve(size_);

        // Copy all elements from buffer to out
        for (std::size_t i = 0; i < size_; i++) {
            std::size_t idx = (tail_ + i) % cap_;
            out.push_back(buffer_[idx]);
        }

        // return out
        return out;
    }

    std::vector<T> range(std::int64_t from_ms, std::int64_t to_ms) const {
        // Declare out vector
        std::vector<T> out;

        // Copy all elements between from_ms and to_ms to out
        for (std::size_t i = 0; i < size_; i++) {
            std::size_t idx = (tail_ + i) % cap_;
            const T &s = buffer_[idx];
            if (s.ts_ms >= from_ms && s.ts_ms <= to_ms) out.push_back(s);
        }

        // return out
        return out;
    }

    void reset(std::size_t cap) {
        buffer_.assign(cap, T{});
        cap_ = cap;
        head_ = tail_ = size_ = 0;
    }


private:
    std::vector<T> buffer_;
    size_t cap_;
    size_t head_; // next write
    size_t tail_; // oldest write
    size_t size_; // current size
};


class MemoryStore {
public:
    explicit MemoryStore(std::size_t keep_seconds = 7200, std::size_t sample_period_s = 1);

    // Non-copyable, movable optional
    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;

    // Append a sample to a metric’s ring (creates ring if missing)
    void append(const std::string& metric, std::int64_t ts_ms, double value);

    // Query samples in [from_ms, to_ms] for a metric; returns oldest->newest
    std::vector<Sample> query(const std::string& metric,
                              std::int64_t from_ms,
                              std::int64_t to_ms) const;

    // Count points retained for a metric (0 if unknown)
    std::size_t count(const std::string& metric) const;

    // Capacity currently configured per metric (samples)
    std::size_t capacity_per_metric() const { return per_metric_capacity_; }

private:
    struct Series{
        explicit Series(std::size_t cap) : ring(cap) {}
        RingBuffer<Sample> ring;
        mutable std::mutex mtx; // guards ring
    };

    // Lazily creates a series if not exists (non-const)
    Series& ensure_series_(const std::string& metric);

    // Returns pointer if exists, else nullptr (const)
    const Series* find_series_(const std::string& metric) const;

    std::size_t per_metric_capacity_;
    std::size_t sample_period_s_;

    mutable std::mutex map_mtx_;
    std::unordered_map<std::string, Series> series_;
};

#endif //SYSTEM_MONITORING_DASHBOARD_MEMORY_STORE_H
