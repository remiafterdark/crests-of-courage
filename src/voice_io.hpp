

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace voice_io {

struct Frame {
    uint32_t seq = 0;
    std::vector<uint8_t> opus;
    bool loud = false;
};

class Device {
public:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    std::vector<std::string> microphones();

    void set(bool on, bool micOn, const std::string& mic, float micGain, float outGain);

    std::vector<Frame> take();

    void play(const std::string& who, uint32_t seq, const uint8_t* opus, size_t size, float gain, float pan);
    void forget(const std::string& who);
    void close();

    const std::string& problem() const;

private:
    struct State;
    std::unique_ptr<State> s_;
};

}
