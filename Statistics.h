
#ifndef STATISTICS_H
#define STATISTICS_H

// Stats
// TODO: Move these elsewhere, as they are useful outside of just branch
// prediction or the FetchDelayStage; we could also make use of the event
// infrastructure that already exists (grep for STALL event)
struct OverflowableCount {
    unsigned long long count;
    bool overflowed;
    void inc(unsigned by) {
        if(count + by < count) {
            overflowed = true;
        }
        count++;
    }
};


#endif