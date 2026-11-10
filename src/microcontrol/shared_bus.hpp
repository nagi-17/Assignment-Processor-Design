#ifndef MICROCONTROL_SHARED_BUS_HPP
#define MICROCONTROL_SHARED_BUS_HPP

// ---------------------------------------------------------------------------
// 32-bit single-driver shared bus, one per pipeline stage.
//
// A micro-op drives exactly one source onto the bus and may latch the value
// into any number of destination registers.  A second, different driver in the
// same micro-op is recorded as a conflict so the unit tests can catch a badly
// written microprogram.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "microinstruction.hpp"

namespace mc {

class SharedBus {
public:
    void reset() {
        driver_ = BusDriver::None;
        value_ = 0;
        conflict_ = false;
    }

    void drive(BusDriver who, uint32_t value) {
        if (who == BusDriver::None)
            return;
        if (driver_ != BusDriver::None && driver_ != who)
            conflict_ = true;
        driver_ = who;
        value_ = value;
    }

    bool        driven() const   { return driver_ != BusDriver::None; }
    BusDriver   driver() const   { return driver_; }
    uint32_t    read() const     { return value_; }
    bool        conflict() const { return conflict_; }

private:
    BusDriver driver_{BusDriver::None};
    uint32_t  value_{0};
    bool      conflict_{false};
};

} // namespace mc

#endif // MICROCONTROL_SHARED_BUS_HPP
