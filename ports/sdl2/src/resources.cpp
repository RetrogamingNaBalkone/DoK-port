#include "dok/resources.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace dok {
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
struct CatalogEntry { const char* name; std::uint32_t size; };
Image decode_image(Bytes b) {
    Image result;
    result.width=b.u16(0);result.height=b.u16(2);result.flags=b.u16(4);
    auto count=std::size_t(result.width)*result.height;
    require(result.width && result.height && b.size>=18 && count==b.size-18 && b.u32(10)==count,"Invalid indexed image");
    result.pixels.assign(b.data+18,b.data+b.size);
    return result;
}
#include "catalogs.inc"
template<std::size_t N> bool matches(const Archive& archive, const CatalogEntry (&catalog)[N]) {
    if (archive.entries().size() != N) return false;
    for (const auto& expected : catalog) {
        const auto* entry = archive.find(expected.name);
        if (!entry || entry->size != expected.size) return false;
    }
    return true;
}
}
std::uint16_t Bytes::u16(std::size_t at) const {
    require(at <= size && size - at >= 2, "Truncated 16-bit field");
    return std::uint16_t(data[at]) | (std::uint16_t(data[at+1]) << 8);
}
std::uint32_t Bytes::u32(std::size_t at) const {
    require(at <= size && size - at >= 4, "Truncated 32-bit field");
    return std::uint32_t(u16(at)) | (std::uint32_t(u16(at+2)) << 16);
}
const char* dataset_name(DatasetId id) {
    switch (id) {
        case DatasetId::old_gold: return "Old Gold";
        case DatasetId::kremlin_pkr: return "Kremlin PKR";
        case DatasetId::kremlin_pke: return "Kremlin PKE";
        default: return "Unknown resource set";
    }
}
Archive::Archive(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    Bytes b{bytes_.data(), bytes_.size()};
    auto count = b.u16(0);
    std::size_t end = 2 + std::size_t(count)*30;
    require(count != 0 && end <= b.size, "Invalid RES directory");
    for (unsigned i=0; i<count; ++i) {
        auto at = 2 + std::size_t(i)*30;
        auto* start = reinterpret_cast<const char*>(b.data+at);
        auto* nul = std::find(start, start+16, '\0');
        require(nul != start && nul != start+16, "Invalid RES name");
        Resource r{std::string(start, nul), b.u32(at+20), b.u32(at+16)};
        require(r.offset >= end && r.offset <= b.size && r.size <= b.size-r.offset,
                "Invalid RES range: " + r.name);
        // Match the DOS lookup contract: the first matching name wins.
        entries_.push_back(std::move(r));
    }
}
Archive Archive::open(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(bool(file), "Cannot open RES: " + path.u8string());
    auto length = file.tellg();
    require(length >= 2 && length <= 256*1024*1024, "Invalid or oversized RES");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    file.seekg(0); file.read(reinterpret_cast<char*>(data.data()), length);
    require(bool(file), "Cannot read RES: " + path.u8string());
    return Archive(std::move(data));
}
const Resource* Archive::find(const std::string& name) const {
    for (const auto& r : entries_) if (r.name == name) return &r;
    return nullptr;
}
Bytes Archive::get(const std::string& name) const {
    const auto* r = find(name);
    require(r != nullptr, "Missing resource: " + name);
    return {bytes_.data()+r->offset, r->size};
}
DatasetId Archive::identify() const {
    if (matches(*this, catalog_og)) return DatasetId::old_gold;
    if (matches(*this, catalog_ru)) return DatasetId::kremlin_pkr;
    if (matches(*this, catalog_en)) return DatasetId::kremlin_pke;
    return DatasetId::unknown;
}
Image Archive::image(const std::string& name) const {
    return decode_image(get(name));
}
std::vector<AnimationFrame> Archive::animation(const std::string& name) const {
    auto b=get(name);auto count=b.u16(0);
    std::size_t at=8+std::size_t(count)*10;
    require(count>0 && count<=32767 && at<=b.size,"Invalid animation directory");
    std::vector<AnimationFrame> result;
    for(unsigned i=0;i<count;++i){
        auto length=std::size_t(b.u32(at+10))+18;
        require(at<=b.size && length<=b.size-at,"Truncated animation image");
        AnimationFrame f;f.interval=b.u16(8+i*10);f.parameter_a=b.u16(10+i*10);f.parameter_b=b.u16(12+i*10);
        f.image=decode_image({b.data+at,length});result.push_back(std::move(f));at+=length;
    }
    require(at==b.size,"Unexpected animation trailing data");return result;
}
Sound Archive::sound(const std::string& name) const {
    auto b=get(name);
    require(b.size>=36 && b.data[2]=='S' && b.data[3]=='V' && b.data[4]=='M',"Invalid SVM signature");
    require(b.u16(5)==1 && b.u16(8)==8 && b.u16(10)==0 && b.u32(20)==b.size-36,"Unsupported SVM format");
    Sound result;result.frequency=b.u32(16);result.parameter_c=b.u16(12);result.parameter_d=b.u16(14);
    require(result.frequency>0,"Invalid SVM frequency");
    result.samples.assign(b.data+36,b.data+b.size);return result;
}
std::array<std::uint32_t, 256> Archive::palette(const std::string& name) const {
    auto b = get(name); require(b.size==768, "Invalid palette: " + name);
    std::array<std::uint32_t, 256> result{};
    for (unsigned i=0; i<256; ++i) {
        unsigned rgb[3];
        for (unsigned j=0; j<3; ++j) {
            unsigned v = b.data[i*3+j]; require(v<=63, "Non-VGA palette component");
            rgb[j]=(v<<2)|(v>>4);
        }
        result[i]=0xff000000u | (rgb[0]<<16) | (rgb[1]<<8) | rgb[2];
    }
    return result;
}
Bytes Archive::font(const std::string& name) const {
    require(name=="FONT8"||name=="FONT16","Unknown font dimensions");
    auto b=get(name);require(b.size==(name=="FONT8"?2048u:4096u),"Invalid bitmap font");return b;
}
Bytes Archive::light(const std::string& name) const {
    auto b=get(name);require(b.size==4096,"Invalid LIGHT table");return b;
}
}
