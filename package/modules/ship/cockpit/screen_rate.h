#pragma once
// Decides WHEN a cached cockpit screen is redrawn (pure, unit-tested). hz <= 0 means "every frame" (no caching);
// otherwise tick(now) is true at most hz times per second, keeping a steady rate even when frames are uneven.
namespace cockpit {

class ScreenRate {
public:
    void setHz(float hz) { period_ = hz > 0 ? 1.0 / (double)hz : 0.0; valid_ = false; }
    bool live() const { return period_ <= 0; }             // redraw every frame, do not cache
    void invalidate() { valid_ = false; }                  // force a redraw at the next tick (screen content changed shape)

    // true = redraw now. 'now' is seconds on any monotonic clock.
    bool tick(double now) {
        if (live()) return true;
        if (!valid_ || now < next_ - period_ - 1e-9) { valid_ = true; next_ = now + period_; return true; }   // first call / clock went backwards
        if (now + 1e-9 < next_) return false;
        next_ += period_;
        if (next_ < now) next_ = now + period_;             // fell far behind (a stall): do not burst-redraw to catch up
        return true;
    }

private:
    double period_ = 0, next_ = 0;
    bool valid_ = false;
};

} // namespace cockpit
