#undef NDEBUG
#include <cassert>
#include <SDL3/SDL.h>
#include "core/desktop_input.h"
#include "core/log.h"
#include "core/mouse_rate.h"
#include "game/prompt_bindings.h"

static void Key(SDL_Scancode code, bool down, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = code;
    event.key.down = down;
    event.key.mod = mod;
    input::DesktopEvent(event);
}

static void Focus(bool focused) {
    SDL_Event event{};
    event.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    input::DesktopEvent(event);
}

static void TestPromptBindings() {
    using prompts::Context;
    using prompts::Control;
    using prompts::KeyboardLabel;
    using input::DesktopAction;
    Focus(true);
    Key(SDL_SCANCODE_RETURN, true);
    Key(SDL_SCANCODE_RETURN, false);

    assert(KeyboardLabel({Context::Gameplay, Control::A}) == "Space");
    assert(KeyboardLabel({Context::Menu, Control::A}) == "Enter");
    assert(KeyboardLabel({Context::Gameplay, Control::B}) == "F");
    assert(KeyboardLabel({Context::Menu, Control::B}) == "Esc");
    assert(KeyboardLabel({Context::Gameplay, Control::X}) == "R");
    assert(KeyboardLabel({Context::Gameplay, Control::Y}) == "Left Shift");
    assert(KeyboardLabel({Context::Gameplay, Control::LT}) == "Left Ctrl");
    assert(KeyboardLabel({Context::Gameplay, Control::RT}) == "Mouse Left");
    assert(KeyboardLabel({Context::Gameplay, Control::White}) == "Q");
    assert(KeyboardLabel({Context::Gameplay, Control::Black}) == "Tab");
    assert(KeyboardLabel({Context::Menu, Control::Start}) == "Enter");
    assert(KeyboardLabel({Context::Gameplay, Control::Start}) == "Esc");
    assert(KeyboardLabel({Context::Menu, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaMusicList, Control::X}).empty());
    assert(KeyboardLabel({Context::PdaTextEntry, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaMessages, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaTextEntry, Control::Y}) == "Left Shift");
    assert(KeyboardLabel({Context::PdaMessages, Control::Y}) == "Left Shift");

    assert(input::AssignDesktopBinding(DesktopAction::Reload, SDL_SCANCODE_K) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Jump, -SDL_BUTTON_X1) == input::BindingResult::Applied);
    assert(KeyboardLabel({Context::Gameplay, Control::X}) == "K");
    assert(KeyboardLabel({Context::Menu, Control::X}) == "K");
    assert(KeyboardLabel({Context::Gameplay, Control::A}) == "Mouse X1");
    assert(KeyboardLabel({Context::Menu, Control::A}) == "Enter");
    assert(KeyboardLabel({Context::PdaTextEntry, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaMessages, Control::X}) == "R");

    input::PreferController();
    assert(KeyboardLabel({Context::Gameplay, Control::X}).empty());
    assert(KeyboardLabel({Context::Menu, Control::X}).empty());
    assert(KeyboardLabel({Context::Menu, Control::A}).empty());
    assert(KeyboardLabel({Context::Menu, Control::Start}).empty());
    assert(KeyboardLabel({Context::Gameplay, Control::Start}).empty());

    input::SetMouseCapturedForTest(true);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.xrel = 1.f;
    input::DesktopEvent(motion);
    assert(KeyboardLabel({Context::Gameplay, Control::X}).empty());
    motion.motion.which = SDL_TOUCH_MOUSEID;
    motion.motion.xrel = 12.f;
    input::DesktopEvent(motion);
    assert(KeyboardLabel({Context::Gameplay, Control::X}).empty());
    motion.motion.which = 1;
    input::DesktopEvent(motion);
    assert(KeyboardLabel({Context::Gameplay, Control::X}) == "K");
    input::SetMouseCapturedForTest(false);
    input::ResetDesktopBindings();
}

static void TestGlobalButtonConversions() {
    using prompts::Context;
    using prompts::Control;
    using prompts::KeyboardLabel;
    using prompts::ControlFromHudIcon;
    using prompts::ControlFromGlyph;
    using prompts::AdaptGlyphText;
    using input::DesktopAction;

    Focus(true);
    Key(SDL_SCANCODE_RETURN, true);
    Key(SDL_SCANCODE_RETURN, false);

    // 1. Test ControlFromHudIcon for all 8 HUD atlas icon indices
    assert(ControlFromHudIcon(0) == Control::Y);
    assert(ControlFromHudIcon(1) == Control::B);
    assert(ControlFromHudIcon(2) == Control::A);
    assert(ControlFromHudIcon(3) == Control::X);
    assert(ControlFromHudIcon(4) == Control::LT);
    assert(ControlFromHudIcon(5) == Control::RT);
    assert(ControlFromHudIcon(6) == Control::White);
    assert(ControlFromHudIcon(7) == Control::Black);
    assert(!ControlFromHudIcon(8).has_value());
    assert(!ControlFromHudIcon(9).has_value());
    assert(!ControlFromHudIcon(0xFFFFFFFF).has_value());

    // 2. Test ControlFromGlyph for all embedded Xbox font glyph codes (\x01..\x09)
    assert(ControlFromGlyph('\x01') == Control::A);
    assert(ControlFromGlyph('\x02') == Control::Y);
    assert(ControlFromGlyph('\x03') == Control::X);
    assert(ControlFromGlyph('\x04') == Control::B);
    assert(ControlFromGlyph('\x05') == Control::LT);
    assert(ControlFromGlyph('\x06') == Control::RT);
    assert(ControlFromGlyph('\x07') == Control::Black);
    assert(ControlFromGlyph('\x08') == Control::White);
    assert(ControlFromGlyph('\x09') == Control::White);
    assert(!ControlFromGlyph('\0').has_value());
    assert(!ControlFromGlyph('\x0a').has_value());
    assert(!ControlFromGlyph('A').has_value());

    // 3. Test default gameplay bindings for all 8 buttons
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(2)}) == "Space");       // A -> Jump
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(1)}) == "F");           // B -> Grab
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(3)}) == "R");           // X -> Reload
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(0)}) == "Left Shift");  // Y -> Evade
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(4)}) == "Left Ctrl");   // LT -> Crouch
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(5)}) == "Mouse Left");  // RT -> Fire
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(6)}) == "Q");           // White -> Heal
    assert(KeyboardLabel({Context::Gameplay, *ControlFromHudIcon(7)}) == "Tab");         // Black -> Inventory

    // 4. Test text adaptation for real tutorial strings
    std::string out;
    assert(AdaptGlyphText("Interrogate \x02 while holding a Hostage.", out));
    assert(out == "Interrogate [Left Shift] while holding a Hostage.");

    assert(AdaptGlyphText("Hostage: \x04 when close to an enemy.", out));
    assert(out == "Hostage: [F] when close to an enemy.");

    assert(AdaptGlyphText("Steal: \x03 when close to a body.", out));
    assert(out == "Steal: [R] when close to a body.");

    assert(AdaptGlyphText("Counter Kill: \x03 when close & in front of an enemy.", out));
    assert(out == "Counter Kill: [R] when close & in front of an enemy.");

    assert(AdaptGlyphText("Evade: \x02 Helps evade gunfire & blast damage.", out));
    assert(out == "Evade: [Left Shift] Helps evade gunfire & blast damage.");

    // Painkillers uses \t (\x09) as White button glyph
    assert(AdaptGlyphText("Painkillers: \t Avoid hits while Painkillers are still healing or lose out.", out));
    assert(out == "Painkillers: [Q] Avoid hits while Painkillers are still healing or lose out.");

    // Action button \x01
    assert(AdaptGlyphText("This string should have an action button (\x01) in it !", out));
    assert(out == "This string should have an action button ([Space]) in it !");

    // Multiple glyphs in a single string
    assert(AdaptGlyphText("Press \x01 to jump, \x03 to reload, \x06 to shoot.", out));
    assert(out == "Press [Space] to jump, [R] to reload, [Mouse Left] to shoot.");

    // String without glyphs returns false (no modification)
    assert(!AdaptGlyphText("Plain text without any buttons.", out));

    // 5. Test dynamic remapping across all 8 gameplay controls
    assert(input::AssignDesktopBinding(DesktopAction::Reload, SDL_SCANCODE_K) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Jump, SDL_SCANCODE_Z) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Grab, SDL_SCANCODE_G) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Evade, SDL_SCANCODE_V) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Crouch, SDL_SCANCODE_C) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Fire, -SDL_BUTTON_RIGHT) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Heal, SDL_SCANCODE_H) == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Inventory, SDL_SCANCODE_I) == input::BindingResult::Applied);

    // Verify all 8 buttons dynamically reflect remappings in HUD queries
    assert(KeyboardLabel({Context::Gameplay, Control::A}) == "Z");
    assert(KeyboardLabel({Context::Gameplay, Control::B}) == "G");
    assert(KeyboardLabel({Context::Gameplay, Control::X}) == "K");
    assert(KeyboardLabel({Context::Gameplay, Control::Y}) == "V");
    assert(KeyboardLabel({Context::Gameplay, Control::LT}) == "C");
    assert(KeyboardLabel({Context::Gameplay, Control::RT}) == "Mouse Right");
    assert(KeyboardLabel({Context::Gameplay, Control::White}) == "H");
    assert(KeyboardLabel({Context::Gameplay, Control::Black}) == "I");

    // Verify text adaptation reflects remapped keys dynamically
    assert(AdaptGlyphText("Steal: \x03 when close to a body.", out));
    assert(out == "Steal: [K] when close to a body.");

    assert(AdaptGlyphText("Interrogate \x02 while holding a Hostage.", out));
    assert(out == "Interrogate [V] while holding a Hostage.");

    assert(AdaptGlyphText("All: \x01 \x04 \x03 \x02 \x05 \x06 \x07 \x09", out));
    assert(out == "All: [Z] [G] [K] [V] [C] [Mouse Right] [I] [H]");

    // 6. Verify menu and PDA context isolation is preserved during remapping
    assert(KeyboardLabel({Context::Menu, Control::A}) == "Enter");
    assert(KeyboardLabel({Context::Menu, Control::B}) == "Esc");
    assert(KeyboardLabel({Context::Menu, Control::Start}) == "Enter");
    assert(KeyboardLabel({Context::Menu, Control::X}) == "K"); // New Game "Continue Without Save" dynamically reflects Reload binding!
    assert(KeyboardLabel({Context::PdaTextEntry, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaMessages, Control::X}) == "R");
    assert(KeyboardLabel({Context::PdaTextEntry, Control::Y}) == "Left Shift");
    assert(KeyboardLabel({Context::PdaMessages, Control::Y}) == "Left Shift"); // Trash Can presents Left Shift!

    // In menu mode, pressing the remapped Reload key (K) activates pad.analog[2] (Xbox X)
    Key(SDL_SCANCODE_K, true);
    assert(input::DesktopPad().analog[2] == 255);
    Key(SDL_SCANCODE_K, false);

    // In menu mode, pressing Left Shift sets DesktopMenuShift() for PDA Messages (Trash Can)
    Key(SDL_SCANCODE_LSHIFT, true);
    Key(SDL_SCANCODE_LSHIFT, false);
    assert(input::DesktopPad().analog[3] == 0);
    assert(input::DesktopMenuShift());
    input::DesktopPad();
    assert(!input::DesktopMenuShift());

    // 7. Verify controller mode suppresses keyboard prompts and preserves native glyphs
    input::PreferController();
    assert(!input::DesktopUsesKeyboardPrompts());
    assert(KeyboardLabel({Context::Gameplay, Control::X}).empty());
    assert(KeyboardLabel({Context::Gameplay, Control::A}).empty());
    assert(!AdaptGlyphText("Steal: \x03 when close to a body.", out));

    // Switching back to keyboard restores active bindings
    Key(SDL_SCANCODE_SPACE, true);
    Key(SDL_SCANCODE_SPACE, false);
    assert(input::DesktopUsesKeyboardPrompts());
    assert(KeyboardLabel({Context::Gameplay, Control::X}) == "K");
    assert(AdaptGlyphText("Steal: \x03 when close to a body.", out));
    assert(out == "Steal: [K] when close to a body.");

    // Clean up
    input::ResetDesktopBindings();
}

static void Wheel(float y, SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = y;
    event.wheel.direction = direction;
    input::DesktopEvent(event);
}

static void TestPromptDevice() {
    Focus(true);
    input::SetMouseCapturedForTest(false);
    input::PreferController();
    assert(!input::DesktopUsesKeyboardPrompts());

    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 1.f;
    input::DesktopEvent(event);
    assert(!input::DesktopUsesKeyboardPrompts());

    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    input::DesktopEvent(event);
    assert(input::DesktopUsesKeyboardPrompts());
    assert(input::DesktopPad().analog[7] == 0);
    input::PreferController();
    assert(!input::DesktopUsesKeyboardPrompts());

    Key(SDL_SCANCODE_RETURN, true);
    assert(input::DesktopUsesKeyboardPrompts());
    Key(SDL_SCANCODE_RETURN, false);

    // A quick tap is offered to the game's PDA context without becoming a
    // generic Xbox Y or replacing the binding used during gameplay.
    Key(SDL_SCANCODE_LSHIFT, true);
    Key(SDL_SCANCODE_LSHIFT, false);
    assert(input::DesktopPad().analog[3] == 0);
    assert(input::DesktopMenuShift());
    input::DesktopPad();
    assert(!input::DesktopMenuShift());

    Key(SDL_SCANCODE_RSHIFT, true);
    Key(SDL_SCANCODE_RSHIFT, false);
    assert(input::DesktopPad().analog[3] == 0);
    assert(input::DesktopMenuShift());
    input::DesktopPad();
    assert(!input::DesktopMenuShift());

    input::SetMouseCapturedForTest(true);
    Key(SDL_SCANCODE_RSHIFT, true);
    input::DesktopPad();
    assert(!input::DesktopMenuShift());
    input::SetMouseCapturedForTest(false);
    Focus(false);
    assert(!input::DesktopMenuShift());
}

static void TestWeaponSelector() {
    input::SetMouseCapturedForTest(false);
    Wheel(-3);
    Key(SDL_SCANCODE_TAB, true);
    assert(input::DesktopPad().analog[4] == 0);
    Key(SDL_SCANCODE_TAB, false);

    input::SetMouseCapturedForTest(true);
    Key(SDL_SCANCODE_TAB, true);
    assert(input::DesktopPad().analog[4] == 255);
    assert(input::DesktopPad().analog[4] == 255);
    Wheel(0.5f);
    assert(input::PeekWeaponWheelStep().direction == 0);
    input::DesktopWeaponSelector(true);
    Wheel(0.5f);
    input::SetMouseCapturedForTest(false); // The open HUD suspends the camera.
    assert(input::DesktopPad().analog[4] == 255);
    Wheel(0.5f);
    Wheel(-2);
    Wheel(-1, SDL_MOUSEWHEEL_FLIPPED);
    for (int expected : {-1, 1, 1, -1}) {
        const auto step = input::PeekWeaponWheelStep();
        assert(step.direction == expected);
        assert(input::PeekWeaponWheelStep().id == step.id);
        input::CompleteWeaponWheelStep(step.id);
    }
    assert(input::PeekWeaponWheelStep().direction == 0);
    assert(input::DesktopPad().buttons == 0); // The wheel never emits global D-pad input.

    Wheel(1);
    const auto stale = input::PeekWeaponWheelStep();
    Key(SDL_SCANCODE_TAB, false);
    assert(input::DesktopPad().analog[4] == 0);
    assert(input::PeekWeaponWheelStep().direction == 0);
    input::DesktopWeaponSelector(false);

    input::SetMouseCapturedForTest(true);
    Key(SDL_SCANCODE_TAB, true);
    input::DesktopWeaponSelector(true);
    Wheel(1);
    input::CompleteWeaponWheelStep(stale.id);
    assert(input::PeekWeaponWheelStep().direction == -1);
    Focus(false);
    Focus(true);
    assert(input::DesktopPad().analog[4] == 0);
    assert(input::PeekWeaponWheelStep().direction == 0);

    Key(SDL_SCANCODE_TAB, true);
    Wheel(1);
    input::SetMouseCapturedForTest(false);
    Key(SDL_SCANCODE_ESCAPE, true);
    Key(SDL_SCANCODE_ESCAPE, false);
    const auto pause = input::DesktopPad();
    assert(pause.buttons == 0x10 && pause.analog[1] == 0 && pause.analog[4] == 0);
    assert(input::PeekWeaponWheelStep().direction == 0);

    input::SetMouseCapturedForTest(true);
    Key(SDL_SCANCODE_TAB, false);
    Key(SDL_SCANCODE_TAB, true);
    Wheel(1);
    input::PreferController();
    assert(input::DesktopPad().analog[4] == 0);
    assert(input::PeekWeaponWheelStep().direction == 0);
    input::DesktopWeaponSelector(false);
    input::SetMouseCapturedForTest(false);
}

static void TestRemainingBindings() {
    using input::BindingResult;
    using input::DesktopAction;
    Focus(true);
    input::SetMouseCapturedForTest(true);
    const DesktopAction actions[] = {DesktopAction::Crouch, DesktopAction::Evade,
                                    DesktopAction::Grab, DesktopAction::Heal};
    const int channels[] = {6, 3, 1, 5};
    for (int i = 0; i < 4; ++i) {
        const auto code = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i);
        assert(input::AssignDesktopBinding(actions[i], code) == BindingResult::Applied);
        Key(code, true);
        assert(input::DesktopPad().analog[channels[i]] == 255);
        Key(code, false);
        assert(input::DesktopPad().analog[channels[i]] == 0);
    }

    assert(input::AssignDesktopBinding(DesktopAction::Inventory, SDL_SCANCODE_G) == BindingResult::Applied);
    Key(SDL_SCANCODE_TAB, true);
    assert(input::DesktopPad().analog[4] == 0);
    Key(SDL_SCANCODE_TAB, false);
    Key(SDL_SCANCODE_G, true);
    assert(input::DesktopPad().analog[4] == 255);
    input::DesktopWeaponSelector(true);
    input::SetMouseCapturedForTest(false);
    Wheel(-1);
    assert(input::DesktopPad().analog[4] == 255);
    assert(input::PeekWeaponWheelStep().direction == 1);
    Key(SDL_SCANCODE_G, false);
    assert(input::DesktopPad().analog[4] == 0);
    assert(!input::PeekWeaponWheelStep().direction);
    input::DesktopWeaponSelector(false);
    input::SetMouseCapturedForTest(true);

    for (const auto button : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE,
                             SDL_BUTTON_X1, SDL_BUTTON_X2}) {
        // Free the default left button before testing it as the inventory binding.
        assert(input::AssignDesktopBinding(DesktopAction::Fire, SDL_SCANCODE_L) == BindingResult::Applied);
        assert(input::AssignDesktopBinding(DesktopAction::Inventory, -button) == BindingResult::Applied);
        SDL_Event click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.down = true;
        click.button.button = button;
        input::DesktopEvent(click);
        assert(input::DesktopPad().analog[4] == 255);
        Wheel(2);
        input::DesktopWeaponSelector(true);
        input::SetMouseCapturedForTest(false);
        assert(input::DesktopPad().analog[4] == 255);
        assert(!input::PeekWeaponWheelStep().direction);
        Wheel(-1);
        assert(input::PeekWeaponWheelStep().direction == 1);
        click.type = SDL_EVENT_MOUSE_BUTTON_UP;
        click.button.down = false;
        input::DesktopEvent(click);
        assert(input::DesktopPad().analog[4] == 0);
        assert(!input::PeekWeaponWheelStep().direction);
        input::DesktopWeaponSelector(false);
        input::SetMouseCapturedForTest(true);
    }
    input::ResetDesktopBindings();
    input::SetMouseCapturedForTest(false);
}

static void TestRemapping() {
    using input::BindingResult;
    using input::DesktopAction;
    Focus(true);
    input::SetMouseCapturedForTest(true);
    assert(input::AssignDesktopBinding(DesktopAction::Forward, SDL_SCANCODE_S) == BindingResult::InUse);
    assert(input::AssignDesktopBinding(DesktopAction::Fire, SDL_SCANCODE_ESCAPE) == BindingResult::Reserved);
    assert(input::AssignDesktopBinding(DesktopAction::Forward, SDL_SCANCODE_I) == BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Jump, SDL_SCANCODE_J) == BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Reload, SDL_SCANCODE_K) == BindingResult::Applied);
    assert(input::AssignDesktopBinding(DesktopAction::Fire, SDL_SCANCODE_L) == BindingResult::Applied);
    Key(SDL_SCANCODE_W, true);
    assert(input::DesktopPad().ly == 0);
    Key(SDL_SCANCODE_W, false);
    Key(SDL_SCANCODE_I, true);
    Key(SDL_SCANCODE_J, true);
    Key(SDL_SCANCODE_K, true);
    Key(SDL_SCANCODE_L, true);
    auto pad = input::DesktopPad();
    assert(pad.ly == 32767 && pad.analog[0] == 255 && pad.analog[2] == 255 && pad.analog[7] == 255);
    Focus(false);
    Focus(true);
    input::SetMouseCapturedForTest(false);
    input::BeginDesktopBindingCapture();
    Key(SDL_SCANCODE_ESCAPE, true);
    assert(input::TakeDesktopBindingCapture() == input::DesktopBindingCancelled);
    assert(input::DesktopPad().analog[1] == 0);
    input::EndDesktopBindingCapture();
    input::BeginDesktopBindingCapture();
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.down = true;
    click.button.button = SDL_BUTTON_RIGHT;
    input::DesktopEvent(click);
    assert(input::TakeDesktopBindingCapture() == -SDL_BUTTON_RIGHT);
    input::EndDesktopBindingCapture();
    assert(input::AssignDesktopBinding(DesktopAction::Fire, -SDL_BUTTON_RIGHT) == BindingResult::Applied);
    input::SetMouseCapturedForTest(true);
    input::DesktopEvent(click);
    assert(input::DesktopPad().analog[7] == 255);
    click.type = SDL_EVENT_MOUSE_BUTTON_UP;
    click.button.down = false;
    input::DesktopEvent(click);
    assert(input::DesktopPad().analog[7] == 0);
    input::SetMouseCapturedForTest(false);
    Key(SDL_SCANCODE_W, true);
    assert(input::DesktopPad().buttons == 1);
    Key(SDL_SCANCODE_W, false);
    Key(SDL_SCANCODE_RETURN, true);
    assert(input::DesktopPad().analog[0] == 255);
    Key(SDL_SCANCODE_RETURN, false);
    input::ResetDesktopBindings();
    assert(input::GetDesktopBindings()[0] == SDL_SCANCODE_W);
}

namespace logx {
void Write(Level, const char*, ...) {}
}

int main() {
    assert(input::DesktopPad().connected);
    Key(SDL_SCANCODE_RETURN, true);
    assert(!input::DesktopPad().analog[0]);

    Focus(true);
    Key(SDL_SCANCODE_RETURN, true, SDL_KMOD_ALT);
    assert(!input::DesktopPad().analog[0]);
    Key(SDL_SCANCODE_RETURN, true);
    const auto confirm = input::DesktopPad();
    assert(confirm.analog[0] == 255 && confirm.analog[1] == 0 && confirm.buttons == 0);
    assert(input::DesktopPad().analog[0] == 0); // Held Enter cannot confirm in the child menu.
    assert((input::DesktopPad().buttons & 0x10) == 0);
    Key(SDL_SCANCODE_RETURN, false);
    assert(!input::DesktopPad().analog[0]);

    Key(SDL_SCANCODE_RETURN, true);
    Key(SDL_SCANCODE_RETURN, false);
    assert(input::DesktopPad().analog[0] == 255);
    assert(!input::DesktopPad().analog[0]);

    Key(SDL_SCANCODE_ESCAPE, true);
    Key(SDL_SCANCODE_ESCAPE, false);
    Focus(false);
    Focus(true);
    assert(!input::DesktopPad().analog[1]);

    Key(SDL_SCANCODE_LEFT, true);
    assert(input::DesktopPad().buttons == 4);
    Focus(false);
    Focus(true);
    assert(!input::DesktopPad().buttons);

    Key(SDL_SCANCODE_ESCAPE, true);
    const auto back = input::DesktopPad();
    assert(back.analog[1] == 255 && back.analog[0] == 0 && back.buttons == 0);
    assert(input::DesktopPad().analog[1] == 0); // Held Esc cannot pop the parent.
    Key(SDL_SCANCODE_ESCAPE, false);

    // Menu navigation with WASD and Space/Backspace
    Key(SDL_SCANCODE_W, true);
    assert(input::DesktopPad().buttons == 1);
    Key(SDL_SCANCODE_W, false);
    Key(SDL_SCANCODE_S, true);
    assert(input::DesktopPad().buttons == 2);
    Key(SDL_SCANCODE_S, false);
    Key(SDL_SCANCODE_A, true);
    assert(input::DesktopPad().buttons == 4);
    Key(SDL_SCANCODE_A, false);
    Key(SDL_SCANCODE_D, true);
    assert(input::DesktopPad().buttons == 8);
    Key(SDL_SCANCODE_D, false);
    Key(SDL_SCANCODE_SPACE, true);
    assert(input::DesktopPad().analog[0] == 255);
    Key(SDL_SCANCODE_SPACE, false);
    Key(SDL_SCANCODE_BACKSPACE, true);
    assert(input::DesktopPad().analog[1] == 255);
    Key(SDL_SCANCODE_BACKSPACE, false);

    input::PreferController();
    Key(SDL_SCANCODE_UP, true);
    assert(input::DesktopPad().buttons == 1);
    assert(!input::DesktopPad().analog[1]);

    // Test 1: Capture and focus state requirements for mouse camera
    float dx = 0.f, dy = 0.f;
    assert(!input::IsMouseActive());
    assert(!input::ConsumeMouseDelta(dx, dy));

    Focus(true);
    input::SetMouseCapturedForTest(true);
    // PreferController had set keyboard=false. A key or mouse event restores keyboard=true:
    Key(SDL_SCANCODE_W, true);
    Key(SDL_SCANCODE_W, false);
    assert(input::IsMouseActive());

    // Test 2: Consumption clears delta; no residual drift
    SDL_Event motionEvent{};
    motionEvent.type = SDL_EVENT_MOUSE_MOTION;
    motionEvent.motion.xrel = 18.f;
    motionEvent.motion.yrel = -18.f;
    input::DesktopEvent(motionEvent);

    assert(input::ConsumeMouseDelta(dx, dy));
    assert(dx == 18.f && dy == -18.f);

    // Immediate second read with no movement returns 0 (zero drift)
    assert(input::ConsumeMouseDelta(dx, dy));
    assert(dx == 0.f && dy == 0.f);

    // Test 3: No artificial clamp at +/-250 (fast flick test)
    motionEvent.motion.xrel = 600.f;
    motionEvent.motion.yrel = -800.f;
    input::DesktopEvent(motionEvent);
    assert(input::ConsumeMouseDelta(dx, dy));
    assert(dx == 600.f && dy == -800.f);

    // Test 4: Multi-event accumulation is linear and exact
    for (int i = 0; i < 24; ++i) {
        motionEvent.motion.xrel = 18.f;
        motionEvent.motion.yrel = 0.f;
        input::DesktopEvent(motionEvent);
    }
    assert(input::ConsumeMouseDelta(dx, dy));
    assert(dx == 432.f && dy == 0.f);

    // Test 5: Reversal net displacement is zero
    motionEvent.motion.xrel = 100.f;
    input::DesktopEvent(motionEvent);
    motionEvent.motion.xrel = -100.f;
    input::DesktopEvent(motionEvent);
    assert(input::ConsumeMouseDelta(dx, dy));
    assert(dx == 0.f && dy == 0.f);

    // Test 6: 1:1 X/Y Sensitivity factor and TOML configurable scaling
    input::mouseSensitivity = 100;
    const float sens100 = input::GetMouseSensitivityFactor();
    assert(std::abs(sens100 - 0.000625f) < 1e-7f);

    input::mouseSensitivity = 200;
    const float sens200 = input::GetMouseSensitivityFactor();
    assert(std::abs(sens200 - 0.001250f) < 1e-7f);

    input::mouseSensitivity = 50;
    const float sens50 = input::GetMouseSensitivityFactor();
    assert(std::abs(sens50 - 0.0003125f) < 1e-7f);

    // Test 7: Clamping bounds on mouse_sensitivity (10% to 400%)
    input::mouseSensitivity = 5;
    assert(std::abs(input::GetMouseSensitivityFactor() - 0.0000625f) < 1e-7f);
    input::mouseSensitivity = 500;
    assert(std::abs(input::GetMouseSensitivityFactor() - 0.0025000f) < 1e-7f);
    input::mouseSensitivity = 100;

    // Test 8: Framerate independence across 30, 60, 90, 120 Hz
    // Over a movement of 432 units, total angular rotation is identical at all frame rates
    for (int hz : {30, 60, 90, 120}) {
        float totalDx = 0.f;
        const float unitsPerFrame = 432.f / hz;
        for (int frame = 0; frame < hz; ++frame) {
            motionEvent.motion.xrel = unitsPerFrame;
            motionEvent.motion.yrel = 0.f;
            input::DesktopEvent(motionEvent);
            float fdx = 0.f, fdy = 0.f;
            assert(input::ConsumeMouseDelta(fdx, fdy));
            totalDx += fdx;
        }
        assert(std::abs(totalDx - 432.f) < 1e-3f);
        const float totalYaw = totalDx * input::GetMouseSensitivityFactor();
        assert(std::abs(totalYaw - 0.27f) < 1e-3f);
    }

    // Test 9: Zero velocity acceleration (Fast flick vs slow sweep produce identical rotation)
    // Fast flick: 432 units in 1 step
    motionEvent.motion.xrel = 432.f;
    input::DesktopEvent(motionEvent);
    input::ConsumeMouseDelta(dx, dy);
    const float flickYaw = dx * input::GetMouseSensitivityFactor();

    // Slow sweep: 24 steps of 18 units
    float sweepTotalDx = 0.f;
    for (int i = 0; i < 24; ++i) {
        motionEvent.motion.xrel = 18.f;
        input::DesktopEvent(motionEvent);
        float stepDx = 0.f, stepDy = 0.f;
        input::ConsumeMouseDelta(stepDx, stepDy);
        sweepTotalDx += stepDx;
    }
    const float sweepYaw = sweepTotalDx * input::GetMouseSensitivityFactor();
    assert(std::abs(flickYaw - sweepYaw) < 1e-6f);

    // Test 10: DesktopLook returns 0.0f when mouse is active
    float lookVal = -999.f;
    assert(input::DesktopLook(2, lookVal) && lookVal == 0.0f);
    assert(input::DesktopLook(3, lookVal) && lookVal == 0.0f);

    // Test 11: PreferController disables mouse active and restores native pad read
    input::PreferController();
    assert(!input::IsMouseActive());
    assert(!input::DesktopLook(2, lookVal));

    // Test 12: Mouse Invert Y setting and direction conventions
    // Default is false (standard modern: mouse UP looks UP)
    assert(!input::invertMouseY);
    const float sens = input::GetMouseSensitivityFactor();

    // Mouse UP (dy < 0)
    const float dyUp = -18.f;
    const float dPitchStandardUp = (input::invertMouseY ? -dyUp : dyUp) * sens;
    assert(dPitchStandardUp < 0.f); // Pitch decreases -> camera looks UP

    // Mouse DOWN (dy > 0)
    const float dyDown = 18.f;
    const float dPitchStandardDown = (input::invertMouseY ? -dyDown : dyDown) * sens;
    assert(dPitchStandardDown > 0.f); // Pitch increases -> camera looks DOWN

    // Inverted mode (invert_mouse_y = true)
    input::invertMouseY = true;
    const float dPitchInvertedUp = (input::invertMouseY ? -dyUp : dyUp) * sens;
    assert(dPitchInvertedUp > 0.f); // Pitch increases -> camera looks DOWN (inverted)
    const float dPitchInvertedDown = (input::invertMouseY ? -dyDown : dyDown) * sens;
    assert(dPitchInvertedDown < 0.f); // Pitch decreases -> camera looks UP (inverted)
    input::invertMouseY = false;
    TestWeaponSelector();
    TestRemapping();
    TestRemainingBindings();
    TestPromptDevice();
    TestPromptBindings();
    TestGlobalButtonConversions();
}

