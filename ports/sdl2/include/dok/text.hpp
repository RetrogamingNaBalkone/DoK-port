#pragma once
#include "resources.hpp"
namespace dok {
using TextPage=std::vector<std::string>;
std::vector<TextPage> paginate_story(Bytes text,unsigned columns=38,unsigned rows=18);
}
