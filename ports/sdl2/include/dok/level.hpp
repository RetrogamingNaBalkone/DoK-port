#pragma once
#include "resources.hpp"
#include <map>

namespace dok {
enum class FieldKind { word, long_integer, character, image, animation, sound, text };
struct Field { FieldKind kind{}; int offset{}; std::int32_t number{}; std::string bytes; };
struct ObjectDefinition {
    std::uint8_t glyph{}, marker{};
    std::uint16_t class_id{}, flags{}, sprite_flags{};
    std::int16_t width_parameter{}, height_parameter{};
    std::string sprite, animation, sound, ignored_tail;
    std::vector<Field> fields;
    std::int32_t number(int offset) const;
    std::string resource(int offset) const;
};
struct LevelDefinition {
    DatasetId dataset{};
    std::string name,palette,light;
    int width{},height{},length{},cell_side{},cell_height{},floor_color{},ceiling_color{},min_distance{};
    unsigned ignored_step_ver{};
    std::vector<ObjectDefinition> objects;
    std::array<int,256> glyph_index{};
    std::map<std::string,std::vector<std::uint8_t>> maps;
    const ObjectDefinition* object(std::uint8_t glyph) const;
};
LevelDefinition load_level(const Archive& archive,const std::string& name);
}
