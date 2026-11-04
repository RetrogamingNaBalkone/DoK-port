#pragma once
#include "contracts.hpp"
#include "resources.hpp"
#include <string>

namespace dok {
// Catalogue values are UTF-8. Only the final bitmap-font boundary uses CP866.
std::string game_text(const std::string& key,LocaleId locale);
std::string game_message_key(const std::string& original_cp866);
std::string game_story_key(DatasetId dataset,const std::string& resource);
std::string game_level_name(DatasetId dataset,int level,LocaleId locale);
std::string game_dataset_title(DatasetId dataset,LocaleId locale);
std::string game_dataset_description(DatasetId dataset,LocaleId locale);
}
