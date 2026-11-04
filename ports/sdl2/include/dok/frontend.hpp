#pragma once
#include "settings.hpp"
#include "menu_strings.hpp"
#include "flow.hpp"
#include "text.hpp"
#include <functional>

namespace dok {
struct FrontendDataset {const Archive* archive;std::string filename;};
enum class MenuScreen {hidden,main,datasets,title,readme,levels,settings,graphics,sound,controls,keyboard,pad,
    bindings,key_slots,gameplay,interface,credits,resources,pause,confirm,capture,conflict,video_confirm,notice,ending,outcome,help,map,files,path};
enum class MenuCommand {start,resume,main,quit,apply_video,keep_video,revert_video,music_title,music_stop,music_story,retry,add_resource,select_source};
struct MenuFrame {int width=640,height=480;std::vector<std::uint32_t> pixels;};
class Frontend {
public:
    explicit Frontend(AppSettings& settings);
    void datasets(std::vector<FrontendDataset> values,const std::string& errors={});
    void handle(const SDL_Event& event,int window_width,int window_height);
    void update(std::uint64_t milliseconds);
    const MenuFrame& render(int output_width,int output_height);
    std::vector<MenuCommand> commands();
    void main_menu();
    void pause();
    void request_quit();
    void hide();
    void outcome(SessionPhase phase);
    void ending();
    void video_failed(const std::string& reason);
    void notice(const std::string& ru,const std::string& en);
    void resources(const ResourceSources& sources);
    void session_world(const World* world){world_=world;}
    void show_map();
    const std::string& pending_resource()const{return pending_resource_;}
    std::size_t pending_source()const{return pending_source_;}
    int focused_row()const{return focus_;}
    MenuScreen screen() const {return screen_;}
    bool active() const {return screen_!=MenuScreen::hidden;}
    bool capturing() const {return screen_==MenuScreen::capture;}
    bool video_pending() const {return screen_==MenuScreen::video_confirm;}
    std::size_t dataset_index() const {return dataset_;}
    void select_dataset(std::size_t index);
    void preview_level(int level);
    void window_mode_changed(bool fullscreen){draft_.fullscreen=fullscreen;}
    int level() const {return level_;}
    const VideoSettings& previous_video() const {return old_video_;}
private:
    struct Row {std::string label,value,help;bool enabled=true;};
    struct Hit {SDL_Rect rect;int row;};
    AppSettings& s_;
    std::vector<FrontendDataset> sets_;
    const World* world_=nullptr;
    struct SourceRow {std::size_t index;DatasetId dataset;std::string path;bool selected;};
    std::vector<SourceRow> source_rows_;
    struct FileRow {std::filesystem::path path;bool directory;};
    std::vector<FileRow> file_rows_;
    std::filesystem::path browser_path_=std::filesystem::current_path();
    std::string browser_error_,pending_resource_,path_buffer_;
    std::size_t pending_source_=0;
    std::map<MenuScreen,int> remembered_focus_;
    MenuScreen help_return_=MenuScreen::main,map_return_=MenuScreen::hidden;
    bool focused_=true,path_selected_=false,map_trigger_released_=false;
    std::size_t dataset_=0;
    MenuScreen screen_=MenuScreen::main,return_screen_=MenuScreen::main,notice_return_=MenuScreen::main;
    MenuScreen binding_parent_=MenuScreen::keyboard;
    int focus_=0,level_=1,page_=0,page_count_=1,credit_page_=0,action_=0,slot_=0;
    bool in_session_=false,pad_=false,confirm_quit_=false,fallback_=false,capture_start_=false;
    int capture_kind_=0,capture_value_=0; // 0 key, 1 mouse, 2 button, 3 trigger
    std::vector<std::pair<int,int>> conflicts_;
    std::uint64_t now_=0,video_deadline_=0;
    VideoSettings draft_,old_video_;
    std::string errors_,notice_ru_,notice_en_;
    std::vector<MenuCommand> commands_;
    std::vector<Hit> hits_;
    std::vector<Row> rows_;
    MenuFrame frame_;
    std::array<std::uint8_t,4096> font_{};
    std::array<std::uint8_t,4096> menu_font_{};
    std::array<std::uint8_t,2048> font8_{};
    Image background_;
    std::array<std::uint32_t,256> colors_{};
    std::uint32_t ink_=0xffe0e0e0,accent_=0xffffff00,shade_=0xff101010;
    const Archive* themed_=nullptr;
    std::map<std::string,std::pair<Image,std::array<std::uint32_t,256>>> art_;
    int line_height_=24,text_height_=16;
    int output_width_=0,output_height_=0;
    bool native_text_=false;
    int last_axis_=0;
    std::string tr(MenuString id) const;
    std::string tr(const char* ru,const char* en) const;
    const Archive* archive() const;
    void theme();
    void enter(MenuScreen screen,int focus=0);
    void back();
    void choose(int delta=0);
    void begin_game();
    void browse(const std::filesystem::path& path);
    void accept_binding();
    void capture(const SDL_Event& event);
    void build_rows();
    void fill(SDL_Rect r,std::uint32_t color);
    void box(SDL_Rect r);
    void text(const std::string& value,int x,int y,std::uint32_t color,bool encoded=false);
    void centered(const std::string& value,int y,std::uint32_t color);
    void paragraph(const std::string& value,SDL_Rect r,bool encoded=false);
    void picture(const std::string& name,const std::string& palette,SDL_Rect r);
    std::vector<TextPage> story(const std::string& resource,int columns,int rows) const;
    std::string action_name(int action) const;
    std::string binding_name(int action,bool gamepad,int slot=-1) const;
};
} // namespace dok
