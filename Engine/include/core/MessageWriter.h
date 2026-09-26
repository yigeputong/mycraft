// Engine/include/net/MessageWriter.h
#pragma once
#include <vector>
#include <string>
#include <span>
#include <cstring>
#include <cstdint>
#include <SDL3/SDL_endian.h>

namespace Eng {

class MessageWriter {
public:
    template<typename T>
    void Write(const T& value) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
        m_buffer.insert(m_buffer.end(), p, p + sizeof(T));
    }

    void WriteU32BE(uint32_t v) {
        v = SDL_Swap32BE(v);
        Write(v);
    }

    void WriteString(const std::string& s) {
        WriteU32BE(static_cast<uint32_t>(s.size()));
        m_buffer.insert(m_buffer.end(), s.begin(), s.end());
    }

    void WriteBytes(std::span<const uint8_t> data) {
        m_buffer.insert(m_buffer.end(), data.begin(), data.end());
    }

    const std::vector<uint8_t>& GetBuffer() const { return m_buffer; }
    size_t Size() const { return m_buffer.size(); }

private:
    std::vector<uint8_t> m_buffer;
};

} // namespace Eng