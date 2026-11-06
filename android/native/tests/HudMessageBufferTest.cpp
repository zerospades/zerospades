#include "../HudMessageBuffer.h"
#include <cassert>
#include <cstdint>
#include <string>
using spades::android::HudMessageBuffer;
static std::uint32_t integer(const std::string& s, std::size_t& at) {
    assert(at+4 <= s.size()); std::uint32_t result=0;
    for (int i=0;i<4;++i) result=(result<<8)|static_cast<unsigned char>(s[at++]);
    return result;
}
static std::string part(const std::string& s,std::size_t& at,std::size_t length) {
    assert(at+length<=s.size()); auto result=s.substr(at,length); at+=length; return result;
}
int main() {
    HudMessageBuffer buffer; std::string value;
    assert(buffer.TakeKills(value) && value.empty()); assert(!buffer.TakeKills(value));
    buffer.AddChat("hello");
    buffer.AddPlayerChat("[Team] ", u8"नमस्ते 😀", ": hi\nthere", 0x123456);
    value=buffer.TakeChat(); std::size_t at=0;
    assert(integer(value,at)==0xffffff); assert(integer(value,at)==0);
    assert(integer(value,at)==0); assert(integer(value,at)==5); assert(part(value,at,5)=="hello");
    assert(integer(value,at)==0x123456);
    auto a=integer(value,at), b=integer(value,at), c=integer(value,at);
    assert(part(value,at,a)=="[Team] "); assert(part(value,at,b)==u8"नमस्ते 😀");
    assert(part(value,at,c)==": hi\nthere"); assert(at==value.size()); assert(buffer.TakeChat().empty());
    for (int i=0;i<101;++i) buffer.AddChat(std::to_string(i));
    value=buffer.TakeChat(); at=0;
    assert(integer(value,at)==0xffffff); assert(integer(value,at)==0); assert(integer(value,at)==0);
    auto n=integer(value,at); assert(part(value,at,n)=="1");
    for (int i=0;i<7;++i) buffer.AddKill("kill\n"+std::to_string(i));
    assert(buffer.TakeKills(value)); assert(value=="kill 2\nkill 3\nkill 4\nkill 5\nkill 6");
    assert(!buffer.TakeKills(value)); buffer.AddChat("old server"); buffer.Reset();
    assert(buffer.TakeChat().empty()); assert(buffer.TakeKills(value) && value.empty());
}
