#include "HeartRateProcessor.hpp"
#include <cmath>
#include <cstring>

/// @brief constructor
HeartRateProcessor::HeartRateProcessor()
    : dc_idx_(0), dc_count_(0), dc_sum_(0),
      rr_count_(0), beat_gap_(0), prev_ac_(0), was_rising_(false)
{
    memset(dc_buf_, 0, sizeof(dc_buf_));
    memset(rr_buf_, 0, sizeof(rr_buf_));
}

//public interfaces

double HeartRateProcessor::update(int rawIR) {
    dcPush(rawIR);

    if (dc_count_ < DC_WIN) { // wait for window to fill
        return 0.0;
    }

    const int32_t ac = dcRemove(rawIR);
    beat_gap_++;

    const bool rising = (ac > prev_ac_);

    // Falling edge after a rise = peak candidate
    if (was_rising_ && !rising
            && prev_ac_ >= MIN_AC_HEIGHT
            && beat_gap_ >= MIN_BEAT_GAP) {
        if (beat_gap_ <= MAX_BEAT_GAP) {
            rr_buf_[rr_count_ % BEATS_TO_AVG] = beat_gap_;
            rr_count_++;
        }
        beat_gap_ = 0;
    }

    was_rising_ = rising;
    prev_ac_ = ac;

    if (rr_count_ < BEATS_TO_AVG) {
        return 0.0;
    }

    double avg_rr = 0.0;
    for (int i = 0; i < BEATS_TO_AVG; i++) {
        avg_rr += rr_buf_[i];
    }
    avg_rr /= BEATS_TO_AVG;

    return SAMPLE_RATE_HZ * 60.0 / avg_rr;
}

double HeartRateProcessor::computeRMSSD() const {
    if (rr_count_ < BEATS_TO_AVG) return 0.0;

    // Oldest entry is at rr_count_ % BEATS_TO_AVG in the circular buffer
    const int start = rr_count_ % BEATS_TO_AVG;
    double sum_sq = 0.0;
    for (int i = 1; i < BEATS_TO_AVG; i++) {
        const double diff = rr_buf_[(start + i) % BEATS_TO_AVG] - rr_buf_[(start + i - 1) % BEATS_TO_AVG];
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / (BEATS_TO_AVG - 1));
}

double HeartRateProcessor::computeSDNN() const {
    if (rr_count_ < BEATS_TO_AVG) {
        return 0.0;
    }

    double avg = 0.0;
    for (int i = 0; i < BEATS_TO_AVG; i++) avg += rr_buf_[i];
    avg /= BEATS_TO_AVG;

    double var = 0.0;
    for (int i = 0; i < BEATS_TO_AVG; i++) {
        const double d = rr_buf_[i] - avg;
        var += d * d;
    }
    return sqrt(var / BEATS_TO_AVG);
}

//private helpers

void HeartRateProcessor::dcPush(int x) {
    if (dc_count_ == DC_WIN) {
        dc_sum_ -= dc_buf_[dc_idx_];
    }
    else {
        dc_count_++;
    }
    dc_buf_[dc_idx_] = x;
    dc_sum_ += x;
    dc_idx_ = (dc_idx_ + 1) % DC_WIN;
}

int32_t HeartRateProcessor::dcRemove(int x) const {
    if (dc_count_ == 0) {
        return 0;
    }
    return static_cast<int32_t>(x) - static_cast<int32_t>(dc_sum_ / dc_count_);
}
