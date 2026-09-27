#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <span>
#include <SDL3/SDL_endian.h>
#include <glm/glm.hpp>

namespace Eng {

class MessageReader {
public:
    MessageReader(std::span<const uint8_t> data) : m_data(data) {}

    template<typename T>
    T Read() {
        T value{};
        if (m_offset + sizeof(T) > m_data.size()) return value;
        std::memcpy(&value, m_data.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return value;
    }

    glm::vec2 ReadVec2() {
        float x = Read<float>(), y = Read<float>();
        return {x, y};
    }
    glm::vec3 ReadVec3() {
        float x = Read<float>(), y = Read<float>(), z = Read<float>();
        return {x, y, z};
    }

    uint32_t ReadU32BE() {
        uint32_t v = Read<uint32_t>();
        return SDL_Swap32BE(v);
    }

    std::string ReadString() {
        uint32_t len = ReadU32BE();
        if (m_offset + len > m_data.size()) return "";
        std::string s(reinterpret_cast<const char*>(m_data.data() + m_offset), len);
        m_offset += len;
        return s;
    }

    std::span<const uint8_t> ReadBytes(size_t count) {
        if (m_offset + count > m_data.size()) return {};
        auto result = m_data.subspan(m_offset, count);
        m_offset += count;
        return result;
    }

    bool HasMore() const { return m_offset < m_data.size(); }
    size_t Remaining() const { return m_data.size() - m_offset; }

private:
    std::span<const uint8_t> m_data;
    size_t m_offset = 0;
};

} // namespace Eng