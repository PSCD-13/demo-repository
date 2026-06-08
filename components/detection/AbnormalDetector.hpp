#pragma once

namespace pscd::detection
{
    /// @brief helper enum for the class
    enum class AlertStatus
    {
        NORMAL,
        SUSPICIOUS,
        ALARMING
    };

    /// @brief class meant for detecting abnormal heart-rate
    class AbnormalDetector
    {
    public:
        // tuning parameters
        static constexpr int BUF_SIZE = 20;        // number of samples to be kept in buffer
        static constexpr int THRESHOLD = 3;        // consecutive hits before alarm
        static constexpr double SPIKE_RATIO = 0.7; // flag if value < SPIKE_RATIO * baseline mean

        AbnormalDetector();

        /// @brief evaluates a pair of HRV metrics against the stored baseline multiplied by SPIKE_RATIO
        /// @param rmssd Root-mean-square of successive RR interval differences
        /// @param sdnn Standard deviation of all RR intervals
        /// @return of 3 enum states; NORMAL if no issues, SUSPICIOUS if a single case of abnormality was detected, ALARMING if abnormality was detected a THRESHOLD amount of consecutive times
        AlertStatus process(double rmssd, double sdnn);

        /// @brief adds a HRV metric pair to the history
        /// @param rmssd Root-mean-square of successive RR interval differences
        /// @param sdnn Standard deviation of all RR intervals
        void pushBaseline(double rmssd, double sdnn);

        /// @brief returns the current count of consecutive abnormal readings
        /// @return current count of consecutive abnormal readings
        int getConsecCount() const { return consec_abno_; }

        /// @brief checks if the baseline history is fully populated
        /// @return true if it is populated, false otherwise
        bool isBaselineReady() const { return hist_count_ >= BUF_SIZE; }

    private:
        double rmssd_hist_[BUF_SIZE];
        double sdnn_hist_[BUF_SIZE];
        int hist_idx_;
        int hist_count_;
        int consec_abno_;

        bool isAbnormal(double rmssd, double sdnn) const;
        double bufMean(const double *buf, int count) const;
    };
}