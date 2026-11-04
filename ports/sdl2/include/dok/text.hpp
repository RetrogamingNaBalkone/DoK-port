#pragma once
#include "resources.hpp"
namespace dok {
std::string cp866(const std::string& utf8);
// Modern menu overlay only; original encoded DOS text keeps the source glyphs.
void menu_yo16(std::array<std::uint8_t,4096>& font);
using TextPage=std::vector<std::string>;
std::vector<TextPage> paginate_story(Bytes text,unsigned columns=38,unsigned rows=18);
}
