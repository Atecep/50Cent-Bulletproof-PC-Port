#include "src/game/native_menu_state.h"
#include <map>
#include <string>
#include <cstdlib>
#include <iostream>
static void Check(bool yes,const char* why){if(!yes){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 const char* mainLabels[] = {"New Game", "Load Game", "Options", "Exit Game"};
 for (int row = 0; row < 4; ++row) {
  Check(std::string(native_menu::MainMenuLabel(row)) == mainLabels[row], "Main menu order");
 }
 Check(native_menu::ExitGameRow == 3 && !native_menu::MainMenuLabel(4), "Exit Game is last");

 for (const bool pause : {false, true}) {
  const auto count = native_menu::OptionsOriginalCount(pause);
  Check(count == (pause ? 6 : 5), "Original scene widget count");
  const char* labels[] = {"Controller", "Keyboard & Mouse", "Camera", "Audio/Video",
                          "Customize Pager", pause ? "Codes" : "Credits", "Autosave"};
  for (int row = 0; row <= count; ++row) {
   Check(std::string(native_menu::OptionsLabel(pause, row)) == labels[row], "Options order");
   if (row != native_menu::OptionsKeyboardRow) {
    Check(native_menu::OptionsWidgetRow(row) == (row == 0 ? 0 : row - 1),
          "Original Options entry keeps its original activation widget");
   }
  }
  Check(native_menu::OptionsLabel(pause, count + 1) == nullptr, "Options bounds");
 }

 std::map<uint32_t,uint32_t> ram;
 std::map<uint32_t,uint8_t> bytes;
 auto read=[&](uint32_t a){return ram[a];};auto byte=[&](uint32_t a){return bytes[a];};
 Check(!native_menu::Descriptor(read),"no menu before stack initialization");
 ram[0x32A170]=0x9000; // unrelated SceneManager pointer
 ram[0x32A17C]=2;ram[0x32A184]=0x1000;ram[0x1000]=0x2000;ram[0x1004]=0x3000;
 ram[0x200C]=0x3335A0;ram[0x300C]=0x332B14;
 Check(native_menu::Controller(read)==0x3000 && native_menu::Descriptor(read)==0x332B14,
       "active descriptor follows top controller, not depth or SceneManager");
 ram[0x3004]=0x7000;ram[0x3008]=0x4000;ram[0x4028]=0x5000;ram[0x4030]=4;
 for(unsigned i=0;i<4;++i){
  auto name=std::string("ROW")+char('0'+i);unsigned va=0x6000+i*16;
  for(unsigned j=0;j<name.size();++j)bytes[va+j]=name[j];
  ram[0x332B14+0x28+i*4]=va;ram[0x5000+i*8]=0x7000+i*256;
  ram[0x5004+i*8]=native_menu::NameHash(va,byte);
 }
 Check(native_menu::SelectedSlot(read,byte)==0,"slot0 follows native selected widget");
 ram[0x3004]=0x7300;Check(native_menu::SelectedSlot(read,byte)==3,"slot3 follows native selected widget");
 ram[0x300C]=0x3329AC;ram[0x3004]=0x7400;ram[0x3329AC+0x28+4*4]=0x6040;
 for(unsigned j=0;j<5;++j)bytes[0x6040+j]="ROW4"[j];
 ram[0x5000+4*8]=0x7400;ram[0x5004+4*8]=native_menu::NameHash(0x6040,byte);ram[0x4030]=5;
 Check(native_menu::SelectedSlot(read,byte)==4,"slot4 follows 5th widget in Controls descriptor 0x3329AC");
 ram[0x300C]=0x332BC8;ram[0x3004]=0x7200;ram[0x332BC8+0x28+2*4]=0x6020;
 for(unsigned j=0;j<5;++j)bytes[0x6020+j]="ROW2"[j];
 ram[0x5000+2*8]=0x7200;ram[0x5004+2*8]=native_menu::NameHash(0x6020,byte);ram[0x4030]=3;
 Check(native_menu::SelectedSlot(read,byte)==2,"slot2 follows 3rd widget in original Pager descriptor 0x332BC8");
 ram[0x300C]=0x332B14;ram[0x3004]=0x7300;
 ram[0x32A17C]=1;Check(native_menu::Descriptor(read)==0x3335A0,"pop resumes parent menu");
 native_menu::ActionReleaseGuard guard;
 Check(!guard.Block(false,true),"ordinary B reaches the native menu");
 guard.Arm();
 for(int i=0;i<20;++i)Check(guard.Block(false,true),"held revert B cannot pop the underlying menu");
 Check(guard.Block(false,false),"release terminates the confirmation press");
 Check(!guard.Block(false,true),"a later independent B works normally");
 guard.Arm();Check(guard.Block(true,false),"held keep A cannot activate a native widget");
 std::cout<<"PASS: real menu stack, named widgets, native selected rows and parent restoration\n";
}
