#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dok {
struct LaunchArguments {
    std::vector<std::pair<std::string,std::string>> options;
    std::optional<std::filesystem::path> user_directory;
    bool explicit_resources=false;
};
inline std::string ascii_lower(std::string value){
    for(auto& c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return value;
}
inline LaunchArguments parse_launch_arguments(int argc,char** argv){
    LaunchArguments result;unsigned resources=0;
    const std::vector<std::string> flags={"--hidden","--replay-live-input","--skip-intro"};
    const std::vector<std::string> values={"--res","--frames","--input-replay","--res-source","--screenshot","--level","--user-dir","--ui-script","--renderer","--filter","--frame-filter","--fxaa","--hud","--locale","--size","--software-mode"};
    for(int i=1;i<argc;++i){
        const std::string option=argv[i];std::string value;
        if(std::find(flags.begin(),flags.end(),option)==flags.end()){
            if(option=="--aspect"||option=="--fov")throw std::runtime_error("FOV and aspect are automatic; select a resolution with --size instead");
            if(std::find(values.begin(),values.end(),option)==values.end())throw std::runtime_error("Unknown option: "+option);
            if(i+1>=argc||std::string(argv[i+1]).empty()||std::string(argv[i+1]).rfind("--",0)==0)throw std::runtime_error("Missing value for "+option);
            value=argv[++i];
        }
        if(option=="--res"&&++resources>3)throw std::runtime_error("At most three --res arguments are allowed");
        if(option=="--user-dir")result.user_directory=std::filesystem::absolute(std::filesystem::u8path(value)).lexically_normal();
        result.options.emplace_back(option,value);
    }
    result.explicit_resources=resources!=0;return result;
}
inline bool portable_marker(const std::filesystem::path& directory){
    for(const auto& entry:std::filesystem::directory_iterator(directory)){
        const auto name=ascii_lower(entry.path().filename().u8string());
        std::error_code error;
        if((name=="portable"||name.rfind("portable.",0)==0)&&entry.is_regular_file(error))return true;
    }
    return false;
}
template<class PlatformDirectory>
std::pair<std::filesystem::path,std::string> select_profile(const LaunchArguments& arguments,const std::filesystem::path& executable,PlatformDirectory platform_directory){
    if(arguments.user_directory)return {*arguments.user_directory,"explicit"};
    if(portable_marker(executable))return {executable,"portable"};
    return {std::filesystem::absolute(platform_directory()).lexically_normal(),"platform"};
}
inline std::filesystem::path resolve_resource_path(const std::string& value,const std::filesystem::path& profile){
    const auto path=std::filesystem::u8path(value);
    return std::filesystem::absolute(path.is_absolute()?path:profile/path).lexically_normal();
}
inline std::string store_resource_path(const std::string& value,const std::filesystem::path& profile){
    const auto path=std::filesystem::absolute(std::filesystem::u8path(value)).lexically_normal();
    const auto relative=path.lexically_relative(profile);
    if(!relative.empty()&&*relative.begin()!=std::filesystem::path(".."))return relative.generic_u8string();
    return path.u8string();
}
} // namespace dok
