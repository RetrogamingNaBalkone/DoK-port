#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dok {
enum class DatasetId { unknown, old_gold, kremlin_pkr, kremlin_pke };
const char* dataset_name(DatasetId id);
struct Resource { std::string name; std::uint32_t offset{}, size{}; };
struct Image { unsigned width{}, height{}; std::uint16_t flags{}; std::vector<std::uint8_t> pixels; };
struct AnimationFrame { std::uint16_t interval{},parameter_a{},parameter_b{}; Image image; };
struct Sound { std::uint32_t frequency{}; std::uint16_t parameter_c{},parameter_d{}; std::vector<std::uint8_t> samples; };
struct Bytes {
    const std::uint8_t* data{}; std::size_t size{};
    std::uint16_t u16(std::size_t at) const;
    std::uint32_t u32(std::size_t at) const;
};
class Archive {
public:
    explicit Archive(std::vector<std::uint8_t> bytes);
    static Archive open(const std::filesystem::path& path);
    const std::vector<Resource>& entries() const { return entries_; }
    const Resource* find(const std::string& name) const;
    Bytes get(const std::string& name) const;
    DatasetId identify() const;
    Image image(const std::string& name) const;
    std::vector<AnimationFrame> animation(const std::string& name) const;
    Sound sound(const std::string& name) const;
    Bytes font(const std::string& name) const;
    Bytes light(const std::string& name) const;
    std::array<std::uint32_t, 256> palette(const std::string& name) const;
private:
    std::vector<std::uint8_t> bytes_;
    std::vector<Resource> entries_;
};
}
