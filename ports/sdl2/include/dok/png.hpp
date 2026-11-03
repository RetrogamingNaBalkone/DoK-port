#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace dok {
// ARGB8888 words, top row first, tightly packed.
void save_png(const std::filesystem::path& path,int width,int height,const std::vector<std::uint32_t>& pixels);
}
