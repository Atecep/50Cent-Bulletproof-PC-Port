#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "prompt_bindings.h"

#include "core/desktop_input.h"

namespace prompts {

std::string KeyboardLabel(Cue cue) {
    if (!input::DesktopUsesKeyboardPrompts()) return {};

    using enum Control;
    using input::DesktopAction;
    switch (cue.context) {
    case Context::Menu:
    case Context::PdaTextEntry:
    case Context::PdaMessages:
    case Context::PdaMusicList:
        switch (cue.control) {
        case A: return "Enter";
        case B: return "Esc";
        case Start: return "Enter";
        case Adjust: return "Left/Right";
        case X:
            if (cue.context == Context::PdaTextEntry || cue.context == Context::PdaMessages) return "R";
            if (cue.context == Context::Menu) return input::DesktopBindingName(DesktopAction::Reload);
            return {};
        case Y:
            return cue.context != Context::Menu ? "Left Shift" : "";
        default: return {};
        }
    case Context::Gameplay:
        switch (cue.control) {
        case A: return input::DesktopBindingName(DesktopAction::Jump);
        case B: return input::DesktopBindingName(DesktopAction::Grab);
        case X: return input::DesktopBindingName(DesktopAction::Reload);
        case Y: return input::DesktopBindingName(DesktopAction::Evade);
        case LT: return input::DesktopBindingName(DesktopAction::Crouch);
        case RT: return input::DesktopBindingName(DesktopAction::Fire);
        case Black: return input::DesktopBindingName(DesktopAction::Inventory);
        case White: return input::DesktopBindingName(DesktopAction::Heal);
        case Start: return "Esc";
        case MoveForward: return input::DesktopBindingName(DesktopAction::Forward);
        case MoveBackward: return input::DesktopBindingName(DesktopAction::Backward);
        case MoveLeft: return input::DesktopBindingName(DesktopAction::Left);
        case MoveRight: return input::DesktopBindingName(DesktopAction::Right);
        case Look: return "Mouse";
        default: return {};
        }
    }
    return {};
}

std::optional<Control> ControlFromHudIcon(uint32_t icon) {
    switch (icon) {
    case 0: return Control::Y;
    case 1: return Control::B;
    case 2: return Control::A;
    case 3: return Control::X;
    case 4: return Control::LT;
    case 5: return Control::RT;
    case 6: return Control::White;
    case 7: return Control::Black;
    default: return std::nullopt;
    }
}

std::optional<Control> ControlFromGlyph(char glyph) {
    switch (static_cast<uint8_t>(glyph)) {
    case 1: return Control::A;
    case 2: return Control::Y;
    case 3: return Control::X;
    case 4: return Control::B;
    case 5: return Control::LT;
    case 6: return Control::RT;
    case 7: return Control::Black;
    case 8: return Control::White;
    case 9: return Control::White;
    default: return std::nullopt;
    }
}

bool AdaptGlyphText(std::string_view source, std::string& out) {
    if (!input::DesktopUsesKeyboardPrompts()) return false;

    bool modified = false;
    out.clear();
    out.reserve(source.size() + 16);

    for (char c : source) {
        if (auto control = ControlFromGlyph(c)) {
            auto binding = KeyboardLabel({Context::Gameplay, *control});
            if (!binding.empty()) {
                out += "[" + binding + "]";
                modified = true;
                continue;
            }
        }
        out += c;
    }

    return modified;
}

}
#endif
