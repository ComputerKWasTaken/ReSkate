#include "gamepad_input.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace dingosdk::launcher_gui {
namespace {

// XINPUT_GAMEPAD_* bits, spelled out so this file needs no Windows headers.
constexpr std::uint16_t dpad_up = 0x0001, dpad_down = 0x0002, dpad_left = 0x0004, dpad_right = 0x0008;
constexpr std::uint16_t right_thumb = 0x0080, button_a = 0x1000, button_b = 0x2000;

struct Button { std::uint16_t bit; ImGuiKey key; };
// Only what moves and presses the focus. X/Y and the shoulders stay out: ImGui
// reads them as its window switcher and tweak modifiers, which the launcher has
// no use for.
constexpr Button focus_buttons[]{
    {dpad_up, ImGuiKey_GamepadDpadUp}, {dpad_down, ImGuiKey_GamepadDpadDown},
    {dpad_left, ImGuiKey_GamepadDpadLeft}, {dpad_right, ImGuiKey_GamepadDpadRight},
    {button_a, ImGuiKey_GamepadFaceDown}};
constexpr std::uint16_t focus_bits = dpad_up | dpad_down | dpad_left | dpad_right | button_a;

// XInput's left stick dead zone.
constexpr float left_dead_zone = 7849.0f / 32767.0f;
// Larger on the right: a thumb resting on the Deck's trackpad is never quite
// centred, and the pointer must not creep under it.
constexpr float right_dead_zone = 0.2f;
// Pointer speed at full tilt, in design pixels a second.
constexpr float pointer_speed = 1400.0f;
// A mouse move this close to the pad's pointer is the echo of moving the
// system cursor there; one further away is the mouse or a finger.
constexpr float pointer_slack = 2.0f;

float axis(std::int16_t value) { return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f); }

void stick(ImGuiIO& io, ImGuiKey negative, ImGuiKey positive, float value) {
    const auto amount = [](float tilt) { return std::clamp((tilt - left_dead_zone) / (1 - left_dead_zone), 0.0f, 1.0f); };
    io.AddKeyAnalogEvent(negative, -value > left_dead_zone, amount(-value));
    io.AddKeyAnalogEvent(positive, value > left_dead_zone, amount(value));
}

// B goes to ImGui's own cancel while ImGui has something to cancel: a combo or
// menu to close, or a text field to leave. Otherwise it is Escape, the
// launcher's way back out of a page or a modal.
bool imgui_cancels() {
    const ImGuiContext& g = *GImGui;
    if (ImGui::IsAnyItemActive()) return true;
    if (g.OpenPopupStack.Size == 0) return false;
    const ImGuiWindow* top = g.OpenPopupStack.back().Window;
    return top && !(top->Flags & ImGuiWindowFlags_Modal);
}

bool moved_away(ImVec2 position, ImVec2 pointer) {
    return std::abs(position.x - pointer.x) > pointer_slack || std::abs(position.y - pointer.y) > pointer_slack;
}

} // namespace

std::optional<ImVec2> PadFeed::update(ImGuiIO& io, const PadState& input, float scale) {
    // A pad that went away lets go of everything it held.
    const PadState pad = input.connected ? input : PadState{};
    const auto changed = static_cast<std::uint16_t>(pad.buttons ^ buttons_);
    const auto pressed = static_cast<std::uint16_t>(pad.buttons & changed);
    if (pad.connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;

    // Only changes are sent: the keyboard shares Escape and the mouse shares the
    // left button, and a pad repeating "up" every frame would cancel them.
    for (const auto& button : focus_buttons)
        if (changed & button.bit) io.AddKeyEvent(button.key, (pad.buttons & button.bit) != 0);
    if (changed & button_b) {
        const bool down = (pad.buttons & button_b) != 0;
        // The release goes to whichever key the press went to.
        if (down) back_key_ = imgui_cancels() ? ImGuiKey_GamepadFaceRight : ImGuiKey_Escape;
        io.AddKeyEvent(back_key_, down);
    }
    const float left_x = axis(pad.left_x), left_y = axis(pad.left_y);
    if (pad.connected || connected_) {
        // Analog values ImGui already has are dropped by ImGui itself.
        stick(io, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, left_x);
        stick(io, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, left_y);
    }
    connected_ = pad.connected;
    buttons_ = pad.buttons;

    // Moving the focus hands the screen back to it: ImGui hides the focus while
    // a pointer moves, so the pointer goes away instead.
    if ((pressed & focus_bits) || std::abs(left_x) > left_dead_zone || std::abs(left_y) > left_dead_zone)
        pointing_ = false;

    bool warp = false;
    const float x = axis(pad.right_x), y = -axis(pad.right_y);   // screen Y grows downward
    const float tilt = std::min(1.0f, std::sqrt(x * x + y * y));
    if (!pointing_ && (tilt > right_dead_zone || (pressed & right_thumb))) {
        pointing_ = true;
        warp = true;
        // From wherever the mouse left it, which may be outside the window.
        pointer_ = ImGui::IsMousePosValid(&io.MousePos) ? io.MousePos
                                                        : ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    }
    if (pointing_ && tilt > right_dead_zone) {
        // Squared, so a small tilt places the pointer precisely and a full one
        // crosses the window in about a second.
        const float speed = (tilt - right_dead_zone) / (1 - right_dead_zone);
        const float step = speed * speed * pointer_speed * scale * io.DeltaTime / tilt;
        pointer_.x += x * step;
        pointer_.y += y * step;
        warp = true;
    }
    if (pointing_) {
        pointer_.x = std::clamp(pointer_.x, 0.0f, std::max(0.0f, io.DisplaySize.x - 1));
        pointer_.y = std::clamp(pointer_.y, 0.0f, std::max(0.0f, io.DisplaySize.y - 1));
        // Every frame, after the backend's own: it may report a system cursor
        // that Proton or gamescope did not move along with ours.
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(pointer_.x, pointer_.y);
    }
    // The release is sent even when the mouse took the pointer meanwhile: the
    // press was ours.
    if (changed & right_thumb) {
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, (pad.buttons & right_thumb) != 0);
    }
    io.MouseDrawCursor = pointing_;
    if (warp) return pointer_;
    return std::nullopt;
}

void PadFeed::mouse_moved(ImVec2 position) {
    // Our own cursor move comes back as a mouse move too; only one that lands
    // somewhere else is the mouse or a finger.
    if (pointing_ && moved_away(position, pointer_)) pointing_ = false;
}

} // namespace dingosdk::launcher_gui
