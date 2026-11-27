#pragma once
#include <cstdint>
namespace native_menu {
// 49F00/49F20/49FC0: MenuManager stores depth at+C and stack at+14.
// 32A170 itself is the manager; its first member is a different SceneManager.
template<class Read> uint32_t Controller(Read read) {
    const auto depth=read(0x0032A17C), stack=read(0x0032A184);
    if (!depth || depth>64 || !stack) return 0;
    return read(stack+(depth-1)*4);
}
template<class Read> uint32_t Descriptor(Read read) {
    const auto controller=Controller(read);
    return controller ? read(controller+0x0C) : 0;
}
}
namespace native_menu {
template<class Byte> uint32_t NameHash(uint32_t address, Byte byte) {
    uint32_t hash=0x811C9DC5;
    for (unsigned i=0;i<512;++i) {
        uint8_t c=byte(address+i);if(!c)break;
        if(c>='a'&&c<='z')c-=32;
        hash=(hash*0x01000193)^c;
    }
    return hash;
}
template<class Read,class Byte> int SelectedSlot(Read read,Byte byte) {
    const auto controller=Controller(read);if(!controller)return -1;
    const auto selected=read(controller+4),scene=read(controller+8),desc=read(controller+12);
    const auto table=read(scene+0x28), count=read(scene+0x30);
    if(!selected||!table||count>16384)return -1;
    uint32_t hash=0;bool found=false;
    for(uint32_t i=0;i<count;++i)if(read(table+i*8)==selected){hash=read(table+i*8+4);found=true;break;}
    if(!found)return -1;
    int max_slots=read(desc+4)&0xFFFF;
    if(max_slots<=0||max_slots>16) {
        max_slots=(desc==0x00332844)?6:(desc==0x003329AC||desc==0x003328F8)?5:(desc==0x00332BC8)?3:4;
    }
    for(int i=0;i<max_slots;++i)if(NameHash(read(desc+0x28+i*4),byte)==hash)return i;
    return -1;
}
}
namespace native_menu {
// Closing a confirmation must consume the press until physical release, or a
// held B becomes a fresh press in the underlying original menu on the next poll.
class ActionReleaseGuard {
    bool waiting_=false;
public:
    void Arm(){waiting_=true;}
    bool Block(bool a,bool b){const bool blocked=waiting_;if(!a&&!b)waiting_=false;return blocked;}
};
}

namespace native_menu {
inline constexpr uint32_t MainMenuDescriptor = 0x003335A0;
inline constexpr int ExitGameRow = 3;

inline constexpr const char* MainMenuLabel(int row) {
    constexpr const char* labels[] = {"New Game", "Load Game", "Options", "Exit Game"};
    return row >= 0 && row <= ExitGameRow ? labels[row] : nullptr;
}

inline constexpr int OptionsOriginalCount(bool pause) { return pause ? 6 : 5; }

inline constexpr int OptionsKeyboardRow = 1;

// Keyboard & Mouse shares Camera's highlight widget. Original entries
// still activate their own widgets at the shifted screen positions.
inline constexpr int OptionsWidgetRow(int row) {
    return row > OptionsKeyboardRow ? row - 1 : row;
}

inline constexpr const char* OptionsLabel(bool pause, int row) {
    if (row < 0 || row > OptionsOriginalCount(pause)) return nullptr;
    constexpr const char* common[] = {
        "Controller", "Keyboard & Mouse", "Camera", "Audio/Video", "Customize Pager"
    };
    if (row < 5) return common[row];
    if (!pause) return "Credits";
    return row == 5 ? "Codes" : "Autosave";
}

inline constexpr bool IsOptions(uint32_t desc) {
    return desc == 0x003328F8 || desc == 0x00332844;
}

inline constexpr uint32_t CodesTextEntry = 0x00332D30;
inline constexpr uint32_t PdaMessages = 0x00333168;
inline constexpr uint32_t PdaMusicList = 0x00332F4C;

inline constexpr bool PdaHasKeyboardShift(uint32_t desc) {
    return desc == CodesTextEntry || desc == PdaMessages || desc == PdaMusicList;
}
}
