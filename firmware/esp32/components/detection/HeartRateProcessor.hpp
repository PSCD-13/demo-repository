#pragma once
#include <cstdint>


/// @brief class responsible for converting IR sensor samples into BPM and HRV metrics
class HeartRateProcessor {
public:
    // tuning parameters
    static constexpr int SAMPLE_RATE_HZ = 100; // Sensor sample rate in Hz
    static constexpr int DC_WIN = 100; // sliding window size for DC removal
    static constexpr int MIN_BEAT_GAP = 40; // min samples between beats
    static constexpr int MAX_BEAT_GAP = 180; // max samples between beats
    static constexpr int BEATS_TO_AVG = 15;  // beats collected before reporting BPM
    static constexpr int MIN_AC_HEIGHT = 200; // ignore peaks below this amplitude

    HeartRateProcessor();


    /// @brief updates BPM
    /// @param rawIR raw IR sample
    /// @return BPM once BEATS_TO_AVG intervals have been collected, else 0
    double update(int rawIR);


    /// @brief calculates the Root-mean-square of successive RR interval differences
    /// @return RMSSD, or 0 if not enough samples yet
    double computeRMSSD() const;


    /// @brief calculates the Standard deviation of all RR intervals in the buffer
    /// @return SDNN, or 0 if not enough samples yet
    double computeSDNN() const;


    /// @brief checks if BEATS_TO_AVG amount of intervals has been collected
    /// @return true if BEATS_TO_AVG amount of intervals has been collected
    bool isReady() const { return rr_count_ >= BEATS_TO_AVG; }

private:
    // DC-removal sliding window
    int dc_buf_[DC_WIN];
    int dc_idx_;
    int dc_count_;
    int dc_sum_;

    // RR-interval circular buffer and peak-detection state
    int  rr_buf_[BEATS_TO_AVG];
    int  rr_count_;
    int  beat_gap_;
    int  prev_ac_;
    bool was_rising_;

    void dcPush(int x);
    int32_t dcRemove(int x) const;
};
