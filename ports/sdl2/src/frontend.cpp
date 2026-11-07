#include "dok/presentation.hpp"
#include "dok/frontend.hpp"
#include "dok/localization.hpp"
#include "dok/automap.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace dok {
namespace {
constexpr int actions=static_cast<int>(Action::count);
constexpr const char* title_sprite=u8"ЗАСТАВКА_СПР";
constexpr const char* title_palette=u8"ЗАСТАВКА_ПАЛ";
bool inside(SDL_Rect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
std::string decimal(float n){std::ostringstream s;s<<std::fixed<<std::setprecision(2)<<n;return s.str();}
// Original, deliberately small bootstrap font; game RES supplies the normal menu fonts.
std::array<std::uint8_t,4096> bootstrap_font(){
    std::array<std::uint8_t,4096> out{};
    const std::pair<const char*,const char*> glyphs[]={
        {"AА","01110100011000111111100011000110001"},{"BВ","11110100011000111110100011000111110"},
        {"CС","01111100001000010000100001000001111"},{"D","11110100011000110001100011000111110"},
        {"EЕЁ","11111100001000011110100001000011111"},{"F","11111100001000011110100001000010000"},
        {"G","01111100001000010111100011000101111"},{"HН","10001100011000111111100011000110001"},
        {"I","11111001000010000100001000010011111"},{"J","00111000100001000010100101001001100"},
        {"KК","10001100101010011000101001001010001"},{"L","10000100001000010000100001000011111"},
        {"MМ","10001110111010110101100011000110001"},{"N","10001110011010110011100011000110001"},
        {"OО","01110100011000110001100011000101110"},{"PР","11110100011000111110100001000010000"},
        {"Q","01110100011000110001101011001001101"},{"R","11110100011000111110101001001010001"},
        {"S","01111100001000001110000010000111110"},{"TТ","11111001000010000100001000010000100"},
        {"U","10001100011000110001100011000101110"},{"V","10001100011000110001100010101000100"},
        {"W","10001100011000110101101011010101010"},{"XХ","10001100010101000100010101000110001"},
        {"YУ","10001100010101000100001000100010000"},{"Z","11111000010001000100010001000011111"},
        {"Б","11111100001000011110100011000111110"},{"Г","11111100001000010000100001000010000"},
        {"Д","00110010100101001010010101111110001"},{"Ж","10101101010101000100010101010110101"},
        {"З","11110000010000101110000010000111110"},{"И","10001100011001110101110011000110001"},
        {"Й","01010001001000110011101011100110001"},{"Л","00111010010100101001100011000110001"},
        {"П","11111100011000110001100011000110001"},{"Ф","00100011101010110101101010111000100"},
        {"Ц","10010100101001010010100101111100001"},{"Ч","10001100011000101111000010000100001"},
        {"Ш","10101101011010110101101011010111111"},{"Щ","10101101011010110101101011111100001"},
        {"Ъ","11000010000100001110010010100101110"},{"Ы","10001100011000111101101011010111101"},
        {"Ь","10000100001000011110100011000111110"},{"Э","01110100010000100111000011000101110"},
        {"Ю","10010101011010111101101011010110010"},{"Я","01111100011000101111001010100110001"},
        {"0","01110100011001110101110011000101110"},{"1","00100011000010000100001000010001110"},
        {"2","01110100010000100010001000100011111"},{"3","11110000010000101110000010000111110"},
        {"4","00010001100101010010111110001000010"},{"5","11111100001000011110000010000111110"},
        {"6","01110100001000011110100011000101110"},{"7","11111000010001000100010000100001000"},
        {"8","01110100011000101110100011000101110"},{"9","01110100011000101111000010000101110"},
        {"-","00000000000000011111000000000000000"},{".","00000000000000000000000000011000110"},
        {":","00000011000110000000011000110000000"},{"/","00001000010001000100010001000010000"},
        {"?","01110100010000100010001000000000100"},{"!","00100001000010000100001000000000100"},
        {"(","00010001000100001000010000010000010"},{")","01000001000001000010000100010001000"},
        {"<","00001000100010001000001000001000001"},{">","10000010000010000010001000100010000"},
        {"+","00000001000010011111001000010000000"},{"%","11001110100001000100010000101110011"},
        {"_","00000000000000000000000000000011111"},{"=","00000000001111100000111110000000000"}
    };
    for(const auto& pair:glyphs)for(unsigned char c:cp866(pair.first))for(int y=0;y<7;++y){
        unsigned bits=0;for(int x=0;x<5;++x)bits=(bits<<1)|(pair.second[y*5+x]=='1');
        out[c*16+y*2+1]=out[c*16+y*2+2]=static_cast<std::uint8_t>(bits<<2);
    }
    for(int c='a';c<='z';++c)std::copy_n(out.begin()+(c-32)*16,16,out.begin()+c*16);
    for(int c=0xa0;c<=0xaf;++c)std::copy_n(out.begin()+(c-32)*16,16,out.begin()+c*16);
    for(int c=0xe0;c<=0xef;++c)std::copy_n(out.begin()+(c-80)*16,16,out.begin()+c*16);
    return out;
}
}
Frontend::Frontend(AppSettings& settings):s_(settings){font_=bootstrap_font();menu_font_=font_;menu_yo16(menu_font_);for(int c=0;c<256;++c)for(int y=0;y<8;++y)font8_[c*8+y]=menu_font_[c*16+y*2+1];frame_.pixels.resize(640*480);}
std::string Frontend::tr(MenuString id)const{return menu_text(id,s_.locale);}
std::string Frontend::tr(const char* ru,const char* en)const{return s_.locale==LocaleId::ru?ru:en;}
const Archive* Frontend::archive()const{return sets_.empty()?nullptr:sets_[dataset_].archive;}
void Frontend::datasets(std::vector<FrontendDataset> sets,const std::string& errors){
    sets_=std::move(sets);errors_=errors;dataset_=0;
    for(std::size_t i=0;i<sets_.size();++i)if(sets_[i].archive->identify()==s_.last_dataset)dataset_=i;
    themed_=nullptr;art_.clear();
    // RES admission can remove the selected edition when two sources conflict.
    // The previous archive pointers are already invalid after main's refresh;
    // use the saved identity, never dereference the old list. Game-specific
    // screens cannot be rendered with a fallback edition (e.g. OG has no GAMEP1).
    const bool selected_available=std::any_of(sets_.begin(),sets_.end(),[&](const auto& set){return set.archive->identify()==s_.last_dataset;});
    const auto needs_selected_game=[](MenuScreen screen){switch(screen){
    case MenuScreen::title:case MenuScreen::readme:case MenuScreen::levels:case MenuScreen::dataset_menu:
    case MenuScreen::save_slots:case MenuScreen::load_slots:case MenuScreen::save_entry:
    case MenuScreen::save_name:case MenuScreen::save_confirm:case MenuScreen::ending:case MenuScreen::outcome:
        return true;
    default:return false;
    }};
    if(!selected_available&&(needs_selected_game(screen_)||
       (screen_==MenuScreen::notice&&needs_selected_game(notice_return_))))main_menu();
}
void Frontend::select_dataset(std::size_t index){if(index<sets_.size()){dataset_=index;s_.last_dataset=archive()->identify();themed_=nullptr;art_.clear();}}
void Frontend::preview_level(int level){level_=level;in_session_=false;enter(MenuScreen::levels);}
void Frontend::enter(MenuScreen screen,int focus){if(screen_==MenuScreen::path||screen_==MenuScreen::save_name)SDL_StopTextInput();remembered_focus_[screen_]=focus_;if(focus_>=0&&focus_<int(rows_.size()))remembered_item_[screen_]=rows_[focus_].id;rows_.clear();screen_=screen;focus_=focus;page_=0;capture_start_=false;last_axis_=0;hits_.clear();}
void Frontend::enter_item(MenuScreen screen,Item item){
    enter(screen);build_rows();
    for(int i=0;i<int(rows_.size());++i)if(rows_[i].id==item&&rows_[i].enabled){focus_=i;break;}
}
Frontend::Item Frontend::resource_item(DatasetId id){return id==DatasetId::kremlin_pkr?Item::resource_pkr:id==DatasetId::kremlin_pke?Item::resource_pke:Item::resource_gold;}
void Frontend::resources(const ResourceSources& sources){
    source_rows_.clear();for(std::size_t i=0;i<sources.sources().size();++i){const auto& source=sources.sources()[i];const auto id=source.archive.identify();source_rows_.push_back({i,id,source.path.u8string(),sources.selected(id)==i});}
}
void Frontend::show_map(){if(!world_)return;map_trigger_released_=true;map_return_=screen_;enter(MenuScreen::map);}
void Frontend::browse(const std::filesystem::path& path){
    std::error_code error;auto absolute=std::filesystem::absolute(path,error);if(error)return;
    std::vector<FileRow> rows;
    for(std::filesystem::directory_iterator i(absolute,std::filesystem::directory_options::skip_permission_denied,error),end;i!=end&&!error;i.increment(error)){
        std::error_code type_error;const bool dir=i->is_directory(type_error);auto ext=i->path().extension().u8string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!type_error&&(dir||ext==".res"))rows.push_back({i->path(),dir});
        if(rows.size()>=1024)break;
    }
    if(error){notice("Не удалось открыть папку: "+error.message(),"Could not open folder: "+error.message());return;}
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.directory!=b.directory?a.directory>b.directory:a.path.filename()<b.path.filename();});
    browser_path_=absolute;file_rows_=std::move(rows);browser_error_.clear();enter(MenuScreen::files);
}
void Frontend::hide(){enter(MenuScreen::hidden);in_session_=true;}
void Frontend::main_menu(){in_session_=false;enter(MenuScreen::main);commands_.push_back(MenuCommand::music_stop);}
void Frontend::pause(){in_session_=true;enter(MenuScreen::pause);}
void Frontend::request_quit(){confirm_quit_=true;fallback_=false;enter(MenuScreen::confirm,1);}
void Frontend::outcome(SessionPhase phase){in_session_=true;enter(MenuScreen::outcome);action_=int(phase);}
void Frontend::ending(){in_session_=true;enter(MenuScreen::ending);commands_.push_back(MenuCommand::music_stop);}
void Frontend::notice(const std::string& ru,const std::string& en){notice_return_=screen_;notice_ru_=ru;notice_en_=en;enter(MenuScreen::notice);}
void Frontend::video_failed(const std::string& why){
    s_.video=old_video_;draft_=old_video_;fallback_=true;notice_en_=why;enter(MenuScreen::confirm,1);
}
std::vector<MenuCommand> Frontend::commands(){auto result=std::move(commands_);commands_.clear();return result;}
void Frontend::update(std::uint64_t ms){now_=ms;if(video_pending()&&ms>=video_deadline_){s_.video=old_video_;draft_=old_video_;enter_item(MenuScreen::graphics,Item::apply_video);commands_.push_back(MenuCommand::revert_video);}}
void Frontend::begin_game(){
    if(!archive()){enter(MenuScreen::resources,0);return;}
    s_.last_dataset=archive()->identify();level_=archive()->identify()==DatasetId::old_gold?4:1;
    enter(MenuScreen::title);commands_.push_back(MenuCommand::music_title);
}
std::string Frontend::action_name(int a)const{
    static const char* ru[]={"Вперёд","Назад","Шаг влево","Шаг вправо","Поворот влево","Поворот вправо","Огонь","Открыть дверь","Карта","Пауза","Бег","Медленное движение","Следующее оружие","Предыдущее оружие","Быстрое сохранение","Быстрая загрузка","Модификатор стрейфа","Смена оружия (1)","Смена оружия (2)"};
    static const char* en[]={"Forward","Backward","Strafe left","Strafe right","Turn left","Turn right","Fire","Open door","Map","Pause","Run","Slow movement","Next weapon","Previous weapon","Quicksave","Quickload","Strafe modifier","Switch weapon (1)","Switch weapon (2)"};
    return tr(ru[a],en[a]);
}
std::string Frontend::binding_name(int a,bool pad,int slot)const{
    const auto& b=s_.input;
    if(pad){
        if(b.triggers[a]==SDL_CONTROLLER_AXIS_TRIGGERLEFT)return "L2";if(b.triggers[a]==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)return "R2";
        const auto button=b.buttons[a];
        switch(button){case SDL_CONTROLLER_BUTTON_A:return tr(MenuString::south_a);case SDL_CONTROLLER_BUTTON_B:return tr(MenuString::east_b);
        case SDL_CONTROLLER_BUTTON_X:return tr(MenuString::west_x);case SDL_CONTROLLER_BUTTON_Y:return tr(MenuString::north_y);
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return "L1";case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return "R1";
        case SDL_CONTROLLER_BUTTON_BACK:return "Select";case SDL_CONTROLLER_BUTTON_START:return "Start";default:break;}
        const char* name=SDL_GameControllerGetStringForButton(button);return name?name:tr(MenuString::unbound);
    }
    const auto mouse_name=[&]{return b.mouse[a]==6?tr(MenuString::wheel_up):b.mouse[a]==7?tr(MenuString::wheel_down):tr(MenuString::mouse)+std::to_string(b.mouse[a]);};
    if(slot==2)return b.mouse[a]?mouse_name():tr(MenuString::unbound);
    auto key=[&](int n){const char* name=SDL_GetScancodeName(b.keys[a][n]);return std::string(name&&*name?name:"-");};
    if(slot>=0)return key(slot);
    auto name=key(0);if(b.keys[a][1])name+=" / "+key(1);if(b.mouse[a])name+=" + "+mouse_name();return name;
}
void Frontend::build_rows(){
    const auto selected=focus_>=0&&focus_<int(rows_.size())?rows_[focus_].id:Item::none;
    rows_.clear();auto row=[&](std::string label,std::string value={},std::string help={},bool enabled=true){rows_.push_back({std::move(label),std::move(value),std::move(help),enabled});};
    const auto item=[&](Item id,std::string label,std::string value={},std::string help={},bool enabled=true,bool adjustable=false){
        row(std::move(label),std::move(value),std::move(help),enabled);rows_.back().id=id;rows_.back().adjustable=adjustable;
    };
    const auto setting=[&](Item id,std::string label,std::string value,std::string help={},bool enabled=true){item(id,std::move(label),std::move(value),std::move(help),enabled,true);};
    const auto finish=[&]{item(Item::defaults,tr(MenuString::restore_defaults));item(Item::back,tr(MenuString::back));};
    const auto on=[&](bool b){return b?tr(MenuString::on):tr(MenuString::off);};
    const auto defaults=[&]{row(tr(MenuString::restore_defaults));};
    const auto backrow=[&]{row(tr(MenuString::back));};
    const auto preset=[&](InputPreset p){return p==InputPreset::classic?tr(MenuString::classic):p==InputPreset::modern?tr(MenuString::modern):tr(MenuString::custom);};
    switch(screen_){
    case MenuScreen::main:row(tr(MenuString::start_game));row(tr(MenuString::settings));row(tr("Ресурсы игр","Game resources"));row(tr(MenuString::credits));row(tr(MenuString::quit));break;
    case MenuScreen::datasets:
        for(const auto& item:sets_){const auto id=item.archive->identify();row(game_dataset_title(id,s_.locale),
            id==DatasetId::kremlin_pkr?tr(MenuString::russian_edition_pkr):id==DatasetId::kremlin_pke?tr(MenuString::english_edition_pke):std::string{},
            game_dataset_description(id,s_.locale));}backrow();break;
    case MenuScreen::settings:
        item(Item::graphics,tr(MenuString::graphics));item(Item::sound,tr(MenuString::sound));
        item(Item::controls,tr(MenuString::controls));item(Item::gameplay,tr(MenuString::gameplay));item(Item::back,tr(MenuString::back));break;
    case MenuScreen::graphics:{
        const bool software=draft_.renderer==RendererKind::software;
        setting(Item::renderer,tr(MenuString::renderer),software?tr(MenuString::software):"OpenGL");
        setting(Item::window_mode,tr(MenuString::window_mode),draft_.fullscreen?tr(MenuString::borderless):tr(MenuString::windowed));
        auto mode=software?software_modes[draft_.software_mode]:draft_.fullscreen?std::array<int,2>{draft_.fullscreen_width,draft_.fullscreen_height}:std::array<int,2>{draft_.width,draft_.height};
        setting(Item::resolution,software?tr(MenuString::original_resolution):draft_.fullscreen?tr("Разрешение кадра","Frame resolution"):tr(MenuString::window_resolution),std::to_string(mode[0])+" x "+std::to_string(mode[1]),
            software?tr(MenuString::software_framebuffer_size_also_resizes_a_window_manual):
            tr("В fullscreen кадр вписывается с полями. FOV рассчитывается автоматически.","Fullscreen fits the frame with bars. FOV is automatic."));
        const bool current_window=!draft_.fullscreen&&draft_.width==s_.video.width&&draft_.height==s_.video.height&&!s_.video.fullscreen;
        const auto aspect_mode=(software||current_window)&&output_width_>0?std::array<int,2>{output_width_,output_height_}:mode;
        item(Item::aspect,tr(MenuString::video_aspect_ratio),software?"4:3":aspect_label(aspect_mode[0],aspect_mode[1]),software?tr("Программный рендерер использует кадр 4:3 с полями.","The software renderer uses a 4:3 frame with bars."):tr("Определяется разрешением. Обзор: Hor+ и Vert+.","Determined by resolution. View expands with Hor+ and Vert+."),false);
        item(Item::apply_video,tr(MenuString::apply_video_mode),{},{},draft_!=s_.video);
        setting(Item::filtering,tr(software?MenuString::video_frame_scaling:MenuString::video_texture_filter),
            software?(s_.frame_filter==3?"xBR":s_.frame_filter==2?"Sharp Bilinear":s_.frame_filter==1?"Linear":"Nearest"):(s_.texture_filter()?"Smooth":"Nearest"),
            software?tr("Увеличение кадра сцены. xBR — экспериментальное сглаживание пиксельных контуров.","Scene scaling. xBR is experimental pixel contour smoothing."):s_.texture_filter()?tr("Смягчает пиксели текстур и уменьшает рябь вдали. Интерфейс остаётся чётким.","Softens texture pixels and reduces distant shimmer. The interface stays crisp."):tr("Сохраняет резкие границы исходных пикселей текстур.","Keeps the sharp edges of the original texture pixels."));
        setting(Item::fxaa,"FXAA",on(s_.fxaa),tr(MenuString::video_fxaa_help),!software);
        setting(Item::hud,tr(MenuString::video_hud),s_.auto_hud?tr(MenuString::automatic):s_.classic_hud?tr(MenuString::classic):tr(MenuString::modern));
        setting(Item::language,"Язык / Language",s_.locale==LocaleId::ru?"Русский":"English",tr(MenuString::all_game_texts_follow_selected_language));
        setting(Item::fps,tr(MenuString::show_fps),on(s_.show_fps));finish();break;}
    case MenuScreen::sound:
        setting(Item::effects,tr(MenuString::effects),std::to_string(s_.effects_volume)+"%");
        setting(Item::music,tr(MenuString::title_music),std::to_string(s_.music_volume)+"%");finish();break;
    case MenuScreen::controls:item(Item::keyboard,tr(MenuString::keyboard_and_mouse));item(Item::pad,tr(MenuString::gamepad));item(Item::back,tr(MenuString::back));break;
    case MenuScreen::keyboard:case MenuScreen::pad:{
        const bool pad=screen_==MenuScreen::pad;
        setting(Item::device_enabled,tr(pad?MenuString::gamepad_in_game:MenuString::mouse_in_game),on(pad?s_.input.gamepad_enabled:s_.input.mouse_enabled),tr(MenuString::device_in_game_help));
        setting(Item::preset,tr(MenuString::preset),preset(pad?s_.input.pad_preset:s_.input.keyboard_preset));
        item(Item::bindings,tr(MenuString::remap_controls));
        setting(Item::sensitivity,tr(pad?MenuString::turn_sensitivity:MenuString::mouse_sensitivity),decimal(pad?s_.input.turn_sensitivity:s_.input.mouse_degrees_per_count));
        if(pad)setting(Item::deadzone,tr(MenuString::deadzone),std::to_string(int(s_.input.stick_deadzone*100+0.5f))+"%");
        finish();break;}
    case MenuScreen::bindings:
        for(int i=0;i<actions;++i)row(action_name(i),binding_name(i,binding_parent_==MenuScreen::pad),tr(MenuString::select_an_action_to_change_its_binding));backrow();break;
    case MenuScreen::key_slots:row(tr(MenuString::primary_key),binding_name(action_,false,0));row(tr(MenuString::secondary_key),binding_name(action_,false,1));row(tr(MenuString::mouse_button),binding_name(action_,false,2));row(tr(MenuString::clear_bindings));backrow();break;
    case MenuScreen::gameplay:
        setting(Item::inertia,tr(MenuString::movement_inertia),on(s_.inertia),tr(MenuString::off_instant_start_and_stop_water_currents_still_move_th));
        setting(Item::saving,tr(MenuString::saving),on(s_.saves_enabled),tr("В оригинале сохранений не было. Выкл.: сохранение и загрузка игры недоступны, включая F5/F9. Файлы остаются.","The originals had no saves. Off disables saving and loading, including F5/F9. Existing files are kept."));
        setting(Item::camera_bobbing,tr("Покачивание в «Старом Золоте»","Bobbing in Old Gold"),on(s_.camera_bobbing),tr("Покачивание камеры при движении в «Старом Золоте». Не влияет на «Подземелья Кремля».","Camera bobbing while moving in Old Gold. Does not affect Kremlin Dungeon."));
        setting(Item::boundary_hits,tr("Исправление попаданий","Hit detection fix"),on(s_.boundary_hits),tr("Исправляет промахи рядом с монстром на границе клеток. Выкл.: поведение оригинала. Правила простреливания стен и дверей сохраняются.","Fixes near misses at monster cell boundaries. Off: original behavior. Preserves wall and door penetration rules."));
        finish();break;
    case MenuScreen::pause:row(tr(MenuString::resume));row(tr(MenuString::settings));row(tr("Карта","Map"),{},{},world_!=nullptr);row(tr("Сохранить игру","Save game"),{},{},save_allowed());row(tr("Загрузить игру","Load game"),{},{},s_.saves_enabled);row(tr("Быстро сохранить","Quicksave"),"F5",{},save_allowed());row(tr("Быстро загрузить","Quickload"),"F9",{},s_.saves_enabled);row(tr(MenuString::main_menu));row(tr(MenuString::quit));break;
    case MenuScreen::dataset_menu:row(tr("Новая игра","New game"));row(tr("Загрузить игру","Load game"),{},{},s_.saves_enabled);backrow();break;
    case MenuScreen::save_slots:case MenuScreen::load_slots:
        for(int i=screen_==MenuScreen::save_slots?1:0;i<=10;++i){const auto& e=saves_.at(i);row((i?std::to_string(i)+". ":tr("Быстрое: ","Quick: "))+(e.exists?(e.valid?e.metadata.name:tr("Повреждено / несовместимо","Damaged / incompatible")):tr("Пусто","Empty")),{},e.error);}
        backrow();break;
    case MenuScreen::save_entry:row(tr("Загрузить","Load"),{},{},saves_.at(save_slot_).valid);row(tr("Удалить…","Delete?"));backrow();break;
    case MenuScreen::save_name:row(tr("Сохранить","Save"));backrow();break;
    case MenuScreen::save_confirm:row(tr(MenuString::yes));row(tr(MenuString::cancel));break;
    case MenuScreen::confirm:row(fallback_?tr(MenuString::use_software_renderer):tr(MenuString::yes));row(tr(MenuString::cancel));break;
    case MenuScreen::conflict:row(tr(MenuString::replace_binding));row(tr(MenuString::cancel));break;
    case MenuScreen::video_unsaved:row(tr(MenuString::apply_video_mode));row(tr(MenuString::discard_video_changes));row(tr(MenuString::continue_video_setup));break;
    case MenuScreen::video_confirm:row(tr(MenuString::keep_video_mode));row(tr(MenuString::revert));break;
    case MenuScreen::resources:
        for(const auto id:{DatasetId::old_gold,DatasetId::kremlin_pkr,DatasetId::kremlin_pke}){
            int count=0;const SourceRow* selected=nullptr;
            for(const auto& source:source_rows_)if(source.dataset==id){++count;if(source.selected)selected=&source;}
            const auto label=game_dataset_title(id,s_.locale)+(id==DatasetId::kremlin_pkr?tr(MenuString::russian_resource_suffix):id==DatasetId::kremlin_pke?tr(MenuString::english_resource_suffix):"");
            item(resource_item(id),label,tr(selected?MenuString::resource_ready:count?MenuString::resource_choose:MenuString::resource_missing),
                selected?selected->path:tr(MenuString::resource_select_help));
        }
        item(Item::back,tr(MenuString::back));break;
    case MenuScreen::files:
        row(tr("На папку выше","Parent folder"),"..",browser_path_.u8string());
        row(tr("Ввести путь…","Enter a path…"),{},tr("Можно открыть другую папку или вставить полный путь к RES.","Open another folder or paste the full path to a RES file."));
        for(const auto& file:file_rows_)row((file.directory?"[+] ":"")+file.path.filename().u8string(),file.directory?tr("Папка","Folder"):"RES",file.path.u8string());
        backrow();break;
    case MenuScreen::path:row(tr("Открыть","Open"));backrow();break;
    case MenuScreen::map:row(tr(MenuString::back));break;
    case MenuScreen::help:if(page_count_>1)row(tr(MenuString::next_page));backrow();break;
    case MenuScreen::notice:backrow();break;
    case MenuScreen::credits:if(page_count_>1)row(tr(MenuString::next_page));backrow();break;
    case MenuScreen::levels:
        row(tr(MenuString::start));if(page_count_>1)row(tr(MenuString::next_page));backrow();break;
    case MenuScreen::readme:row(tr(MenuString::continue_action));backrow();break;
    case MenuScreen::ending:row(tr(MenuString::to_menu));break;
    case MenuScreen::title:row(tr(MenuString::continue_2));backrow();break;
    case MenuScreen::outcome:row(action_==int(SessionPhase::retry)?tr(MenuString::retry):tr(MenuString::level_selection));row(tr(MenuString::main_menu));row(tr("Загрузить игру","Load game"),{},{},s_.saves_enabled);row(tr("Быстро загрузить","Quickload"),"F9",{},s_.saves_enabled);break;
    default:break;
    }
    if(!rows_.empty()){
        if(selected!=Item::none)for(int i=0;i<int(rows_.size());++i)if(rows_[i].id==selected){focus_=i;break;}
        focus_=std::clamp(focus_,0,int(rows_.size())-1);
        for(int i=0;i<int(rows_.size())&&!rows_[focus_].enabled;++i)focus_=(focus_+1)%int(rows_.size());
    }
}
void Frontend::apply_video(){
    if(draft_==s_.video)return;
    old_video_=s_.video;s_.video=draft_;video_deadline_=now_+15000;
    enter(MenuScreen::video_confirm);commands_.push_back(MenuCommand::apply_video);
}
void Frontend::back(){
    switch(screen_){
    case MenuScreen::hidden:pause();break;
    case MenuScreen::main:commands_.push_back(MenuCommand::quit);break;
    case MenuScreen::pause:hide();commands_.push_back(MenuCommand::resume);break;
    case MenuScreen::settings:enter(in_session_?MenuScreen::pause:MenuScreen::main,1);break;
    case MenuScreen::graphics:
        if(draft_!=s_.video){enter(MenuScreen::video_unsaved,2);break;}
        enter_item(MenuScreen::settings,Item::graphics);break;
    case MenuScreen::video_unsaved:enter_item(MenuScreen::graphics,remembered_item_[MenuScreen::graphics]);break;
    case MenuScreen::sound:enter_item(MenuScreen::settings,Item::sound);break;
    case MenuScreen::controls:enter_item(MenuScreen::settings,Item::controls);break;
    case MenuScreen::gameplay:enter_item(MenuScreen::settings,Item::gameplay);break;
    case MenuScreen::keyboard:case MenuScreen::pad:enter_item(MenuScreen::controls,screen_==MenuScreen::pad?Item::pad:Item::keyboard);break;
    case MenuScreen::bindings:enter_item(binding_parent_,Item::bindings);break;
    case MenuScreen::key_slots:enter(MenuScreen::bindings,action_);break;
    case MenuScreen::capture:case MenuScreen::conflict:enter(binding_parent_==MenuScreen::pad?MenuScreen::bindings:MenuScreen::key_slots,binding_parent_==MenuScreen::pad?action_:slot_);break;
    case MenuScreen::confirm:if(fallback_){fallback_=false;enter_item(MenuScreen::graphics,Item::apply_video);}else enter(MenuScreen::pause,remembered_focus_[MenuScreen::pause]);break;
    case MenuScreen::notice:
        enter(notice_return_,remembered_focus_[notice_return_]);
        if(notice_return_==MenuScreen::hidden||notice_return_==MenuScreen::map)commands_.push_back(MenuCommand::resume);
        break;
    case MenuScreen::help:enter(help_return_,remembered_focus_[help_return_]);break;
    case MenuScreen::map:if(map_return_==MenuScreen::hidden){hide();commands_.push_back(MenuCommand::resume);}else enter(map_return_,remembered_focus_[map_return_]);break;
    case MenuScreen::files:enter_item(MenuScreen::resources,resource_item(resource_target_));break;
    case MenuScreen::path:enter(MenuScreen::files,1);break;
    case MenuScreen::resources:enter(MenuScreen::main,remembered_focus_[MenuScreen::main]);break;
    case MenuScreen::credits:enter(MenuScreen::main,4);break;
    case MenuScreen::title:case MenuScreen::dataset_menu:leave_game_menu();break;
    case MenuScreen::readme:case MenuScreen::levels:if(has_loadable_saves())enter(MenuScreen::dataset_menu);else leave_game_menu();break;
    case MenuScreen::save_slots:case MenuScreen::load_slots:
        if(save_return_==MenuScreen::dataset_menu&&!has_loadable_saves())leave_game_menu();
        else enter(save_return_,remembered_focus_[save_return_]);break;
    case MenuScreen::save_entry:enter(MenuScreen::load_slots,save_slot_);break;
    case MenuScreen::save_name:enter(MenuScreen::save_slots,save_slot_-1);break;
    case MenuScreen::save_confirm:enter(save_operation_==0?MenuScreen::save_name:MenuScreen::save_entry,save_operation_==0?0:1);if(save_operation_==0)SDL_StartTextInput();break;
    case MenuScreen::video_confirm:s_.video=old_video_;draft_=old_video_;enter_item(MenuScreen::graphics,Item::apply_video);commands_.push_back(MenuCommand::revert_video);break;
    case MenuScreen::ending:case MenuScreen::outcome:commands_.push_back(MenuCommand::main);main_menu();break;
    default:commands_.push_back(MenuCommand::music_stop);enter(MenuScreen::main);break;
    }
}
void Frontend::choose(int delta){
    build_rows();if(rows_.empty()||!rows_[focus_].enabled)return;
    const int d=delta?delta:1;
    switch(screen_){
    case MenuScreen::main:if(focus_==0){if(sets_.size()>1)enter(MenuScreen::datasets);else begin_game();}else if(focus_==1)enter(MenuScreen::settings);else if(focus_==2)enter(MenuScreen::resources,0);else if(focus_==3){credit_page_=0;enter(MenuScreen::credits);}else commands_.push_back(MenuCommand::quit);break;
    case MenuScreen::datasets:if(focus_==int(sets_.size()))back();else{select_dataset(focus_);begin_game();}break;
    case MenuScreen::title:if(focus_==1){back();break;}enter(has_loadable_saves()?MenuScreen::dataset_menu:archive()->identify()==DatasetId::old_gold?MenuScreen::levels:MenuScreen::readme);break;
    case MenuScreen::dataset_menu:
        if(focus_==0)enter(archive()->identify()==DatasetId::old_gold?MenuScreen::levels:MenuScreen::readme);
        else if(focus_==1)open_saves(false);else back();break;
    case MenuScreen::save_slots:case MenuScreen::load_slots:{const bool saving=screen_==MenuScreen::save_slots;const int slot=focus_+(saving?1:0);if(slot>10){back();break;}save_slot_=slot;
        if(saving){save_name_=saves_[slot].exists&&saves_[slot].valid?saves_[slot].metadata.name:game_level_name(archive()->identify(),level_,s_.locale);path_buffer_=save_name_;path_selected_=true;enter(MenuScreen::save_name);SDL_StartTextInput();}
        else if(saves_[slot].exists)enter(MenuScreen::save_entry);break;}
    case MenuScreen::save_name:
        if(focus_){back();break;}save_name_=path_buffer_;if(save_name_.empty())save_name_=game_level_name(archive()->identify(),level_,s_.locale);
        if(saves_[save_slot_].exists)confirm_save_action(0);else{commands_.push_back(MenuCommand::save_game);enter(MenuScreen::pause);}break;
    case MenuScreen::save_entry:
        if(focus_==0){if(in_session_||saves_[save_slot_].backup)confirm_save_action(1);else commands_.push_back(MenuCommand::load_game);}
        else if(focus_==1)confirm_save_action(2);else back();break;
    case MenuScreen::save_confirm:
        if(focus_){back();break;}
        commands_.push_back(save_operation_==0?MenuCommand::save_game:save_operation_==1?MenuCommand::load_game:MenuCommand::delete_game);
        if(save_operation_==0)enter(MenuScreen::pause);else if(save_operation_==2)enter(MenuScreen::load_slots,save_slot_);break;
    case MenuScreen::readme:
        if(focus_==1){back();break;}
        if(++page_>=page_count_)enter(MenuScreen::levels);break;
    case MenuScreen::levels:{
        if(focus_==0){commands_.push_back(MenuCommand::start);hide();}
        else if(page_count_>1&&focus_==1)page_=(page_+d+page_count_)%page_count_;else back();break;}
    case MenuScreen::settings:{
        switch(rows_[focus_].id){
        case Item::graphics:draft_=s_.video;enter(MenuScreen::graphics);break;
        case Item::sound:enter(MenuScreen::sound);break;
        case Item::controls:enter(MenuScreen::controls);break;
        case Item::gameplay:enter(MenuScreen::gameplay);break;
        default:back();break;
        }break;}
    case MenuScreen::graphics:
        if(rows_[focus_].id==Item::renderer)draft_.renderer=draft_.renderer==RendererKind::opengl?RendererKind::software:RendererKind::opengl;
        else if(rows_[focus_].id==Item::window_mode)draft_.fullscreen=!draft_.fullscreen;
        else if(rows_[focus_].id==Item::resolution){
            if(draft_.renderer==RendererKind::software){draft_.software_mode=(draft_.software_mode+d+5)%5;const auto size=software_modes[draft_.software_mode];draft_.width=size[0];draft_.height=size[1];}
            else{
                std::vector<std::array<int,2>> sizes{{640,480},{800,600},{1024,768},{1280,800},{1280,720},{1280,768},{1600,900},{1920,1080},{2560,1440}};
                SDL_DisplayMode desktop{};if(SDL_GetDesktopDisplayMode(0,&desktop)==0&&desktop.w>=320&&desktop.h>=200&&desktop.w<=7680&&desktop.h<=4320)sizes.push_back({desktop.w,desktop.h});
                int& w=draft_.fullscreen?draft_.fullscreen_width:draft_.width;
                int& h=draft_.fullscreen?draft_.fullscreen_height:draft_.height;
                if(std::find(sizes.begin(),sizes.end(),std::array<int,2>{w,h})==sizes.end())sizes.push_back({w,h});
                std::vector<std::array<int,2>> unique;for(const auto size:sizes)if(std::find(unique.begin(),unique.end(),size)==unique.end())unique.push_back(size);
                int at=0;for(int i=0;i<int(unique.size());++i)if(unique[i]==std::array<int,2>{w,h})at=i;
                at=(at+d+int(unique.size()))%int(unique.size());w=unique[at][0];h=unique[at][1];
            }
        }else if(rows_[focus_].id==Item::filtering){if(draft_.renderer==RendererKind::software)s_.frame_filter=(s_.frame_filter+d+4)%4;else s_.texture_filter(s_.texture_filter()?0:6);}
        else if(rows_[focus_].id==Item::apply_video){apply_video();}
        else if(rows_[focus_].id==Item::fxaa)s_.fxaa=!s_.fxaa;
        else if(rows_[focus_].id==Item::hud){
            int layout=s_.auto_hud?0:s_.classic_hud?1:2;layout=(layout+d+3)%3;
            s_.auto_hud=layout==0;s_.classic_hud=layout==1;
        }else if(rows_[focus_].id==Item::language)s_.locale=s_.locale==LocaleId::ru?LocaleId::en:LocaleId::ru;
        else if(rows_[focus_].id==Item::fps)s_.show_fps=!s_.show_fps;
        else if(rows_[focus_].id==Item::defaults){draft_=VideoSettings{};s_.linear=false;s_.mip_filter=0;s_.frame_filter=0;s_.fxaa=false;s_.classic_hud=false;s_.auto_hud=true;s_.locale=system_locale();s_.show_fps=false;}else back();break;
    case MenuScreen::video_unsaved:
        if(focus_==0)apply_video();
        else if(focus_==1){draft_=s_.video;enter_item(MenuScreen::settings,Item::graphics);}
        else back();break;
    case MenuScreen::sound:
        if(rows_[focus_].id==Item::effects)s_.effects_volume=std::clamp(s_.effects_volume+d*5,0,100);
        else if(rows_[focus_].id==Item::music)s_.music_volume=std::clamp(s_.music_volume+d*5,0,100);
        else if(rows_[focus_].id==Item::defaults)s_.effects_volume=s_.music_volume=100;
        else back();break;
    case MenuScreen::controls:if(rows_[focus_].id==Item::keyboard)enter(MenuScreen::keyboard);else if(rows_[focus_].id==Item::pad)enter(MenuScreen::pad);else back();break;
    case MenuScreen::keyboard:case MenuScreen::pad:{
        const bool pad=screen_==MenuScreen::pad;
        switch(rows_[focus_].id){
        case Item::device_enabled:if(pad)s_.input.gamepad_enabled=!s_.input.gamepad_enabled;else s_.input.mouse_enabled=!s_.input.mouse_enabled;break;
        case Item::preset:{const auto p=pad?s_.input.pad_preset:s_.input.keyboard_preset;const auto next=p==InputPreset::classic?InputPreset::modern:InputPreset::classic;if(pad)s_.input.gamepad(next);else s_.input.keyboard(next);break;}
        case Item::bindings:binding_parent_=screen_;enter(MenuScreen::bindings);break;
        case Item::sensitivity:if(pad)s_.input.turn_sensitivity=std::clamp(s_.input.turn_sensitivity+d*0.1f,0.1f,3.0f);else s_.input.mouse_degrees_per_count=std::clamp(s_.input.mouse_degrees_per_count+d*0.01f,0.01f,2.0f);break;
        case Item::deadzone:s_.input.stick_deadzone=std::clamp(s_.input.stick_deadzone+d*0.01f,0.0f,0.8f);break;
        case Item::defaults:if(pad){s_.input.gamepad_enabled=true;s_.input.gamepad(InputPreset::modern);s_.input.turn_sensitivity=1;s_.input.stick_deadzone=0.18f;}else{s_.input.mouse_enabled=true;s_.input.keyboard(InputPreset::modern);s_.input.mouse_degrees_per_count=0.15f;}break;
        default:back();break;
        }break;}
    case MenuScreen::bindings:if(focus_==actions)back();else{action_=focus_;enter(binding_parent_==MenuScreen::pad?MenuScreen::capture:MenuScreen::key_slots);}break;
    case MenuScreen::key_slots:if(focus_<3){slot_=focus_;enter(MenuScreen::capture);}else if(focus_==3){s_.input.keys[action_]={};s_.input.mouse[action_]=0;s_.input.keyboard_preset=InputPreset::custom;}else back();break;
    case MenuScreen::gameplay:if(rows_[focus_].id==Item::inertia)s_.inertia=!s_.inertia;else if(rows_[focus_].id==Item::saving)s_.saves_enabled=!s_.saves_enabled;else if(rows_[focus_].id==Item::camera_bobbing)s_.camera_bobbing=!s_.camera_bobbing;else if(rows_[focus_].id==Item::boundary_hits)s_.boundary_hits=!s_.boundary_hits;else if(rows_[focus_].id==Item::defaults){s_.inertia=true;s_.saves_enabled=true;s_.camera_bobbing=true;s_.boundary_hits=false;}else back();break;
    case MenuScreen::pause:if(focus_==0){hide();commands_.push_back(MenuCommand::resume);}else if(focus_==1)enter(MenuScreen::settings);else if(focus_==2)show_map();else if(focus_==3)open_saves(true);else if(focus_==4)open_saves(false);else if(focus_==5)commands_.push_back(MenuCommand::quicksave);else if(focus_==6)commands_.push_back(MenuCommand::quickload);else{confirm_quit_=focus_==8;fallback_=false;enter(MenuScreen::confirm,1);}break;
    case MenuScreen::confirm:if(focus_==1)back();else if(fallback_){fallback_=false;old_video_=s_.video;draft_=s_.video;draft_.renderer=RendererKind::software;s_.video=draft_;video_deadline_=now_+15000;enter(MenuScreen::video_confirm);commands_.push_back(MenuCommand::apply_video);}else{commands_.push_back(confirm_quit_?MenuCommand::quit:MenuCommand::main);if(!confirm_quit_)main_menu();}break;
    case MenuScreen::conflict:if(focus_==0)accept_binding();else back();break;
    case MenuScreen::video_confirm:if(focus_==0){draft_=s_.video;enter_item(MenuScreen::graphics,Item::apply_video);commands_.push_back(MenuCommand::keep_video);}else back();break;
    case MenuScreen::credits:if(page_count_>1&&focus_==0)credit_page_=(credit_page_+d+page_count_)%page_count_;else back();break;
    case MenuScreen::ending:back();break;
    case MenuScreen::outcome:if(focus_==2)open_saves(false);else if(focus_==3)commands_.push_back(MenuCommand::quickload);else if(focus_==1)back();else if(action_==int(SessionPhase::retry)){hide();commands_.push_back(MenuCommand::retry);}else{commands_.push_back(MenuCommand::main);in_session_=false;enter(MenuScreen::levels);}break;
    case MenuScreen::resources:
        if(rows_[focus_].id==Item::back){back();break;}
        resource_target_=rows_[focus_].id==Item::resource_pkr?DatasetId::kremlin_pkr:rows_[focus_].id==Item::resource_pke?DatasetId::kremlin_pke:DatasetId::old_gold;
        for(const auto& source:source_rows_)if(source.dataset==resource_target_&&source.selected){browser_path_=std::filesystem::u8path(source.path).parent_path();break;}
        browse(browser_path_);break;
    case MenuScreen::files:
        if(focus_==0)browse(browser_path_.parent_path());
        else if(focus_==1){path_buffer_=browser_path_.u8string();path_selected_=true;enter(MenuScreen::path);SDL_StartTextInput();}
        else if(focus_<=int(file_rows_.size())+1){const auto file=file_rows_[focus_-2];if(file.directory)browse(file.path);else{pending_resource_=file.path.u8string();enter_item(MenuScreen::resources,resource_item(resource_target_));commands_.push_back(MenuCommand::add_resource);}}
        else back();break;
    case MenuScreen::path:
        if(focus_==0){auto value=path_buffer_;const auto first=value.find_first_not_of(" \t\r\n");
            value=first==std::string::npos?std::string{}:value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
            if(value.size()>1&&value.front()=='"'&&value.back()=='"')value=value.substr(1,value.size()-2);
            const auto path=std::filesystem::u8path(value);std::error_code error;
            if(std::filesystem::is_regular_file(path,error)){pending_resource_=path.u8string();enter_item(MenuScreen::resources,resource_item(resource_target_));commands_.push_back(MenuCommand::add_resource);}
            else browse(path);
        }else back();break;
    case MenuScreen::help:if(page_count_>1&&focus_==0)page_=(page_+d+page_count_)%page_count_;else back();break;
    case MenuScreen::map:case MenuScreen::notice:back();break;
    default:break;
    }
}
void Frontend::capture(const SDL_Event& e){
    if(e.type==SDL_CONTROLLERDEVICEREMOVED){back();return;}
    if(e.type==SDL_KEYDOWN&&e.key.keysym.scancode==SDL_SCANCODE_ESCAPE){back();return;}
    if(binding_parent_==MenuScreen::pad){
        if(e.type==SDL_CONTROLLERBUTTONDOWN&&e.cbutton.button==SDL_CONTROLLER_BUTTON_START){capture_start_=true;return;}
        if(e.type==SDL_CONTROLLERBUTTONDOWN&&e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK&&capture_start_){back();return;}
        if(e.type==SDL_CONTROLLERBUTTONUP&&e.cbutton.button==SDL_CONTROLLER_BUTTON_START&&capture_start_){capture_kind_=2;capture_value_=SDL_CONTROLLER_BUTTON_START;capture_start_=false;}
        else if(e.type==SDL_CONTROLLERBUTTONDOWN){capture_kind_=2;capture_value_=e.cbutton.button;}
        else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.value>20000&&(e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)){capture_kind_=3;capture_value_=e.caxis.axis;}
        else return;
    }else if(slot_==2){
        if(e.type==SDL_MOUSEWHEEL){capture_value_=wheel_binding(e.wheel);if(!capture_value_)return;}
        else if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button<=5)capture_value_=e.button.button;
        else return;capture_kind_=1;
    }
    else{if(e.type!=SDL_KEYDOWN||e.key.repeat||e.key.keysym.scancode==SDL_SCANCODE_UNKNOWN)return;capture_kind_=0;capture_value_=e.key.keysym.scancode;}
    conflicts_.clear();
    for(int i=0;i<actions;++i){
        if(capture_kind_==0){for(int n=0;n<2;++n)if((i!=action_||n!=slot_)&&s_.input.keys[i][n]==capture_value_)conflicts_.push_back({i,n});}
        else if(i!=action_&&((capture_kind_==1&&s_.input.mouse[i]==unsigned(capture_value_))||(capture_kind_==2&&s_.input.buttons[i]==capture_value_)||(capture_kind_==3&&s_.input.triggers[i]==capture_value_)))conflicts_.push_back({i,0});
    }
    if(conflicts_.empty())accept_binding();else enter(MenuScreen::conflict,1);
}
void Frontend::accept_binding(){
    auto& b=s_.input;
    for(const auto& c:conflicts_){if(capture_kind_==0)b.keys[c.first][c.second]=SDL_SCANCODE_UNKNOWN;else if(capture_kind_==1)b.mouse[c.first]=0;else if(capture_kind_==2)b.buttons[c.first]=SDL_CONTROLLER_BUTTON_INVALID;else b.triggers[c.first]=SDL_CONTROLLER_AXIS_INVALID;}
    if(capture_kind_==0)b.keys[action_][slot_]=SDL_Scancode(capture_value_);
    else if(capture_kind_==1)b.mouse[action_]=capture_value_;
    else{b.buttons[action_]=capture_kind_==2?SDL_GameControllerButton(capture_value_):SDL_CONTROLLER_BUTTON_INVALID;b.triggers[action_]=capture_kind_==3?SDL_GameControllerAxis(capture_value_):SDL_CONTROLLER_AXIS_INVALID;}
    if(binding_parent_==MenuScreen::pad)b.pad_preset=InputPreset::custom;else b.keyboard_preset=InputPreset::custom;
    enter(binding_parent_==MenuScreen::pad?MenuScreen::bindings:MenuScreen::key_slots,binding_parent_==MenuScreen::pad?action_:slot_);
}
void Frontend::handle(const SDL_Event& e,int ww,int wh){
    if(!active())return;
    if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){focused_=false;last_axis_=0;capture_start_=false;if(capturing())back();return;}
    if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){focused_=true;last_axis_=0;return;}
    if(e.type==SDL_CONTROLLERDEVICEREMOVED){last_axis_=0;capture_start_=false;if(capturing())back();return;}
    if(!focused_)return;
    if(screen_==MenuScreen::title||screen_==MenuScreen::readme){
        const bool proceed=(e.type==SDL_KEYDOWN&&!e.key.repeat&&(e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_SPACE))||
            (e.type==SDL_CONTROLLERBUTTONDOWN&&(e.cbutton.button==SDL_CONTROLLER_BUTTON_A||e.cbutton.button==SDL_CONTROLLER_BUTTON_START))||
            (e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT);
        const bool cancel=(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)||
            (e.type==SDL_CONTROLLERBUTTONDOWN&&(e.cbutton.button==SDL_CONTROLLER_BUTTON_B||e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK));
        if(cancel)back();else if(proceed){focus_=0;choose();}
        return;
    }
    if(screen_==MenuScreen::path||screen_==MenuScreen::save_name){
        const std::size_t limit=screen_==MenuScreen::save_name?240:4095;
        if(e.type==SDL_TEXTINPUT){if(path_selected_)path_buffer_.clear();path_selected_=false;if(path_buffer_.size()+std::char_traits<char>::length(e.text.text)<=limit)path_buffer_+=e.text.text;return;}
        if(e.type==SDL_KEYDOWN&&(e.key.keysym.mod&KMOD_CTRL)&&e.key.keysym.sym==SDLK_a){path_selected_=true;return;}
        if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_BACKSPACE){if(path_selected_)path_buffer_.clear();path_selected_=false;if(!path_buffer_.empty()){do{const auto c=static_cast<unsigned char>(path_buffer_.back());path_buffer_.pop_back();if((c&0xc0)!=0x80)break;}while(!path_buffer_.empty());}return;}
        if(e.type==SDL_KEYDOWN&&(e.key.keysym.mod&KMOD_CTRL)&&e.key.keysym.sym==SDLK_v){char* value=SDL_GetClipboardText();if(value){std::string pasted=value;if(pasted.size()>limit){auto cut=limit;while(cut&&((static_cast<unsigned char>(pasted[cut])&0xc0)==0x80))--cut;pasted.resize(cut);}pasted.erase(std::remove_if(pasted.begin(),pasted.end(),[](unsigned char c){return c<32||c==127;}),pasted.end());path_buffer_=pasted;path_selected_=false;SDL_free(value);}return;}
    }
    if(screen_==MenuScreen::map){
        const auto map=static_cast<unsigned>(Action::map);bool toggle=false;
        if(e.type==SDL_KEYUP&&e.key.keysym.scancode!=SDL_SCANCODE_UNKNOWN)
            for(auto key:s_.input.keys[map])toggle|=key==e.key.keysym.scancode;
        if(e.type==SDL_MOUSEBUTTONUP&&s_.input.mouse_enabled&&s_.input.mouse[map])toggle|=s_.input.mouse[map]==e.button.button;
        if(e.type==SDL_CONTROLLERBUTTONUP&&s_.input.gamepad_enabled&&s_.input.buttons[map]>=0)toggle|=s_.input.buttons[map]==e.cbutton.button;
        if(e.type==SDL_CONTROLLERAXISMOTION&&s_.input.gamepad_enabled&&s_.input.triggers[map]>=0&&s_.input.triggers[map]==e.caxis.axis){
            if(e.caxis.value>16384)map_trigger_released_=false;
            else if(e.caxis.value<10000&&!map_trigger_released_){toggle=true;map_trigger_released_=true;}
        }
        if(toggle){back();return;}
        if((e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)||
           (e.type==SDL_CONTROLLERBUTTONDOWN&&e.cbutton.button==SDL_CONTROLLER_BUTTON_B))back();
        return; // The map has no menu rows: Space/Enter/A remain gameplay input.
    }
    if(e.type==SDL_KEYDOWN||e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEMOTION)pad_=false;
    if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERAXISMOTION)pad_=true;
    if(screen_==MenuScreen::capture){capture(e);return;}
    if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button!=SDL_BUTTON_LEFT&&e.button.button!=SDL_BUTTON_RIGHT)return;
    build_rows();if(rows_.empty())return;
    int nav=0,adjust=0;bool accept=false,cancel=false;
    if(e.type==SDL_KEYDOWN){
        switch(e.key.keysym.sym){case SDLK_UP:nav=-1;break;case SDLK_DOWN:nav=1;break;case SDLK_LEFT:adjust=-1;break;case SDLK_RIGHT:adjust=1;break;
        case SDLK_RETURN:case SDLK_SPACE:accept=!e.key.repeat;break;case SDLK_ESCAPE:cancel=!e.key.repeat;break;
        case SDLK_PAGEUP:page_=std::max(0,page_-1);break;case SDLK_PAGEDOWN:++page_;break;default:break;}
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN){switch(e.cbutton.button){case SDL_CONTROLLER_BUTTON_DPAD_UP:nav=-1;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:nav=1;break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:adjust=-1;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:adjust=1;break;case SDL_CONTROLLER_BUTTON_A:accept=true;break;
        case SDL_CONTROLLER_BUTTON_B:cancel=true;break;case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:page_=std::max(0,page_-1);break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:++page_;break;default:break;}}
    if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY){int axis=e.caxis.value<-18000?-1:e.caxis.value>18000?1:0;if(axis&&axis!=last_axis_)nav=axis;last_axis_=axis;}
    if(e.type==SDL_MOUSEWHEEL)nav=e.wheel.y>0?-1:e.wheel.y<0?1:0;
    if(e.type==SDL_MOUSEMOTION||e.type==SDL_MOUSEBUTTONDOWN){
        const int x=(e.type==SDL_MOUSEMOTION?e.motion.x:e.button.x)*frame_.width/std::max(1,ww);
        const int y=(e.type==SDL_MOUSEMOTION?e.motion.y:e.button.y)*frame_.height/std::max(1,wh);
        for(const auto& hit:hits_)if(inside(hit.rect,x,y)){
            if(hit.row>=100){if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT){level_=hit.row-100;page_=0;}return;}
            if(!rows_[hit.row].enabled)return;
            focus_=hit.row;if(e.type==SDL_MOUSEBUTTONDOWN){
                accept=e.button.button==SDL_BUTTON_LEFT;
                if(e.button.button==SDL_BUTTON_RIGHT){
                    const bool adjustable=rows_[focus_].adjustable||(screen_==MenuScreen::gameplay&&focus_==0);
                    if(adjustable)adjust=-1;
                }
            }break;
        }
    }
    if(cancel){back();return;}
    if(nav){for(int i=0;i<int(rows_.size());++i){focus_=(focus_+nav+int(rows_.size()))%int(rows_.size());if(rows_[focus_].enabled)break;}}
    if(adjust){
        if(screen_==MenuScreen::levels&&archive()->identify()!=DatasetId::old_gold){level_=(level_-1+adjust+3)%3+1;page_=0;}
        else if(screen_==MenuScreen::help)page_=(page_+adjust+page_count_)%page_count_;
        else if(screen_==MenuScreen::credits)credit_page_=(credit_page_+adjust+page_count_)%page_count_;
        else if(screen_==MenuScreen::graphics||screen_==MenuScreen::sound||screen_==MenuScreen::keyboard||screen_==MenuScreen::pad||screen_==MenuScreen::gameplay||screen_==MenuScreen::interface){
            if(rows_[focus_].adjustable||(screen_==MenuScreen::gameplay&&focus_==0))choose(adjust);
        }
    }
    if(accept)choose();
}
bool Frontend::save_allowed()const{return s_.saves_enabled&&world_&&world_->player&&!world_->level_complete&&world_->objects.at(*world_->player).player_state->health()>0;}
void Frontend::refresh_saves(){saves_=save_catalog?save_catalog():std::vector<SaveEntry>{};if(saves_.size()!=11){saves_.resize(11);for(int i=0;i<11;++i)saves_[i].slot=i;}}
bool Frontend::has_loadable_saves(){
    if(!s_.saves_enabled)return false;
    refresh_saves();return std::any_of(saves_.begin(),saves_.end(),[](const SaveEntry& entry){return entry.exists&&entry.valid;});
}
void Frontend::leave_game_menu(){
    commands_.push_back(MenuCommand::music_stop);
    enter(sets_.size()>1?MenuScreen::datasets:MenuScreen::main,sets_.size()>1?int(dataset_):0);
}
void Frontend::open_saves(bool saving){if(!s_.saves_enabled)return;save_return_=screen_;refresh_saves();enter(saving?MenuScreen::save_slots:MenuScreen::load_slots);}
void Frontend::confirm_save_action(int op){save_operation_=op;enter(MenuScreen::save_confirm,1);}
void Frontend::quickload_request(){if(!s_.saves_enabled)return;commands_.push_back(MenuCommand::quickload);}
const MenuFrame& Frontend::render_saves(){
    const int w=frame_.width,h=frame_.height,t=text_height_;
    const bool compact=h<300,list=screen_==MenuScreen::save_slots||screen_==MenuScreen::load_slots;
    const bool saving=screen_==MenuScreen::save_slots,details=list||screen_==MenuScreen::save_entry;
    const int selected=list?focus_+(saving?1:0):save_slot_;
    const SaveEntry* entry=selected>=0&&selected<int(saves_.size())?&saves_[selected]:nullptr;
    if(!background_.pixels.empty())for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto c=colors_[background_.pixels[(y%background_.height)*background_.width+x%background_.width]];frame_.pixels[y*w+x]=0xff000000|((c&0x00fefefe)>>1);}
    // Match the ordinary menu width, header, padding and row rhythm.
    const int panel_w=std::min(w-24,420),margin=(w-panel_w)/2,pad=compact?6:12;
    const int header_h=t+(compact?12:20),gap=compact?6:12,footer_h=t+16;
    const int body_h=std::min(h-header_h-gap-footer_h-12,details?(compact?132:244):(compact?116:180));
    const int group_h=header_h+gap+body_h+footer_h,header_y=std::max(4,(h-group_h)/2),body_y=header_y+header_h+gap;
    box({margin,header_y,panel_w,header_h});
    centered(saving||screen_==MenuScreen::save_name||(screen_==MenuScreen::save_confirm&&save_operation_==0)?tr("Сохранить игру","Save game"):tr("Загрузить игру","Load game"),header_y+(header_h-t)/2,ink_);
    box({margin,body_y,panel_w,body_h});
    int top=body_y+pad,bottom=body_y+body_h-pad,left=margin+pad,width=panel_w-pad*2;
    auto preview=[&](SDL_Rect area,bool side){
        const int iw=side?128:96,ih=iw*5/8;fill({area.x,area.y,iw,ih},0xff101010);
        if(entry&&entry->valid&&!entry->metadata.preview.empty()){const auto& m=entry->metadata;for(int y=0;y<ih;++y)for(int x=0;x<iw;++x)fill({area.x+x,area.y+y,1,1},m.preview[std::size_t(y)*m.preview_height/ih*m.preview_width+std::size_t(x)*m.preview_width/iw]);}
        else paragraph(tr("Нет превью","No preview"),{area.x+4,area.y+4,iw-8,ih-8});
        std::string info;
        if(entry&&entry->exists&&entry->valid){const auto& m=entry->metadata;std::time_t stamp=static_cast<std::time_t>(m.timestamp);const auto* time=std::localtime(&stamp);char date[40]{};if(time)std::strftime(date,sizeof(date),"%Y-%m-%d %H:%M",time);
            const auto seconds=m.ticks*119/GameClock::pit_hz;
            info=game_level_name(m.dataset,m.level.back()-'0',s_.locale)+"\n"+date+"\n"+tr("Время: ","Time: ")+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60)+"\n"+tr("Здоровье: ","Health: ")+std::to_string(m.health)+"\n"+tr("Попытки: ","Lives: ")+std::to_string(m.lives);
            if(entry->backup)info+="\n"+tr("Резервная копия","Backup copy");
        }else info=entry&&entry->exists?tr("Повреждено или несовместимо.","Damaged or incompatible."):tr("Пустой слот","Empty slot");
        paragraph(info,{side?area.x:area.x+iw+8,side?area.y+ih+8:area.y,side?area.w:area.w-iw-8,side?area.h-ih-8:area.h});
    };
    if(details){if(!compact){preview({margin+panel_w-pad-128,top,128,bottom-top},true);width-=128+16;}
        else {preview({left,top,width,64},false);top+=70;}}
    if(screen_==MenuScreen::save_name){paragraph(tr("Название сохранения","Save name"),{left,top,width,(t+4)*2});top+=(t+4)*2+4;
        fill({left,top,width,t+12},path_selected_?0xff3d3322:0xff242424);auto name=cp866(path_buffer_);const auto count=std::size_t(std::max(1,(width-16)/8));if(name.size()>count)name=name.substr(name.size()-count);text(name+"_",left+4,top+6,accent_,true);top+=t+20;}
    if(screen_==MenuScreen::save_confirm){const auto question=save_operation_==0?tr("Перезаписать сохранение?","Overwrite this save?"):save_operation_==2?tr("Удалить сохранение и резервную копию?","Delete this save and its backup?"):entry&&entry->backup?tr("Загрузить резервную копию? Текущий прогресс будет потерян.","Load the backup copy? Current progress will be lost."):tr("Загрузить сохранение? Текущий прогресс будет потерян.","Load this save? Current progress will be lost.");paragraph(question,{left,top,width,(t+4)*4});top+=(t+4)*4+8;}
    const int rh=t+(compact?10:14),visible=std::max(1,(bottom-top)/rh),first=std::max(0,focus_-visible+1);
    for(int i=first;i<int(rows_.size())&&i<first+visible;++i){SDL_Rect r{left-4,top+(i-first)*rh,width+4,rh-4};if(i==focus_){fill(r,0xff302b20);fill({r.x,r.y,2,r.h},accent_);}
        const auto color=!rows_[i].enabled?0xff99958b:i==focus_?accent_:ink_;auto label=cp866(rows_[i].label);const auto count=std::size_t(std::max(1,(width-12)/8));if(label.size()>count){label.resize(count>3?count-3:count);label+="...";}text(label,r.x+8,r.y+4,color,true);hits_.push_back({r,i});}
    if(list&&int(rows_.size())>visible){const int track=std::max(1,bottom-top),thumb=std::max(6,track*visible/int(rows_.size()));fill({left+width-3,top,2,track},0xff444038);fill({left+width-3,top+(track-thumb)*first/std::max(1,int(rows_.size())-visible),2,thumb},accent_);}
    centered(compact?tr("Enter: OK  Esc: назад","Enter: OK  Esc: back"):pad_?tr(MenuString::d_pad_select_a_ok_b_back):tr(MenuString::arrows_select_enter_ok_esc_back),body_y+body_h+8,ink_);return frame_;
}
void Frontend::theme(){
    if(themed_==archive()&&!font_.empty())return;themed_=archive();art_.clear();font_=bootstrap_font();menu_font_=font_;menu_yo16(menu_font_);background_={};
    for(int c=0;c<256;++c)for(int y=0;y<8;++y)font8_[c*8+y]=menu_font_[c*16+y*2+1];
    if(!themed_)return;
    colors_=themed_->palette(cp866("МЕНЮ_ПАЛ"));background_=themed_->image("FON");
    const auto f=themed_->font("FONT16");std::copy(f.data,f.data+std::min(f.size,font_.size()),font_.begin());
    menu_font_=font_;menu_yo16(menu_font_);
    const auto small=themed_->font("FONT8");std::copy(small.data,small.data+std::min(small.size,font8_.size()),font8_.begin());
    // Select legible source-palette entries, preserving each game's colour family.
    auto light=[](std::uint32_t c){return int((c>>16)&255)*3+int((c>>8)&255)*6+int(c&255);};
    ink_=*std::max_element(colors_.begin(),colors_.end(),[&](auto a,auto b){return light(a)<light(b);});
    int best=-10000;for(auto c:colors_){int r=(c>>16)&255,g=(c>>8)&255,b=c&255;int score=r+g-b*2-std::abs(r-g);if(score>best){best=score;accent_=c;}}
    shade_=colors_[0]|0xff000000;
}
void Frontend::fill(SDL_Rect r,std::uint32_t color){for(int y=std::max(0,r.y);y<std::min(frame_.height,r.y+r.h);++y)for(int x=std::max(0,r.x);x<std::min(frame_.width,r.x+r.w);++x)frame_.pixels[y*frame_.width+x]=color;}
void Frontend::box(SDL_Rect r){fill(r,shade_);fill({r.x,r.y,r.w,1},ink_);fill({r.x,r.y,1,r.h},ink_);fill({r.x+r.w-1,r.y,1,r.h},0xff444444);fill({r.x,r.y+r.h-1,r.w,1},0xff444444);}
void Frontend::text(const std::string& value,int x,int y,std::uint32_t color,bool encoded){
    const auto bytes=encoded?value:cp866(value);
    for(unsigned char c:bytes){for(int row=0;row<text_height_;++row){const int src=row*16/text_height_;const auto bits=text_height_==8?font8_[c*8+row]:(native_text_?font_:menu_font_)[c*16+src];
        for(int bit=0;bit<8;++bit)if(bits&(0x80>>bit))fill({x+bit,y+row,1,1},color);}x+=8;}
}
void Frontend::centered(const std::string& value,int y,std::uint32_t color){text(value,std::max(8,(frame_.width-int(cp866(value).size())*8)/2),y,color);}
void Frontend::paragraph(const std::string& value,SDL_Rect r,bool encoded){
    const auto bytes=encoded?value:cp866(value);const auto pages=paginate_story({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},std::max(1,r.w/8),std::max(1,r.h/(text_height_+3)));
    if(pages.empty())return;int y=r.y;for(const auto& line:pages[0]){text(line,r.x,y,ink_,true);y+=text_height_+3;}
}
void Frontend::picture(const std::string& name,const std::string& palette,SDL_Rect r){
    if(!archive())return;const auto key=name+palette;auto found=art_.find(key);
    if(found==art_.end())found=art_.emplace(key,std::make_pair(archive()->image(cp866(name)),archive()->palette(cp866(palette)))).first;
    const auto& image=found->second.first;const auto& colors=found->second.second;
    const double aspect=double(image.width)/(image.height*1.2);int w=r.w,h=int(w/aspect);if(h>r.h){h=r.h;w=int(h*aspect);}
    int ox=r.x+(r.w-w)/2,oy=r.y+(r.h-h)/2;fill(r,shade_);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)fill({ox+x,oy+y,1,1},colors[image.pixels[std::size_t(y)*image.height/h*image.width+std::size_t(x)*image.width/w]]);
}
std::vector<TextPage> Frontend::story(const std::string& name,int columns,int rows)const{
    if(!archive()||!archive()->find(name))return {{}};
    const auto translated=game_text(game_story_key(archive()->identify(),name),s_.locale);
    const auto encoded=cp866(translated);
    const auto bytes=translated.empty()?archive()->get(name):Bytes{reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()};
    auto result=paginate_story(bytes,columns,rows);if(result.empty())result.push_back({});return result;
}
const MenuFrame& Frontend::render(int width,int height){
    output_width_=width;output_height_=height;
    native_text_=false;
    if(screen_==MenuScreen::map&&world_&&archive()){
        auto indexed=native_automap(*world_);const auto palette=archive()->palette(world_->definition.palette);
        const bool og=world_->definition.dataset==DatasetId::old_gold;
        // The DOS OG marker is one white pixel on a white map. Preserve the
        // native map oracle; give that same point contrast in the full-screen UI.
        if(og&&world_->player){
            const auto& d=world_->definition;const auto p=*world_->objects.at(*world_->player).pose;
            const int x=(d.width-p.x/d.cell_side)*2-1,y=p.z/d.cell_side*2+1;
            if(x>=0&&x<320&&y>=0&&y<200)indexed.pixels[y*320+x]=0;
        }
        // Keep native map pixels and discovery, but present OG's small DOS
        // overlay as a centered full-screen map, with square map cells.
        frame_.width=std::max(320,width);frame_.height=std::max(200,height);
        const int sw=og?world_->definition.width*2+1:320,sh=og?world_->definition.length*2+1:200;
        frame_.pixels.assign(std::size_t(frame_.width)*frame_.height,palette[indexed.pixels[0]]);hits_.clear();
        const double aspect=og?1.0:1.2;
        const double zoom=std::min(double(frame_.width)/sw,double(frame_.height)/(sh*aspect));
        const int dw=std::max(1,int(sw*zoom)),dh=std::max(1,int(sh*zoom*aspect));
        const int ox=(frame_.width-dw)/2,oy=(frame_.height-dh)/2;
        for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)
            frame_.pixels[(oy+y)*frame_.width+ox+x]=palette[indexed.pixels[(y*sh/dh)*320+x*sw/dw]];
        return frame_;
    }
    // Story text is 25% larger than menus: 20/30/40 px at 720/1080/1440p.
    // Ordinary menus keep their established 16/24/32 px scale.
    // Keep the full reading area instead of magnifying a 360-line canvas.
    // Fractional scaling avoids a sudden font-size jump during window resizing.
    const bool story_text=screen_==MenuScreen::readme||screen_==MenuScreen::levels||screen_==MenuScreen::ending;
    const double scale=std::max(1.0,std::min(width/640.0,height/(story_text?576.0:720.0)));
    frame_.width=std::max(320,int(width/scale));frame_.height=std::max(200,int(height/scale));
    text_height_=frame_.height<300?8:16;line_height_=text_height_+(frame_.height<300?8:12);
    frame_.pixels.assign(std::size_t(frame_.width)*frame_.height,shade_);theme();hits_.clear();build_rows();
    const int w=frame_.width,h=frame_.height;
    if(screen_==MenuScreen::save_slots||screen_==MenuScreen::load_slots||screen_==MenuScreen::save_entry||screen_==MenuScreen::save_name||screen_==MenuScreen::save_confirm)return render_saves();
    if(screen_==MenuScreen::title){
        fill({0,0,w,h},0xff000000);
        picture(title_sprite,title_palette,{0,0,w,h});
        return frame_;
    }
    if(!background_.pixels.empty())for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const auto c=colors_[background_.pixels[(y%background_.height)*background_.width+x%background_.width]];
        frame_.pixels[y*w+x]=0xff000000|((c&0x00fefefe)>>1);
    }
    if(screen_==MenuScreen::readme){
        // Original README draws text over the menu's FON tile using GAMEP1.
        // The title illustration belongs only to the preceding splash screen.
        const auto palette=archive()->palette("GAMEP1");
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const auto c=palette[background_.pixels[(y%background_.height)*background_.width+x%background_.width]];
            const auto red=((c>>16)&255)*3/5,green=((c>>8)&255)*3/5,blue=(c&255)*3/5;
            frame_.pixels[y*w+x]=0xff000000|(red<<16)|(green<<8)|blue;
        }
        const int content_w=std::min(w-32,720),x=(w-content_w)/2;
        const int rows=std::max(1,(h-64)/(text_height_+4));
        auto pages=story("README",std::max(1,content_w/8),rows);
        page_count_=int(pages.size());page_=(page_%page_count_+page_count_)%page_count_;
        int y=std::max(12,(h-int(pages[page_].size())*(text_height_+4))/2);
        native_text_=true;
        for(const auto& line:pages[page_]){text(line,x,y,ink_,true);y+=text_height_+4;}
        native_text_=false;
        if(page_count_>1)text(std::to_string(page_+1)+" / "+std::to_string(page_count_),x,h-text_height_*2-16,accent_);
        centered(pad_?tr("A: далее   B: назад","A: next   B: back"):tr("Enter: далее   Esc: назад","Enter: next   Esc: back"),h-text_height_-8,ink_);
        hits_.push_back({{0,0,w,h},0});
        return frame_;
    }
    const bool story_screen=screen_==MenuScreen::levels||screen_==MenuScreen::ending;
    const bool illustrated=story_screen;
    const bool compact=h<300;
    const bool reading=story_screen||screen_==MenuScreen::help||screen_==MenuScreen::credits||screen_==MenuScreen::map;
    std::string reading_content;
    if(screen_==MenuScreen::credits){reading_content=tr(MenuString::dungeons_of_kremlin_n_n3_d_technology_nalexander_razbak)+"\n\n"+tr(MenuString::old_gold_n_nauthors_nandrey_razbakov_nalexander_razbako)+"\n\n"+tr(MenuString::sdl2_port_n_n_n_nold_gold_pkr_pke_n_noriginal_materials);}
    else if(screen_==MenuScreen::help){reading_content=tr(
            "УПРАВЛЕНИЕ ПО УМОЛЧАНИЮ\n\nWASD: движение. Мышь: поворот. ЛКМ: огонь. Пробел или Enter: открыть дверь. 1, 2, колесо вверх/вниз и PgUp/PgDn: сменить оружие на другое, если оно получено. Tab: карта. Esc: пауза.\n\nНазначения и чувствительность можно изменить в настройках управления. Меню работает с клавиатурой, мышью и геймпадом.\n\nСТАРОЕ ЗОЛОТО\n\nВы начинаете без палочки с магией 0/0. Сначала найдите палочку: после этого можно стрелять и подбирать пополнения магии. Соберите 105 монет и найдите выход.",
            "DEFAULT CONTROLS\n\nWASD: move. Mouse: turn. Left mouse: fire. Space or Enter: open a door. 1, 2, wheel up/down and PgUp/PgDn: switch to the other weapon, if owned. Tab: map. Esc: pause.\n\nChange bindings and sensitivity in Controls. Menus support keyboard, mouse and gamepad.\n\nOLD GOLD\n\nYou start without a wand, with magic at 0/0. Find a wand first: you can then fire and pick up magic refills. Collect 105 coins and find the exit.");}
    const int panel_width=std::min(w-24,illustrated?960:reading?720:screen_==MenuScreen::resources?640:420);
    const bool resource_stacked=panel_width<600;
    const int margin=(w-panel_width)/2,pad=compact?6:12,header_y=compact?4:14;
    const int header_height=text_height_+(illustrated||compact?12:20);
    // Hover/focus must not resize and recenter the screen. Reserve the same
    // help area for every row, including rows without their own description.
    const bool footer_help=std::any_of(rows_.begin(),rows_.end(),[](const Row& row){return !row.help.empty();})||(screen_==MenuScreen::resources&&!errors_.empty());
    const bool show_help=footer_help&&!reading&&!illustrated&&(!compact||screen_==MenuScreen::datasets);
    int help_lines=0;
    if(show_help)for(const auto& row:rows_){
        const auto bytes=cp866(row.help);
        const auto pages=paginate_story({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},std::max(1,(panel_width-pad*2)/8),100);
        if(!pages.empty())help_lines=std::max(help_lines,int(pages.front().size()));
    }
    const int footer_height=show_help?std::max(2,help_lines)*(text_height_+3)+text_height_+12:text_height_+12;
    int footer_top=h-footer_height-8;
    int top=header_y+header_height+(compact||illustrated?6:12),bottom=footer_top-(compact?4:10);
    std::string dialog_message;
    const bool dialog=screen_==MenuScreen::confirm||screen_==MenuScreen::conflict||screen_==MenuScreen::video_confirm||screen_==MenuScreen::video_unsaved||screen_==MenuScreen::notice||screen_==MenuScreen::outcome;
    if(screen_==MenuScreen::confirm)dialog_message=fallback_?tr(MenuString::opengl_could_not_be_enabled_nuse_the_software_renderer):tr(MenuString::current_progress_will_be_lost_ncontinue);
    else if(screen_==MenuScreen::conflict){dialog_message=tr(MenuString::control_already_assigned_n);for(const auto& c:conflicts_)dialog_message+=action_name(c.first)+"\n";}
    else if(screen_==MenuScreen::video_confirm)dialog_message=tr(MenuString::keep_the_new_display_settings_nreverting_in)+std::to_string((video_deadline_>now_?video_deadline_-now_:0)/1000+1)+tr(MenuString::s);
    else if(screen_==MenuScreen::video_unsaved)dialog_message=tr(MenuString::unapplied_video_changes);
    else if(screen_==MenuScreen::notice)dialog_message=s_.locale==LocaleId::ru?notice_ru_:notice_en_;
    else if(screen_==MenuScreen::outcome)dialog_message=action_==int(SessionPhase::retry)?tr(MenuString::life_lost):tr(MenuString::game_over);
    // Measure short screens before drawing their panels. Large displays give
    // text breathing room, rather than stretching every menu to screen height.
    int desired=pad*2+8;
    const int measure_w=panel_width-pad*2;
    if(story_screen){
        const bool og=archive()->identify()==DatasetId::old_gold,end=screen_==MenuScreen::ending;
        const auto resource=og?(end?"INTRO4E":"INTRO4B"):(end?"EINTRO":"BINTRO")+std::to_string(level_);
        const int text_w=measure_w-measure_w*2/5-pad;
        const int tabs=!og&&screen_==MenuScreen::levels?text_height_+18:0;
        const int available=std::max(1,bottom-top-pad*2-tabs-(text_height_+10));
        const auto pages=story(resource,std::max(1,text_w/8),std::max(1,(available-text_height_-8)/(text_height_+4)));
        int lines=0;for(const auto& p:pages)lines=std::max(lines,int(p.size()));
        const int text_h=lines*(text_height_+4)+text_height_+8;
        desired=pad*2+tabs+std::min(available,std::max(measure_w*2/5*3/4,text_h))+(text_height_+10)+8;
    }else if(!reading_content.empty()){
        const auto bytes=cp866(reading_content);
        const int actions_h=2*(text_height_+4+(compact?6:10))+8;
        const int available=std::max(1,bottom-top-actions_h-pad*2);
        const auto pages=paginate_story({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},std::max(1,measure_w/8),std::max(1,(available-text_height_-8)/(text_height_+4)));
        int lines=0;for(const auto& p:pages)lines=std::max(lines,int(p.size()));
        desired=pad*2+actions_h+lines*(text_height_+4)+text_height_+8;
    }else if(!reading&&screen_!=MenuScreen::capture&&screen_!=MenuScreen::path&&screen_!=MenuScreen::files){
        for(int i=0;i<int(rows_.size());++i){const auto& row=rows_[i];const int chars=std::max(1,(measure_w-28)/8);
            if(screen_==MenuScreen::resources&&row.id!=Item::back){desired+=(resource_stacked?2:1)*(text_height_+4)+10;continue;}
            int lines=std::max(1,(int(cp866(row.label).size())+chars-1)/chars);
            if(!row.value.empty()&&(screen_==MenuScreen::datasets||int(cp866(row.label).size()+cp866(row.value).size())*8>measure_w-44))++lines;
            desired+=lines*(text_height_+4)+(compact?6:10)+(screen_==MenuScreen::graphics&&rows_[i].id==Item::filtering?(compact?6:12):0);
        }
        if(dialog){const auto bytes=cp866(dialog_message);const auto pages=paginate_story({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},std::max(1,measure_w/8),100);
            desired=int(rows_.size())*(text_height_+4+(compact?6:10))+8+(pages.empty()?1:int(pages[0].size()))*(text_height_+4)+pad*2+8;}
    }else desired=bottom-top;
    const int spare=std::max(0,bottom-top-desired);
    const int panel_shift=spare/2;
    top+=panel_shift;bottom-=spare-panel_shift;footer_top-=spare-panel_shift;
    std::string heading=tr(MenuString::dungeons_of_kremlin_old_gold);
    switch(screen_){
    case MenuScreen::settings:heading=tr(MenuString::settings_2);break;case MenuScreen::graphics:heading=tr(MenuString::graphics_2);break;
    case MenuScreen::sound:heading=tr(MenuString::sound_2);break;case MenuScreen::controls:heading=tr(MenuString::controls_2);break;
    case MenuScreen::keyboard:heading=tr(MenuString::keyboard_and_mouse_2);break;case MenuScreen::pad:heading=tr(MenuString::gamepad_2);break;
    case MenuScreen::gameplay:heading=tr(MenuString::gameplay_2);break;case MenuScreen::interface:heading=tr(MenuString::interface_2);break;
    case MenuScreen::datasets:heading=tr(MenuString::select_game);break;case MenuScreen::credits:heading=tr(MenuString::credits_2);break;
    case MenuScreen::pause:heading=tr(MenuString::paused);break;case MenuScreen::bindings:heading=tr(MenuString::remap_controls_2);break;
    case MenuScreen::key_slots:heading=action_name(action_);break;case MenuScreen::levels:heading=archive()&&archive()->identify()==DatasetId::old_gold?game_dataset_title(DatasetId::old_gold,s_.locale):tr(MenuString::select_level);break;
    case MenuScreen::readme:heading=tr(MenuString::story);break;case MenuScreen::ending:heading=tr(MenuString::level_completed);break;
    case MenuScreen::capture:heading=action_name(action_);break;case MenuScreen::resources:heading=tr("Ресурсы игр","Game resources");break;
    case MenuScreen::help:heading=tr("Как играть","How to play");break;case MenuScreen::map:heading=tr("Карта","Map");break;case MenuScreen::files:heading=tr("Выбрать файл ресурсов","Choose resource file");break;
    case MenuScreen::path:heading=tr("Путь к папке или RES","Folder or RES path");break;
    default:break;
    }
    box({margin,header_y+panel_shift,panel_width,header_height});centered(heading,header_y+panel_shift+(illustrated||compact?6:10),ink_);
    box({margin,top,panel_width,std::max(1,bottom-top)});
    const int content_x=margin+pad,content_w=panel_width-pad*2;
    auto pages_at=[&](const std::string& content,SDL_Rect r,bool encoded=false){
        const auto bytes=encoded?content:cp866(content);auto pages=paginate_story({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},std::max(1,r.w/8),std::max(1,(r.h-text_height_-8)/(text_height_+4)));
        if(pages.empty())pages.push_back({});page_count_=int(pages.size());page_=(page_%page_count_+page_count_)%page_count_;
        int y=r.y;for(const auto& line:pages[page_]){text(line,r.x,y,ink_,true);y+=text_height_+4;}
        if(page_count_>1)text(std::to_string(page_+1)+" / "+std::to_string(page_count_),r.x,r.y+r.h-text_height_,accent_);
    };
    const int action_height=text_height_+10;
    const int stacked_actions_height=(screen_==MenuScreen::credits||screen_==MenuScreen::help?2:int(rows_.size()))*(text_height_+4+(compact?6:10))+8;
    if(story_screen){
        const bool og=archive()&&archive()->identify()==DatasetId::old_gold;
        int art_top=top+pad;
        if(screen_==MenuScreen::levels&&!og){
            const int cell=content_w/3;
            for(int i=0;i<3;++i){SDL_Rect r{content_x+i*cell,art_top,cell-4,text_height_+10};
                fill(r,i+1==level_?0xff3d3322:0xff181818);const auto label=std::to_string(i+1)+". "+game_level_name(archive()->identify(),i+1,s_.locale);
                if(int(cp866(label).size())*8<=r.w-8)text(label,r.x+4,r.y+5,level_==i+1?accent_:ink_);
                else text(std::to_string(i+1),r.x+r.w/2-4,r.y+5,level_==i+1?accent_:ink_);
                hits_.push_back({r,101+i});
            }art_top+=text_height_+18;
        }
        const int content_bottom=bottom-action_height-pad;
        SDL_Rect image_rect{content_x,art_top,content_w*2/5,content_bottom-art_top};
        SDL_Rect text_rect{image_rect.x+image_rect.w+pad,art_top,content_w-image_rect.w-pad,content_bottom-art_top};
        const bool end=screen_==MenuScreen::ending;
        const auto sprite=screen_==MenuScreen::readme?std::string(title_sprite):og?"INTRO4BSPR":(end?"ESPR":"BSPR")+std::to_string(level_);
        std::string palette=screen_==MenuScreen::readme?std::string(title_palette):og?"GAMEPAL":"GAMEP"+std::to_string(level_);
        if(!archive()->find(cp866(palette)))palette=og?"МЕНЮ_ПАЛ":palette;
        picture(sprite,palette,image_rect);
        const auto resource=screen_==MenuScreen::readme?"README":og?(end?"INTRO4E":"INTRO4B"):(end?"EINTRO":"BINTRO")+std::to_string(level_);
        auto pages=story(resource,std::max(1,text_rect.w/8),std::max(1,(text_rect.h-text_height_-8)/(text_height_+4)));
        page_count_=std::max(1,int(pages.size()));page_%=page_count_;int y=text_rect.y;
        native_text_=true;
        if(!pages.empty())for(const auto& line:pages[page_]){text(line,text_rect.x,y,ink_,true);y+=text_height_+4;}
        native_text_=false;
        if(page_count_>1)text(std::to_string(page_+1)+" / "+std::to_string(page_count_),text_rect.x,text_rect.y+text_rect.h-text_height_,accent_);
        build_rows();
    }else if(screen_==MenuScreen::credits){

        const int saved=page_;page_=credit_page_;pages_at(reading_content,{content_x,top+pad,content_w,bottom-top-stacked_actions_height-pad*2});credit_page_=page_;page_=saved;build_rows();top=bottom-int(rows_.size())*(text_height_+4+(compact?6:10))-8;
    }else if(screen_==MenuScreen::help){

        pages_at(reading_content,{content_x,top+pad,content_w,bottom-top-stacked_actions_height-pad*2});build_rows();top=bottom-int(rows_.size())*(text_height_+4+(compact?6:10))-8;
    }else if(screen_==MenuScreen::path){
        paragraph(tr("Введите путь или вставьте его сочетанием Ctrl+V.","Type a path, or paste it with Ctrl+V."),{content_x,top+pad,content_w,text_height_*3});
        const auto path=cp866(path_buffer_);const auto visible=path.size()>std::size_t(content_w/8-1)?path.substr(path.size()-(content_w/8-1)):path;
        fill({content_x,top+text_height_*3+pad,content_w,text_height_+12},path_selected_?0xff3d3322:0xff242424);text(visible+"_",content_x+4,top+text_height_*3+pad+6,accent_,true);
        top=bottom-stacked_actions_height;
    }else if(screen_==MenuScreen::capture){paragraph(tr(MenuString::press_a_new_control_nesc_cancel_ngamepad_hold_start_and),{content_x,top+pad,content_w,bottom-top-pad*2});}
    else if(screen_==MenuScreen::confirm||screen_==MenuScreen::conflict||screen_==MenuScreen::video_confirm||screen_==MenuScreen::video_unsaved||screen_==MenuScreen::notice||screen_==MenuScreen::outcome){
        paragraph(dialog_message,{content_x,top+pad,content_w,std::max(20,bottom-top-stacked_actions_height-pad*2)});top=bottom-stacked_actions_height;
    }
    if(illustrated){
        const int cell=content_w/std::max(1,int(rows_.size()));
        for(int i=0;i<int(rows_.size());++i){SDL_Rect r{content_x+i*cell,bottom-action_height,cell-4,action_height};
            if(i==focus_){fill(r,0xff302b20);fill({r.x,r.y,2,r.h},accent_);}
            const auto label=rows_[i].label;
            text(label,r.x+6,r.y+5,i==focus_?accent_:ink_);hits_.push_back({r,i});
        }
        const auto hint=compact?tr("Enter: OK   Esc: назад","Enter: OK   Esc: back"):pad_?tr(MenuString::d_pad_select_a_ok_b_back):tr(MenuString::arrows_select_enter_ok_esc_back);
        centered(screen_==MenuScreen::ending&&page_count_>1?(compact?tr("PgUp/PgDn: текст  Enter: меню","PgUp/PgDn: text  Enter: menu"):tr("PgUp/PgDn, L1/R1: текст   Enter/A: в меню","PgUp/PgDn, L1/R1: text   Enter/A: menu")):hint,footer_top+6,ink_);return frame_;
    }
    if(screen_==MenuScreen::files){paragraph(browser_path_.u8string(),{content_x,top+8,content_w,text_height_+4});top+=text_height_+20;}
    const bool editions=screen_==MenuScreen::datasets;
    auto row_height=[&](int i){const auto& row=rows_[i];if(screen_==MenuScreen::resources&&row.id!=Item::back)return (resource_stacked?2:1)*(text_height_+4)+10;const int chars=std::max(1,(content_w-28)/8);
        int lines=std::max(1,(int(cp866(row.label).size())+chars-1)/chars);
        if(!row.value.empty()&&(editions||int(cp866(row.label).size()+cp866(row.value).size())*8>content_w-44))++lines;
        return lines*(text_height_+4)+(compact?6:10)+(screen_==MenuScreen::graphics&&rows_[i].id==Item::filtering?(compact?6:12):0);
    };
    top+=4;bottom-=4;
    int first=0;
    for(int i=0,total=0;i<=focus_&&i<int(rows_.size());++i){total+=row_height(i);while(total>bottom-top&&first<i){total-=row_height(first++);}}
    int y=top,last=first;
    for(int i=first;i<int(rows_.size());++i){const auto& row=rows_[i];const int rh=row_height(i);if(y+rh>bottom&&i>first)break;
        const int gap=screen_==MenuScreen::graphics&&rows_[i].id==Item::filtering?(compact?6:12):0;
        if(gap)fill({content_x,y+3,content_w,1},0xff575047);
        SDL_Rect r{content_x-4,y+gap,content_w+8,std::min(rh-gap-4,bottom-y-gap)};
        if(i==focus_){fill(r,0xff302b20);fill({r.x,y,2,r.h},accent_);}
        const auto color=!row.enabled?0xff99958b:i==focus_?accent_:ink_;
        if(screen_==MenuScreen::resources&&row.id!=Item::back){
            const auto action=tr(MenuString::resource_file_button);
            const int action_x=content_x+content_w-12-int(cp866(action).size())*8;
            const int status_x=content_x+content_w-12-int(cp866(action).size())*8-24-18*8;
            text(row.label,content_x+12,y+4,color);
            text(row.value,resource_stacked?content_x+12:status_x,resource_stacked?y+text_height_+8:y+4,ink_);
            box({action_x-6,resource_stacked?y+text_height_+4:y,int(cp866(action).size())*8+12,text_height_+10});
            text(action,action_x,resource_stacked?y+text_height_+8:y+4,color);
            hits_.push_back({r,i});y+=rh;last=i+1;continue;
        }
        int ty=y+gap+4;const int chars=std::max(1,(content_w-28)/8);auto label=cp866(row.label);
        auto wrapped=paginate_story({reinterpret_cast<const std::uint8_t*>(label.data()),label.size()},chars,10);
        if(!wrapped.empty())for(const auto& line:wrapped[0]){text(line,content_x+12,ty,color,true);ty+=text_height_+4;}
        if(!row.value.empty()){
            const bool split=editions||int(cp866(row.label).size()+cp866(row.value).size())*8>content_w-44;
            text(row.value,split?content_x+12:content_x+content_w-12-int(cp866(row.value).size())*8,split?ty:y+gap+4,color);
        }
        hits_.push_back({r,i});y+=rh;last=i+1;
    }
    if(first>0||last<int(rows_.size())){
        const int track=std::max(1,bottom-top),thumb=std::max(8,track*(last-first)/int(rows_.size()));
        fill({margin+panel_width-7,top,2,track},0xff444038);
        fill({margin+panel_width-7,top+(track-thumb)*first/std::max(1,int(rows_.size())-(last-first)),2,thumb},accent_);
    }
    if(show_help){
        fill({margin,footer_top,panel_width,footer_height},shade_);
        const auto& help=rows_[focus_].help;
        if(!help.empty())paragraph(help,{content_x,footer_top+4,content_w,footer_height-text_height_-12});
        else if(screen_==MenuScreen::resources&&!errors_.empty())paragraph(errors_,{content_x,footer_top+4,content_w,footer_height-text_height_-12});
    }
    const auto hint=compact?tr("Enter: OK   Esc: назад","Enter: OK   Esc: back"):pad_?tr(MenuString::d_pad_select_a_ok_b_back):tr(MenuString::arrows_select_enter_ok_esc_back);
    centered(hint,footer_top+footer_height-text_height_,ink_);
    return frame_;
}
} // namespace dok
