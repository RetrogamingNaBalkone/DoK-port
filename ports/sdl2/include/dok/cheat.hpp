#pragma once
#include <cstddef>
namespace dok {
class CheatSequence {
public:
    void reset() noexcept { matched_=0; }
    bool push(char letter) noexcept {
        constexpr char code[]="IDKFA";
        if(letter>='a'&&letter<='z')letter-=('a'-'A');
        if(letter==code[matched_])++matched_;
        else matched_=letter=='I'?1:0;
        if(matched_==5){reset();return true;}
        return false;
    }
private:
    std::size_t matched_=0;
};
}
