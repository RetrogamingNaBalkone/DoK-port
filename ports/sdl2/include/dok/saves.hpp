#pragma once
#include "flow.hpp"
#include "audio.hpp"
#include <memory>
namespace dok {
inline constexpr std::uint32_t save_format_version=1,save_rules_version=2;
using Digest=std::array<std::uint8_t,32>;
Digest sha256(const std::vector<std::uint8_t>& data);
Digest save_fingerprint(const Archive& archive,const std::string& level);
struct SaveAudio {VoiceSnapshot voice;std::string resource;};
struct SaveMetadata {
    DatasetId dataset=DatasetId::unknown;std::string level,name;
    std::uint64_t timestamp=0,ticks=0;std::int16_t health=0,lives=0;
    Digest fingerprint{};
    std::uint32_t preview_width=0,preview_height=0;
    std::vector<std::uint32_t> preview;
};
struct SaveGame {
    SaveMetadata metadata;GameFlow::Snapshot flow;SaveAudio audio;
    std::unique_ptr<World> world;
};
struct SaveEntry {int slot=0;bool exists=false,valid=false,backup=false;std::string error;SaveMetadata metadata;};
// Core format and storage have no SDL dependency. slot 0 is quicksave.
std::vector<std::uint8_t> encode_save(const World&,const GameFlow::Snapshot&,const SaveAudio&,SaveMetadata);
SaveGame decode_save(const std::vector<std::uint8_t>&,const Archive&);
std::filesystem::path save_path(const std::filesystem::path& profile,DatasetId,int slot);
std::vector<std::uint8_t> read_save_file(const std::filesystem::path&);
void write_save_file(const std::filesystem::path&,const std::vector<std::uint8_t>&,const Archive&);
std::vector<SaveEntry> list_saves(const std::filesystem::path& profile,const Archive&);
void delete_save(const std::filesystem::path&);
}
