#pragma once
#include "resources.hpp"
namespace dok {
std::string cp866(const std::string& utf8);
using TextPage=std::vector<std::string>;
std::vector<TextPage> paginate_story(Bytes text,unsigned columns=38,unsigned rows=18);
}
