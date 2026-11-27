#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prompts {

// Xbox channels are scoped by the action family supplied by the presenter.
// For example A is Jump in Gameplay and Confirm in Menu.
enum class Context { Gameplay, Menu, PdaTextEntry, PdaMessages, PdaMusicList };
enum class Control {
    A, B, X, Y, LT, RT, Black, White, Start,
    MoveForward, MoveBackward, MoveLeft, MoveRight, Look, Adjust
};

struct Cue {
    Context context;
    Control control;
};

// An empty result asks the caller to use its original text/icon.
std::string KeyboardLabel(Cue cue);

// Global mapping from HUD icon atlas index to gameplay control.
// Atlas UV layout:
// icon 0: Y, icon 1: B, icon 2: A, icon 3: X,
// icon 4: LT, icon 5: RT, icon 6: White, icon 7: Black.
std::optional<Control> ControlFromHudIcon(uint32_t icon);

// Global mapping from Xbox font glyph byte code (\x01..\x09) to control.
// \x01: A, \x02: Y, \x03: X, \x04: B, \x05: LT, \x06: RT,
// \x07: Black, \x08: White, \x09: White.
std::optional<Control> ControlFromGlyph(char glyph);

// Adapts text containing embedded Xbox button glyph codes (\x01..\x09)
// into "[<Key>]". Returns true if modified, false if unchanged or controller active.
bool AdaptGlyphText(std::string_view source, std::string& out);

}
