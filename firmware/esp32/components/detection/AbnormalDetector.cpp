#include "AbnormalDetector.hpp"
#include <cstring>

// constructor
AbnormalDetector::AbnormalDetector()
    : hist_idx_(0), hist_count_(0), consec_abno_(0)
{
    memset(rmssd_hist_, 0, sizeof(rmssd_hist_));
    memset(sdnn_hist_,  0, sizeof(sdnn_hist_));
}

//public interface

AlertStatus AbnormalDetector::process(double rmssd, double sdnn) {
    if (!isAbnormal(rmssd, sdnn)) {
        consec_abno_ = 0;
        return AlertStatus::NORMAL;
    }

    consec_abno_++;

    if (consec_abno_ >= THRESHOLD) {
        consec_abno_ = 0;
        return AlertStatus::ALARMING;
    }

    return AlertStatus::SUSPICIOUS;
}

void AbnormalDetector::pushBaseline(double rmssd, double sdnn) {
    rmssd_hist_[hist_idx_] = rmssd;
    sdnn_hist_[hist_idx_]  = sdnn;
    hist_idx_ = (hist_idx_ + 1) % BUF_SIZE;

    if (hist_count_ < BUF_SIZE) {
        hist_count_++;
    }
}

// private helpers

bool AbnormalDetector::isAbnormal(double rmssd, double sdnn) const {
    if (hist_count_ < BUF_SIZE) {
        return false;
    }

    const double mean_rmssd = bufMean(rmssd_hist_, hist_count_);
    const double mean_sdnn = bufMean(sdnn_hist_,  hist_count_);

    const bool rmssd_drop = (mean_rmssd > 0.0) && (rmssd < SPIKE_RATIO * mean_rmssd);
    const bool sdnn_drop = (mean_sdnn  > 0.0) && (sdnn  < SPIKE_RATIO * mean_sdnn);

    return rmssd_drop && sdnn_drop;
}

double AbnormalDetector::bufMean(const double *buf, int count) const {
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += buf[i];
    }
    
    return sum / count;
}
