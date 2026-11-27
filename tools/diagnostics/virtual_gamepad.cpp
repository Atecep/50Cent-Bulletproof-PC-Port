// Linux diagnostic controller: real evdev/uinput events consumed through SDL.
// No game hooks, memory writes or keyboard/mouse emulation. Device is removed
// when this process exits. Requires access to /dev/uinput.
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
static int device=-1;
static void Event(int type,int code,int value) {
    input_event e{};e.type=type;e.code=code;e.value=value;
    if(write(device,&e,sizeof(e))!=sizeof(e)){perror("write input");std::exit(1);}
}
int main() {
    device=open("/dev/uinput",O_WRONLY|O_NONBLOCK);
    if(device<0){perror("/dev/uinput");return 1;}
    const std::map<std::string,int> buttons={{"a",BTN_A},{"b",BTN_B},{"x",BTN_X},
        {"y",BTN_Y},{"lb",BTN_TL},{"rb",BTN_TR},{"back",BTN_SELECT},{"start",BTN_START},
        {"guide",BTN_MODE},{"ls",BTN_THUMBL},{"rs",BTN_THUMBR},
        {"f1",KEY_F1},{"f2",KEY_F2},{"f10",KEY_F10},{"esc",KEY_ESC},
        {"enter",KEY_ENTER},{"up",KEY_UP},{"down",KEY_DOWN},{"left",KEY_LEFT},{"right",KEY_RIGHT},
        {"space",KEY_SPACE},{"backspace",KEY_BACKSPACE},{"w",KEY_W},{"s",KEY_S},{"key_a",KEY_A},{"d",KEY_D}};
    const std::map<std::string,int> axes={{"lx",ABS_X},{"ly",ABS_Y},{"rx",ABS_RX},
        {"ry",ABS_RY},{"lt",ABS_Z},{"rt",ABS_RZ},{"dx",ABS_HAT0X},{"dy",ABS_HAT0Y}};
    ioctl(device,UI_SET_EVBIT,EV_KEY);ioctl(device,UI_SET_EVBIT,EV_ABS);
    for(auto [name,code]:buttons)ioctl(device,UI_SET_KEYBIT,code);
    for(auto [name,code]:axes) {
        ioctl(device,UI_SET_ABSBIT,code);uinput_abs_setup a{};a.code=code;
        a.absinfo.minimum=(code==ABS_Z||code==ABS_RZ)?0:code>=ABS_HAT0X?-1:-32768;
        a.absinfo.maximum=(code==ABS_Z||code==ABS_RZ)?255:code>=ABS_HAT0X?1:32767;
        if(ioctl(device,UI_ABS_SETUP,&a)<0){perror("UI_ABS_SETUP");return 1;}
    }
    uinput_setup setup{};setup.id={BUS_USB,0x045e,0x028e,0x0114};
    std::strcpy(setup.name,"Bulletproof validation Xbox 360 pad");
    if(ioctl(device,UI_DEV_SETUP,&setup)<0||ioctl(device,UI_DEV_CREATE)<0){perror("UI_DEV_CREATE");return 1;}
    std::cout<<"READY: button <a|b|x|y|start|back|lb|rb|ls|rs> <0|1>; axis <lx|ly|rx|ry|lt|rt|dx|dy> <value>; neutral; quit\n"<<std::flush;
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream command(line);std::string kind,name;int value=0;command>>kind>>name>>value;
        if(kind=="quit")break;
        if(kind=="pulse"&&buttons.contains(name)) {
            Event(EV_KEY,buttons.at(name),1);Event(EV_SYN,SYN_REPORT,0);
            std::this_thread::sleep_for(std::chrono::milliseconds(value>0?value:150));
            Event(EV_KEY,buttons.at(name),0);
        }
        else if(kind=="button"&&buttons.contains(name))Event(EV_KEY,buttons.at(name),value!=0);
        else if(kind=="axis"&&axes.contains(name))Event(EV_ABS,axes.at(name),value);
        else if(kind=="neutral") {
            for(auto [key,code]:buttons)Event(EV_KEY,code,0);
            for(auto [key,code]:axes)Event(EV_ABS,code,0);
        } else {std::cout<<"ERROR: invalid command\n"<<std::flush;continue;}
        Event(EV_SYN,SYN_REPORT,0);std::cout<<"OK "<<line<<'\n'<<std::flush;
    }
    ioctl(device,UI_DEV_DESTROY);close(device);
}
