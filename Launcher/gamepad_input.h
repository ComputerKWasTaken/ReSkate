#pragma once

#include <imgui.h>

#include <cstdint>

// Controller input for the launcher window. Kept free of Windows headers so
// the mapping can be tested anywhere; gui_gamepad.cpp reads the pads.
namespace dingosdk::launcher_gui {

// One frame of a controller in XInput's XINPUT_GAMEPAD layout: an Xbox pad,
// Steam Input's virtual pad (Steam Deck), or a DualShock 4 / DualSense read
// over HID. Several pads are merged into one.
struct PadState {
    bool connected{};
    std::uint16_t buttons{};   // XINPUT_GAMEPAD_* bits
    std::uint8_t left_trigger{}, right_trigger{};
    std::int16_t left_x{}, left_y{}, right_x{}, right_y{};
};

// Turns a pad into the launcher's ImGui input:
// - D-pad and left stick move the focus, A (Cross) presses the focused item;
// - B (Circle) closes an open combo or menu and leaves a text field, and is
//   Escape otherwise, which the launcher's pages and modals already go back on;
// - the right stick moves a pointer and R3 clicks with it. Under Steam Input's
//   gamepad layouts the Steam Deck's right trackpad is the right stick and its
//   click is R3, so this is what makes the trackpad a mouse in the launcher.
class PadFeed {
public:
    // Call once a frame after the backends' NewFrame and before
    // ImGui::NewFrame(), which takes the events in.
    void update(ImGuiIO& io, const PadState& pad, float scale);
    // A mouse move or a touch the window received (WM_MOUSEMOVE, client area
    // pixels). A move to a new place takes the pointer back from the pad.
    void mouse_moved(ImVec2 position);
    // The right stick owns the pointer. ImGui draws it and the system cursor
    // stays put: under Proton a moved system cursor comes back late as a
    // mouse move, which would look like the mouse taking over.
    bool pointing() const { return pointing_; }

private:
    std::uint16_t buttons_{};
    ImGuiKey back_key_{ImGuiKey_Escape};
    bool connected_{};
    bool pointing_{};
    bool clicking_{};   // R3 holds the left button
    ImVec2 pointer_{};
    ImVec2 mouse_{-1, -1};   // where the last mouse move was
};

} // namespace dingosdk::launcher_gui
