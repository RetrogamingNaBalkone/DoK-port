#include "dok/frontend.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

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
std::string cp866(const std::string& text){
    std::string out;
    for(std::size_t i=0;i<text.size();){
        unsigned c=static_cast<unsigned char>(text[i++]);
        if(c>=0xc0){const int count=c<0xe0?1:c<0xf0?2:3;c&=count==1?31:count==2?15:7;
            for(int n=0;n<count&&i<text.size();++n)c=(c<<6)|(static_cast<unsigned char>(text[i++])&63);}
        if(c<128)out+=char(c);else if(c>=0x410&&c<=0x42f)out+=char(c-0x410+0x80);
        else if(c>=0x430&&c<=0x43f)out+=char(c-0x430+0xa0);else if(c>=0x440&&c<=0x44f)out+=char(c-0x440+0xe0);
        else if(c==0x401||c==0x451)out+=char(c==0x401?0xf0:0xf1);
        else if(c==0x2014||c==0x2013)out+='-';else if(c==0xab||c==0xbb)out+='"';else if(c==0xd7)out+='x';else out+='?';
    }return out;
}
Frontend::Frontend(AppSettings& settings):s_(settings){font_=bootstrap_font();for(int c=0;c<256;++c)for(int y=0;y<8;++y)font8_[c*8+y]=font_[c*16+y*2+1];frame_.pixels.resize(640*480);}
std::string Frontend::tr(MenuString id)const{return menu_text(id,s_.locale);}
std::string Frontend::tr(const char* ru,const char* en)const{return s_.locale==LocaleId::ru?ru:en;}
const Archive* Frontend::archive()const{return sets_.empty()?nullptr:sets_[dataset_].archive;}
void Frontend::datasets(std::vector<FrontendDataset> sets,const std::string& errors){
    sets_=std::move(sets);errors_=errors;dataset_=0;
    for(std::size_t i=0;i<sets_.size();++i)if(sets_[i].archive->identify()==s_.last_dataset)dataset_=i;
    themed_=nullptr;art_.clear();
}
void Frontend::select_dataset(std::size_t index){if(index<sets_.size()){dataset_=index;s_.last_dataset=archive()->identify();themed_=nullptr;art_.clear();}}
void Frontend::preview_level(int level){level_=level;in_session_=false;enter(MenuScreen::levels);}
void Frontend::enter(MenuScreen screen,int focus){screen_=screen;focus_=focus;page_=0;capture_start_=false;hits_.clear();}
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
void Frontend::update(std::uint64_t ms){now_=ms;if(video_pending()&&ms>=video_deadline_){s_.video=old_video_;draft_=old_video_;enter(MenuScreen::graphics,4);commands_.push_back(MenuCommand::revert_video);}}
void Frontend::begin_game(){
    if(!archive()){enter(MenuScreen::resources);return;}
    s_.last_dataset=archive()->identify();level_=archive()->identify()==DatasetId::old_gold?4:1;
    enter(MenuScreen::title);commands_.push_back(MenuCommand::music_title);
}
std::string Frontend::action_name(int a)const{
    static const char* ru[]={"Вперёд","Назад","Шаг влево","Шаг вправо","Поворот влево","Поворот вправо","Огонь","Открыть дверь","Карта","Пауза","Бег","Медленное движение","Следующее оружие","Предыдущее оружие","Быстрое сохранение","Быстрая загрузка","Модификатор стрейфа","Оружие 1","Оружие 2"};
    static const char* en[]={"Forward","Backward","Strafe left","Strafe right","Turn left","Turn right","Fire","Open door","Map","Pause","Run","Slow movement","Next weapon","Previous weapon","Quicksave","Quickload","Strafe modifier","Weapon 1","Weapon 2"};
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
    if(slot==2)return b.mouse[a]?tr(MenuString::mouse)+std::to_string(b.mouse[a]):tr(MenuString::unbound);
    auto key=[&](int n){const char* name=SDL_GetScancodeName(b.keys[a][n]);return std::string(name&&*name?name:"-");};
    if(slot>=0)return key(slot);
    auto name=key(0);if(b.keys[a][1])name+=" / "+key(1);if(b.mouse[a])name+=" + "+tr(MenuString::m)+std::to_string(b.mouse[a]);return name;
}
void Frontend::build_rows(){
    rows_.clear();auto row=[&](std::string label,std::string value={},std::string help={},bool enabled=true){rows_.push_back({std::move(label),std::move(value),std::move(help),enabled});};
    const auto on=[&](bool b){return b?tr(MenuString::on):tr(MenuString::off);};
    const auto defaults=[&]{row(tr(MenuString::restore_defaults));};
    const auto backrow=[&]{row(tr(MenuString::back));};
    const auto preset=[&](InputPreset p){return p==InputPreset::classic?tr(MenuString::classic):p==InputPreset::modern?tr(MenuString::modern):tr(MenuString::custom);};
    switch(screen_){
    case MenuScreen::main:row(tr(MenuString::start_game));row(tr(MenuString::settings));row(tr(MenuString::credits));row(tr(MenuString::quit));break;
    case MenuScreen::datasets:
        for(const auto& item:sets_){const auto id=item.archive->identify();row(id==DatasetId::old_gold?"Old Gold":tr(MenuString::dungeons_of_kremlin),
            id==DatasetId::kremlin_pkr?tr(MenuString::russian_edition_pkr):id==DatasetId::kremlin_pke?tr(MenuString::english_edition_pke):tr(MenuString::original_edition),
            tr(MenuString::editions_have_different_game_parameters_interface_langu));}backrow();break;
    case MenuScreen::settings:for(const auto& label:{tr(MenuString::graphics),tr(MenuString::sound),tr(MenuString::controls),tr(MenuString::gameplay),tr(MenuString::interface)})row(label);backrow();break;
    case MenuScreen::graphics:{
        const bool software=draft_.renderer==RendererKind::software;
        row(tr(MenuString::renderer),software?tr(MenuString::software):"OpenGL");
        row(tr(MenuString::window_mode),draft_.fullscreen?tr(MenuString::borderless):tr(MenuString::windowed));
        auto mode=software?software_modes[draft_.software_mode]:std::array<int,2>{draft_.width,draft_.height};
        row(software?tr(MenuString::original_resolution):tr(MenuString::window_resolution),std::to_string(mode[0])+" x "+std::to_string(mode[1]),
            software?tr(MenuString::software_framebuffer_size_also_resizes_a_window_manual):
            tr(MenuString::borderless_uses_the_desktop_resolution),software||!draft_.fullscreen);
        row(tr(MenuString::smoothing),on(s_.linear),tr(MenuString::linear_scene_filtering_text_and_menus_remain_sharp));
        row(tr(MenuString::apply_video_mode));defaults();backrow();break;}
    case MenuScreen::sound:row(tr(MenuString::master_volume),std::to_string(s_.master_volume)+"%");row(tr(MenuString::effects),std::to_string(s_.effects_volume)+"%");row(tr(MenuString::title_music),std::to_string(s_.music_volume)+"%");defaults();backrow();break;
    case MenuScreen::controls:row(tr(MenuString::keyboard_and_mouse));row(tr(MenuString::gamepad));backrow();break;
    case MenuScreen::keyboard:
        row(tr(MenuString::preset),preset(s_.input.keyboard_preset));row(tr(MenuString::remap_controls));
        row(tr(MenuString::mouse_sensitivity),decimal(s_.input.mouse_degrees_per_count));defaults();backrow();break;
    case MenuScreen::pad:
        row(tr(MenuString::preset),preset(s_.input.pad_preset));row(tr(MenuString::remap_controls));
        row(tr(MenuString::turn_sensitivity),decimal(s_.input.turn_sensitivity));
        row(tr(MenuString::deadzone),std::to_string(int(s_.input.stick_deadzone*100+0.5f))+"%");defaults();backrow();break;
    case MenuScreen::bindings:
        for(int i=0;i<actions;++i){const bool future=i==int(Action::map)||i==int(Action::quicksave)||i==int(Action::quickload);
            row(action_name(i),binding_name(i,binding_parent_==MenuScreen::pad),future?tr(MenuString::this_game_action_is_not_implemented_yet):tr(MenuString::select_an_action_to_change_its_binding));}backrow();break;
    case MenuScreen::key_slots:row(tr(MenuString::primary_key),binding_name(action_,false,0));row(tr(MenuString::secondary_key),binding_name(action_,false,1));row(tr(MenuString::mouse_button),binding_name(action_,false,2));row(tr(MenuString::clear_bindings));backrow();break;
    case MenuScreen::gameplay:row(tr(MenuString::movement_inertia),on(s_.inertia),tr(MenuString::off_instant_start_and_stop_water_currents_still_move_th));row(tr(MenuString::saving),tr(MenuString::in_development),{},false);defaults();backrow();break;
    case MenuScreen::interface:row("Язык / Language",s_.locale==LocaleId::ru?"Русский":"English",tr(MenuString::story_and_in_game_messages_currently_use_the_original_r));row(tr(MenuString::show_fps),on(s_.show_fps));defaults();backrow();break;
    case MenuScreen::pause:row(tr(MenuString::resume));row(tr(MenuString::settings));row(tr(MenuString::main_menu));row(tr(MenuString::quit));break;
    case MenuScreen::confirm:row(fallback_?tr(MenuString::use_software_renderer):tr(MenuString::yes));row(tr(MenuString::cancel));break;
    case MenuScreen::conflict:row(tr(MenuString::replace_binding));row(tr(MenuString::cancel));break;
    case MenuScreen::video_confirm:row(tr(MenuString::keep_video_mode));row(tr(MenuString::revert));break;
    case MenuScreen::resources:case MenuScreen::notice:backrow();break;
    case MenuScreen::credits:row(tr(MenuString::next_page));backrow();break;
    case MenuScreen::levels:
        row(tr(MenuString::start));row(tr(MenuString::next_page));backrow();break;
    case MenuScreen::readme:case MenuScreen::ending:row(tr(MenuString::continue_action));backrow();break;
    case MenuScreen::title:row(tr(MenuString::continue_2));backrow();break;
    case MenuScreen::outcome:row(action_==int(SessionPhase::retry)?tr(MenuString::retry):tr(MenuString::level_selection));row(tr(MenuString::main_menu));break;
    default:break;
    }
    if(!rows_.empty())focus_=std::clamp(focus_,0,int(rows_.size())-1);
}
void Frontend::back(){
    switch(screen_){
    case MenuScreen::hidden:pause();break;
    case MenuScreen::main:commands_.push_back(MenuCommand::quit);break;
    case MenuScreen::pause:hide();commands_.push_back(MenuCommand::resume);break;
    case MenuScreen::settings:enter(in_session_?MenuScreen::pause:MenuScreen::main,1);break;
    case MenuScreen::graphics:case MenuScreen::sound:case MenuScreen::controls:case MenuScreen::gameplay:case MenuScreen::interface:enter(MenuScreen::settings);break;
    case MenuScreen::keyboard:case MenuScreen::pad:enter(MenuScreen::controls,screen_==MenuScreen::pad?1:0);break;
    case MenuScreen::bindings:enter(binding_parent_,1);break;
    case MenuScreen::key_slots:enter(MenuScreen::bindings,action_);break;
    case MenuScreen::capture:case MenuScreen::conflict:enter(binding_parent_==MenuScreen::pad?MenuScreen::bindings:MenuScreen::key_slots,binding_parent_==MenuScreen::pad?action_:slot_);break;
    case MenuScreen::confirm:if(fallback_){fallback_=false;enter(MenuScreen::graphics);}else enter(MenuScreen::pause);break;
    case MenuScreen::notice:enter(notice_return_);break;
    case MenuScreen::video_confirm:s_.video=old_video_;draft_=old_video_;enter(MenuScreen::graphics,4);commands_.push_back(MenuCommand::revert_video);break;
    case MenuScreen::ending:case MenuScreen::outcome:commands_.push_back(MenuCommand::main);main_menu();break;
    default:commands_.push_back(MenuCommand::music_stop);enter(MenuScreen::main);break;
    }
}
void Frontend::choose(int delta){
    build_rows();if(rows_.empty()||!rows_[focus_].enabled)return;
    const int d=delta?delta:1;
    switch(screen_){
    case MenuScreen::main:if(focus_==0){if(sets_.size()>1)enter(MenuScreen::datasets);else begin_game();}else if(focus_==1)enter(MenuScreen::settings);else if(focus_==2){credit_page_=0;enter(MenuScreen::credits);}else commands_.push_back(MenuCommand::quit);break;
    case MenuScreen::datasets:if(focus_==int(sets_.size()))back();else{select_dataset(focus_);begin_game();}break;
    case MenuScreen::title:
        if(focus_==1){back();break;}commands_.push_back(MenuCommand::music_stop);
        if(archive()->identify()==DatasetId::old_gold){enter(MenuScreen::levels);commands_.push_back(MenuCommand::music_story);}
        else{enter(MenuScreen::readme);commands_.push_back(MenuCommand::music_title);}break;
    case MenuScreen::readme:
        if(focus_==1){back();break;}
        if(++page_>=page_count_){enter(MenuScreen::levels);commands_.push_back(MenuCommand::music_stop);}break;
    case MenuScreen::levels:{
        if(focus_==0){commands_.push_back(MenuCommand::music_stop);commands_.push_back(MenuCommand::start);hide();}
        else if(focus_==1)page_=std::max(0,page_+d);else back();break;}
    case MenuScreen::settings:{static const MenuScreen screens[]={MenuScreen::graphics,MenuScreen::sound,MenuScreen::controls,MenuScreen::gameplay,MenuScreen::interface};
        if(focus_==5)back();else{draft_=s_.video;enter(screens[focus_]);}break;}
    case MenuScreen::graphics:
        if(focus_==0)draft_.renderer=draft_.renderer==RendererKind::opengl?RendererKind::software:RendererKind::opengl;
        else if(focus_==1)draft_.fullscreen=!draft_.fullscreen;
        else if(focus_==2){
            if(draft_.renderer==RendererKind::software){draft_.software_mode=(draft_.software_mode+d+5)%5;const auto size=software_modes[draft_.software_mode];draft_.width=size[0];draft_.height=size[1];}
            else{const std::array<std::array<int,2>,6> sizes{{{640,480},{800,600},{1024,768},{1280,720},{1600,900},{1920,1080}}};int at=-1;
                for(int i=0;i<int(sizes.size());++i)if(sizes[i][0]==draft_.width&&sizes[i][1]==draft_.height)at=i;
                at=(at+d+int(sizes.size()))%int(sizes.size());draft_.width=sizes[at][0];draft_.height=sizes[at][1];}
        }else if(focus_==3)s_.linear=!s_.linear;
        else if(focus_==4){old_video_=s_.video;s_.video=draft_;video_deadline_=now_+15000;enter(MenuScreen::video_confirm);commands_.push_back(MenuCommand::apply_video);}
        else if(focus_==5){draft_=VideoSettings{};s_.linear=false;}else back();break;
    case MenuScreen::sound:if(focus_<3){auto& v=focus_==0?s_.master_volume:focus_==1?s_.effects_volume:s_.music_volume;v=std::clamp(v+d*5,0,100);}else if(focus_==3)s_.master_volume=s_.effects_volume=s_.music_volume=100;else back();break;
    case MenuScreen::controls:if(focus_<2)enter(focus_?MenuScreen::pad:MenuScreen::keyboard);else back();break;
    case MenuScreen::keyboard:case MenuScreen::pad:{
        const bool pad=screen_==MenuScreen::pad;
        if(focus_==0){const auto p=pad?s_.input.pad_preset:s_.input.keyboard_preset;const auto next=p==InputPreset::classic?InputPreset::modern:InputPreset::classic;if(pad)s_.input.gamepad(next);else s_.input.keyboard(next);}
        else if(focus_==1){binding_parent_=screen_;enter(MenuScreen::bindings);}
        else if(focus_==2){if(pad)s_.input.turn_sensitivity=std::clamp(s_.input.turn_sensitivity+d*0.1f,0.1f,3.0f);else s_.input.mouse_degrees_per_count=std::clamp(s_.input.mouse_degrees_per_count+d*0.01f,0.01f,2.0f);}
        else if(pad&&focus_==3)s_.input.stick_deadzone=std::clamp(s_.input.stick_deadzone+d*0.01f,0.0f,0.8f);
        else if(focus_==(pad?4:3)){if(pad){s_.input.gamepad(InputPreset::modern);s_.input.turn_sensitivity=1;s_.input.stick_deadzone=0.18f;}else{s_.input.keyboard(InputPreset::modern);s_.input.mouse_degrees_per_count=0.15f;}}
        else back();break;}
    case MenuScreen::bindings:if(focus_==actions)back();else{action_=focus_;enter(binding_parent_==MenuScreen::pad?MenuScreen::capture:MenuScreen::key_slots);}break;
    case MenuScreen::key_slots:if(focus_<3){slot_=focus_;enter(MenuScreen::capture);}else if(focus_==3){s_.input.keys[action_]={};s_.input.mouse[action_]=0;s_.input.keyboard_preset=InputPreset::custom;}else back();break;
    case MenuScreen::gameplay:if(focus_==0)s_.inertia=!s_.inertia;else if(focus_==2)s_.inertia=true;else if(focus_==3)back();break;
    case MenuScreen::interface:if(focus_==0)s_.locale=s_.locale==LocaleId::ru?LocaleId::en:LocaleId::ru;else if(focus_==1)s_.show_fps=!s_.show_fps;else if(focus_==2){s_.locale=system_locale();s_.show_fps=true;}else back();break;
    case MenuScreen::pause:if(focus_==0){hide();commands_.push_back(MenuCommand::resume);}else if(focus_==1)enter(MenuScreen::settings);else{confirm_quit_=focus_==3;fallback_=false;enter(MenuScreen::confirm,1);}break;
    case MenuScreen::confirm:if(focus_==1)back();else if(fallback_){fallback_=false;old_video_=s_.video;draft_=s_.video;draft_.renderer=RendererKind::software;s_.video=draft_;video_deadline_=now_+15000;enter(MenuScreen::video_confirm);commands_.push_back(MenuCommand::apply_video);}else{commands_.push_back(confirm_quit_?MenuCommand::quit:MenuCommand::main);if(!confirm_quit_)main_menu();}break;
    case MenuScreen::conflict:if(focus_==0)accept_binding();else back();break;
    case MenuScreen::video_confirm:if(focus_==0){draft_=s_.video;enter(MenuScreen::graphics,4);commands_.push_back(MenuCommand::keep_video);}else back();break;
    case MenuScreen::credits:if(focus_==0)credit_page_=(credit_page_+d+page_count_)%page_count_;else back();break;
    case MenuScreen::ending:if(focus_==1)back();else{const auto name=archive()->identify()==DatasetId::old_gold?"INTRO4E":"EINTRO"+std::to_string(level_);
        if(++page_>=page_count_){commands_.push_back(MenuCommand::main);in_session_=false;enter(MenuScreen::levels);}}break;
    case MenuScreen::outcome:if(focus_==1)back();else if(action_==int(SessionPhase::retry)){hide();commands_.push_back(MenuCommand::retry);}else{commands_.push_back(MenuCommand::main);in_session_=false;enter(MenuScreen::levels);}break;
    case MenuScreen::notice:case MenuScreen::resources:back();break;
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
    }else if(slot_==2){if(e.type!=SDL_MOUSEBUTTONDOWN||e.button.button>5)return;capture_kind_=1;capture_value_=e.button.button;}
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
    if(e.type==SDL_KEYDOWN||e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEMOTION)pad_=false;
    if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERAXISMOTION)pad_=true;
    if(screen_==MenuScreen::capture){capture(e);return;}
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
            if(hit.row>=100){if(e.type==SDL_MOUSEBUTTONDOWN){level_=hit.row-100;page_=0;}return;}
            focus_=hit.row;if(e.type==SDL_MOUSEBUTTONDOWN){accept=e.button.button==SDL_BUTTON_LEFT;cancel=e.button.button==SDL_BUTTON_RIGHT;}break;
        }
    }
    if(cancel){back();return;}
    if(nav){for(int i=0;i<int(rows_.size());++i){focus_=(focus_+nav+int(rows_.size()))%int(rows_.size());if(rows_[focus_].enabled)break;}}
    if(adjust){
        if(screen_==MenuScreen::levels&&archive()->identify()!=DatasetId::old_gold){level_=(level_-1+adjust+3)%3+1;page_=0;}
        else if(screen_==MenuScreen::credits)credit_page_=(credit_page_+adjust+page_count_)%page_count_;
        else if(screen_==MenuScreen::graphics||screen_==MenuScreen::sound||screen_==MenuScreen::keyboard||screen_==MenuScreen::pad||screen_==MenuScreen::gameplay||screen_==MenuScreen::interface){
            if(focus_<int(rows_.size())-2)choose(adjust);
        }
    }
    if(accept)choose();
}
void Frontend::theme(){
    if(themed_==archive()&&!font_.empty())return;themed_=archive();art_.clear();font_=bootstrap_font();background_={};
    if(!themed_)return;
    colors_=themed_->palette(cp866("МЕНЮ_ПАЛ"));background_=themed_->image("FON");
    const auto f=themed_->font("FONT16");std::copy(f.data,f.data+std::min(f.size,font_.size()),font_.begin());
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
    for(unsigned char c:bytes){for(int row=0;row<text_height_;++row){const int src=row*16/text_height_;const auto bits=text_height_==8?font8_[c*8+row]:font_[c*16+src];
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
    if(!archive()||!archive()->find(name))return {{}};auto result=paginate_story(archive()->get(name),columns,rows);if(result.empty())result.push_back({});return result;
}
const MenuFrame& Frontend::render(int width,int height){
    // Integer-sized glyphs. On VGA-sized windows use FONT16 sampled to eight rows.
    const int scale=std::max(1,std::min(width/640,height/360));
    frame_.width=std::clamp(width/scale,320,1280);frame_.height=std::max(200,height/scale);
    text_height_=frame_.height<400?8:16;line_height_=text_height_+10;
    frame_.pixels.assign(std::size_t(frame_.width)*frame_.height,shade_);theme();hits_.clear();build_rows();
    const int w=frame_.width,h=frame_.height;
    if(!background_.pixels.empty())for(int y=0;y<h;++y)for(int x=0;x<w;++x)frame_.pixels[y*w+x]=colors_[background_.pixels[(y%background_.height)*background_.width+x%background_.width]];
    std::string heading=tr(MenuString::dungeons_of_kremlin_old_gold);
    switch(screen_){
    case MenuScreen::settings:heading=tr(MenuString::settings_2);break;case MenuScreen::graphics:heading=tr(MenuString::graphics_2);break;
    case MenuScreen::sound:heading=tr(MenuString::sound_2);break;case MenuScreen::controls:heading=tr(MenuString::controls_2);break;
    case MenuScreen::keyboard:heading=tr(MenuString::keyboard_and_mouse_2);break;case MenuScreen::pad:heading=tr(MenuString::gamepad_2);break;
    case MenuScreen::gameplay:heading=tr(MenuString::gameplay_2);break;case MenuScreen::interface:heading=tr(MenuString::interface_2);break;
    case MenuScreen::datasets:heading=tr(MenuString::select_game);break;case MenuScreen::credits:heading=tr(MenuString::credits_2);break;
    case MenuScreen::pause:heading=tr(MenuString::paused);break;case MenuScreen::bindings:heading=tr(MenuString::remap_controls_2);break;
    case MenuScreen::key_slots:heading=action_name(action_);break;case MenuScreen::levels:heading=archive()&&archive()->identify()==DatasetId::old_gold?"OLD GOLD":tr(MenuString::select_level);break;
    case MenuScreen::readme:heading=tr(MenuString::story);break;case MenuScreen::ending:heading=tr(MenuString::level_completed);break;
    case MenuScreen::capture:heading=action_name(action_);break;case MenuScreen::resources:heading=tr(MenuString::game_resources_required);break;
    default:break;
    }
    const int margin=std::max(8,std::min(32,w/24));
    int top=std::max(40,h/5),bottom=h-100;
    const int header_y=h<300?4:12;
    box({margin,header_y,w-2*margin,text_height_+12});centered(heading,header_y+6,accent_);
    if(screen_==MenuScreen::title){picture(title_sprite,title_palette,{margin,48,w-2*margin,h-110});top=h-58;bottom=h-18;}
    else if(screen_==MenuScreen::levels||screen_==MenuScreen::ending||screen_==MenuScreen::readme){
        const bool og=archive()&&archive()->identify()==DatasetId::old_gold;
        int art_top=header_y+text_height_+22;
        if(screen_==MenuScreen::levels&&!og){
            static const char* ru[]={"ХОРОМЫ НЕЧИСТИ","ПОДЗЕМНАЯ РЕКА","ПОДВАЛЫ КРЕМЛЯ"};
            static const char* en[]={"PALACE OF EVIL","SACRED RIVER","UNDERGROUND KREMLIN"};
            const int cell=(w-2*margin)/3;
            for(int i=0;i<3;++i){SDL_Rect r{margin+i*cell,art_top,cell-2,text_height_+10};box(r);const std::string label=archive()->identify()==DatasetId::kremlin_pkr?ru[i]:en[i];
                if(w>=600)text(label,r.x+std::max(4,(r.w-int(cp866(label).size())*8)/2),r.y+6,level_==i+1?accent_:ink_);
                else text(std::to_string(i+1),r.x+r.w/2-4,r.y+6,level_==i+1?accent_:ink_);
                hits_.push_back({r,101+i});}
            art_top+=text_height_+18;
        }
        const bool side=w>=600;const int content_bottom=h-int(rows_.size())*line_height_-text_height_-16;
        SDL_Rect image_rect,text_rect;
        if(side){image_rect={margin,art_top,(w-3*margin)/2,content_bottom-art_top};text_rect={image_rect.x+image_rect.w+margin,art_top,w-3*margin-image_rect.w,content_bottom-art_top};}
        else{const int available=content_bottom-art_top;const int image_height=std::max(16,std::min((available-8)/2,available-46));image_rect={margin,art_top,w-2*margin,image_height};text_rect={margin,art_top+image_height+8,w-2*margin,available-image_height-8};}
        const bool end=screen_==MenuScreen::ending;
        const auto sprite=screen_==MenuScreen::readme?std::string(title_sprite):og?"INTRO4BSPR":(end?"ESPR":"BSPR")+std::to_string(level_);
        const auto palette=screen_==MenuScreen::readme?std::string(title_palette):og?"GAMEPAL":"GAMEP"+std::to_string(level_);
        // OG's story palette is selected by its resource catalog (see SP15 evidence).
        std::string actual_palette=palette;
        if(!archive()->find(cp866(actual_palette)))actual_palette=og?"МЕНЮ_ПАЛ":palette;
        picture(sprite,actual_palette,image_rect);box(text_rect);
        const auto resource=screen_==MenuScreen::readme?"README":og?(end?"INTRO4E":"INTRO4B"):(end?"EINTRO":"BINTRO")+std::to_string(level_);
        auto pages=story(resource,std::max(1,(text_rect.w-16)/8),std::max(1,(text_rect.h-28)/(text_height_+3)));
        page_count_=int(pages.size());page_=page_%page_count_;int y=text_rect.y+8;for(const auto& line:pages[page_]){text(line,text_rect.x+8,y,ink_,true);y+=text_height_+3;}
        text(std::to_string(page_+1)+" / "+std::to_string(pages.size()),text_rect.x+8,text_rect.y+text_rect.h-18,accent_);
        top=content_bottom+8;bottom=h-text_height_-4;
    }else if(screen_==MenuScreen::credits){
        std::array<std::string,3> content;
        content[0]=tr(MenuString::dungeons_of_kremlin_n_n3_d_technology_nalexander_razbak);
        content[1]=tr(MenuString::old_gold_n_nauthors_nandrey_razbakov_nalexander_razbako);
        content[2]=tr(MenuString::sdl2_port_n_n_n_nold_gold_pkr_pke_n_noriginal_materials);
        SDL_Rect r{margin,header_y+text_height_+16,w-2*margin,h-(header_y+text_height_+16)-2*line_height_-28};box(r);
        std::vector<TextPage> pages;
        for(const auto& block:content){const auto encoded=cp866(block);auto section=paginate_story({reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()},std::max(1,(r.w-32)/8),std::max(1,(r.h-36)/(text_height_+3)));pages.insert(pages.end(),section.begin(),section.end());}
        page_count_=std::max(1,int(pages.size()));credit_page_%=page_count_;int y=r.y+8;
        if(!pages.empty())for(const auto& line:pages[credit_page_]){text(line,r.x+16,y,ink_,true);y+=text_height_+3;}
        text(std::to_string(credit_page_+1)+" / "+std::to_string(page_count_),r.x+16,r.y+r.h-16,accent_);
        top=r.y+r.h+6;bottom=h-12;
    }else if(screen_==MenuScreen::capture){
        paragraph(tr(MenuString::press_a_new_control_nesc_cancel_ngamepad_hold_start_and),{margin+16,top,w-2*margin-32,120});
    }else if(screen_==MenuScreen::confirm||screen_==MenuScreen::conflict||screen_==MenuScreen::video_confirm||screen_==MenuScreen::notice||screen_==MenuScreen::resources||screen_==MenuScreen::outcome){
        std::string message;
        if(screen_==MenuScreen::confirm)message=fallback_?tr(MenuString::opengl_could_not_be_enabled_nuse_the_software_renderer):tr(MenuString::current_progress_will_be_lost_ncontinue);
        else if(screen_==MenuScreen::conflict){message=tr(MenuString::control_already_assigned_n);for(const auto& c:conflicts_)message+=action_name(c.first)+"\n";}
        else if(screen_==MenuScreen::video_confirm)message=tr(MenuString::keep_the_new_display_settings_nreverting_in)+std::to_string((video_deadline_>now_?video_deadline_-now_:0)/1000+1)+tr(MenuString::s);
        else if(screen_==MenuScreen::notice)message=s_.locale==LocaleId::ru?notice_ru_:notice_en_;
        else if(screen_==MenuScreen::outcome)message=action_==int(SessionPhase::retry)?tr(MenuString::life_lost):tr(MenuString::game_over);
        else message=tr(MenuString::place_pkr_res_pke_res_or_old_gold_res_next_to_the_progr)+"\n\n"+errors_;
        box({margin,top,w-2*margin,h/3});paragraph(message,{margin+12,top+12,w-2*margin-24,h/3-24});top+=h/3+16;bottom=h-22;
    }
    // Dataset descriptions use two lines so RU/EN edition labels stay prominent.
    const bool editions=screen_==MenuScreen::datasets;
    const bool split_values=w<600&&std::any_of(rows_.begin(),rows_.end(),[&](const auto& row){return !row.value.empty()&&int(cp866(row.label).size()+cp866(row.value).size())*8>w-2*margin-48;});
    const int stride=editions||split_values?line_height_*2+4:line_height_;
    const int visible=std::max(1,(bottom-top)/stride),first=std::max(0,focus_-visible+1);
    for(int i=first;i<int(rows_.size())&&i<first+visible;++i){const auto& row=rows_[i];const int y=top+(i-first)*stride;
        SDL_Rect r{margin,y,w-2*margin,stride-3};box(r);const auto color=!row.enabled?0xff888888:i==focus_?accent_:ink_;
        text(i==focus_?">":" ",r.x+5,y+4,color);text(row.label,r.x+20,y+4,color);
        if(!row.value.empty()){
            if(editions||split_values)text(row.value,r.x+20,y+4+line_height_,color);
            else{const int vx=std::max(r.x+20,r.x+r.w-12-int(cp866(row.value).size())*8);text(row.value,vx,y+4,color);}
        }
        hits_.push_back({r,i});
    }
    if(first>0)text("^",w-margin-12,top-text_height_-2,accent_);
    if(first+visible<int(rows_.size()))text("v",w-margin-12,std::min(bottom,h-20),accent_);
    const bool normal=screen_!=MenuScreen::levels&&screen_!=MenuScreen::title&&screen_!=MenuScreen::readme&&screen_!=MenuScreen::ending&&screen_!=MenuScreen::credits;
    if(normal&&!rows_.empty()&&!rows_[focus_].help.empty()){box({margin,h-88,w-2*margin,62});paragraph(rows_[focus_].help,{margin+8,h-81,w-2*margin-16,51});}
    if(normal&&screen_!=MenuScreen::capture)centered(pad_?tr(MenuString::d_pad_select_a_ok_b_back):tr(MenuString::arrows_select_enter_ok_esc_back),h-text_height_-5,ink_);
    if(screen_==MenuScreen::levels)centered(pad_?tr(MenuString::arrows_level_l1_r1_text):tr(MenuString::arrows_level_pgup_dn_text),h-text_height_-4,ink_);
    return frame_;
}
} // namespace dok
