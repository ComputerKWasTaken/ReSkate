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
// Scrolling at full tilt: design pixels a second for the focused panel, and
// mouse wheel notches a second (ImGui scrolls five lines a notch) under a pointer.
constexpr float scroll_speed = 1200.0f;
constexpr float wheel_speed = 16.0f;
// A mouse move this close to the last one is the same place reported again.
constexpr float pointer_slack = 2.0f;

float axis(std::int16_t value) { return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f); }

// The left stick scrolls whatever the user is looking at: the panel holding
// the pad's focus while the focus is shown, else the one under the pointer.
// ImGui's own stick scrolling only ever moves the focused window, which under a
// pointer is usually the page itself, with nothing to scroll.
void scroll(ImGuiIO& io, float tilt, float scale) {
    const float amount = std::copysign(std::clamp((std::abs(tilt) - left_dead_zone) / (1 - left_dead_zone), 0.0f, 1.0f), tilt);
    if (amount == 0.0f) return;
    const ImGuiContext& g = *GImGui;
    if (g.NavCursorVisible && g.NavHighlightItemUnderNav) {
        // The nearest panel around the focus that can scroll.
        ImGuiWindow* window = g.NavWindow;
        while (window && window->ScrollMax.y <= 0 && (window->Flags & ImGuiWindowFlags_ChildWindow)) window = window->ParentWindow;
        if (window && window->ScrollMax.y > 0)
            ImGui::SetScrollY(window, window->Scroll.y - amount * scroll_speed * scale * io.DeltaTime);
        return;
    }
    io.AddMouseWheelEvent(0, amount * wheel_speed * io.DeltaTime);
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

bool moved_away(ImVec2 position, ImVec2 before) {
    return std::abs(position.x - before.x) > pointer_slack || std::abs(position.y - before.y) > pointer_slack;
}

} // namespace

void PadFeed::update(ImGuiIO& io, const PadState& input, float scale) {
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
    scroll(io, axis(pad.left_y), scale);
    buttons_ = pad.buttons;

    // Moving the focus hands the screen back to it: ImGui hides the focus while
    // a pointer moves, so the pointer goes away instead. Scrolling keeps both.
    if (pressed & focus_bits) pointing_ = false;

    const float x = axis(pad.right_x), y = -axis(pad.right_y);   // screen Y grows downward
    const float tilt = std::min(1.0f, std::sqrt(x * x + y * y));
    // The first R3 only shows the pointer: a stray click must not press
    // whatever the mouse last rested on.
    const bool click = pointing_ && (pressed & right_thumb);
    if (!pointing_ && (tilt > right_dead_zone || (pressed & right_thumb))) {
        pointing_ = true;
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
    }
    if (pointing_) {
        pointer_.x = std::clamp(pointer_.x, 0.0f, std::max(0.0f, io.DisplaySize.x - 1));
        pointer_.y = std::clamp(pointer_.y, 0.0f, std::max(0.0f, io.DisplaySize.y - 1));
        // Every frame, after the backend's own report of the system cursor,
        // which stays where the mouse left it.
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(pointer_.x, pointer_.y);
    }
    if (click || (clicking_ && (changed & right_thumb))) {
        clicking_ = click;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, click);
    }
    io.MouseDrawCursor = pointing_;
}

void PadFeed::mouse_moved(ImVec2 position) {
    // Windows repeats a mouse move where the mouse already was when a window
    // changes under it; only a move to a new place is the mouse or a finger.
    if (pointing_ && moved_away(position, mouse_)) pointing_ = false;
    mouse_ = position;
}

} // namespace dingosdk::launcher_gui
