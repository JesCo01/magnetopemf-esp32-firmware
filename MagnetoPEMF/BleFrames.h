#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Version 1: A5, version, message id LE16, sequence, flags (START=1, END=2).
// id=0 is unsolicited telemetry. Commands/responses use a nonzero id.
namespace BleFrames {
constexpr size_t HEADER = 6;
constexpr size_t MAX_MESSAGE = 1024;
constexpr uint32_t TIMEOUT_MS = 3000;
enum class Result { Incomplete, Complete, Invalid };
class Receiver {
public:
    uint8_t data[MAX_MESSAGE + 1]{};
    size_t size = 0;
    uint16_t id = 0;
    void reset() { size = 0; active = false; next = 0; }
    Result accept(const uint8_t* p, size_t n, uint32_t now) {
        if (active && uint32_t(now - last) > TIMEOUT_MS) reset();
        if (n <= HEADER || p[0] != 0xA5 || p[1] != 1 || (p[5] & ~3)) {
            reset(); return Result::Invalid;
        }
        const uint16_t incoming = p[2] | uint16_t(p[3]) << 8;
        if (p[5] & 1) {
            reset();
            if (p[4] != 0) return Result::Invalid;
            active = true; id = incoming;
        }
        if (!active || incoming != id || p[4] != next || size + n - HEADER > MAX_MESSAGE) {
            reset(); return Result::Invalid;
        }
        memcpy(data + size, p + HEADER, n - HEADER);
        size += n - HEADER; data[size] = 0; last = now; ++next;
        if (p[5] & 2) { active = false; return Result::Complete; }
        if (next == 0) { reset(); return Result::Invalid; }
        return Result::Incomplete;
    }
private:
    bool active = false;
    uint8_t next = 0;
    uint32_t last = 0;
};
}
