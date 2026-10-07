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
// Scrolling at full tilt, in design pixels a second.
constexpr float scroll_speed = 1200.0f;

// How the right stick drives the pointer.
struct PointerFeel {
    float dead_zone;
    float speed;      // design pixels a second at full tilt
    bool squared;     // a small tilt is slow, for placing the pointer precisely
    bool cut_glide;   // stop once the tilt only fades
};
// A thumbstick sets a speed. A resting thumb is never quite centred, so the
// dead zone is large.
constexpr PointerFeel stick_feel{0.2f, 1400.0f, true, false};
// The Steam Deck's trackpad, in Steam's gamepad layouts, tilts the stick by how
// fast the thumb swipes and keeps gliding after it lifts, fading out. Small
// swipes must move the pointer, and the glide must not carry it on.
constexpr PointerFeel trackpad_feel{0.06f, 2600.0f, false, true};
// Frames of a steadily fading tilt that make it a glide rather than the thumb.
constexpr int glide_frames = 4;
// A mouse move this close to the last one is the same place reported again.
constexpr float pointer_slack = 2.0f;

float axis(std::int16_t value) { return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f); }

// The left stick scrolls whatever the user is looking at: the panel holding
// the pad's focus while the focus is shown, else the one under the pointer.
// ImGui's own stick scrolling only ever moves the focused window, which under a
// pointer is usually the page itself, with nothing to scroll. It scrolls the
// panel directly: wheel events would queue behind the pointer's moves and lag.
void scroll(const ImGuiIO& io, float tilt, float scale) {
    const float amount = std::copysign(std::clamp((std::abs(tilt) - left_dead_zone) / (1 - left_dead_zone), 0.0f, 1.0f), tilt);
    if (amount == 0.0f) return;
    const ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.NavCursorVisible && g.NavHighlightItemUnderNav ? g.NavWindow : g.HoveredWindow;
    // The nearest panel around it that can scroll.
    while (window && window->ScrollMax.y <= 0 && (window->Flags & ImGuiWindowFlags_ChildWindow)) window = window->ParentWindow;
    if (window && window->ScrollMax.y > 0)
        ImGui::SetScrollY(window, window->Scroll.y - amount * scroll_speed * scale * io.DeltaTime);
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

    const PointerFeel& feel = trackpad_ ? trackpad_feel : stick_feel;
    const float x = axis(pad.right_x), y = -axis(pad.right_y);   // screen Y grows downward
    const float tilt = std::min(1.0f, std::sqrt(x * x + y * y));
    if (feel.cut_glide) {
        // A glide only ever fades; the thumb speeds up again or stops.
        if (tilt <= feel.dead_zone || tilt > last_tilt_ + 0.01f) fading_ = 0;
        else if (tilt < last_tilt_) ++fading_;
        last_tilt_ = tilt;
    }
    const bool steering = tilt > feel.dead_zone && fading_ < glide_frames;
    // The first R3 only shows the pointer: a stray click must not press
    // whatever the mouse last rested on.
    const bool click = pointing_ && (pressed & right_thumb);
    if (!pointing_ && (steering || (pressed & right_thumb))) {
        pointing_ = true;
        // From wherever the mouse left it, which may be outside the window.
        pointer_ = ImGui::IsMousePosValid(&io.MousePos) ? io.MousePos
                                                        : ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    }
    if (pointing_ && steering) {
        const float speed = (tilt - feel.dead_zone) / (1 - feel.dead_zone);
        const float step = (feel.squared ? speed * speed : speed) * feel.speed * scale * io.DeltaTime / tilt;
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

void PadFeed::after_new_frame() {
    ImGuiContext& g = *GImGui;
    // ImGui looks to the side only along a thin band through the middle of the
    // focus, so it misses an item level with the gap between two list rows: the
    // rail's tiles beside a list. A side move that found nothing looks again
    // across the focus's full height.
    if (side_dir_ != ImGuiDir_None && g.NavId == side_from_ && g.NavWindow && !g.NavMoveSubmitted) {
        const ImGuiDir dir = side_dir_;
        side_dir_ = ImGuiDir_None;
        ImGui::NavMoveRequestSubmit(dir, dir, ImGuiNavMoveFlags_None,
            ImGuiScrollFlags_KeepVisibleEdgeX | ImGuiScrollFlags_KeepVisibleEdgeY);
        g.NavScoringRect = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
        last_nav_id_ = g.NavId;
        return;
    }
    side_dir_ = ImGuiDir_None;
    if (g.NavMoveSubmitted && (g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right)) {
        side_dir_ = g.NavMoveDir;
        // ImGui clears the focus for the move when the item is wider than its
        // panel; the move then lands on that same item, which is no move either.
        side_from_ = g.NavId ? g.NavId : last_nav_id_;
    }
    last_nav_id_ = g.NavId;
}

namespace {
bool g_row_focused{};
} // namespace

void default_focus() {
    ImGui::SetItemDefaultFocus();
    ImGuiContext& g = *GImGui;
    // With a pad there, the focus starts on it straight away as the window
    // appears: when the launcher starts, and when the pad opened this page.
    // (Not whenever nothing has the focus: ImGui clears it for a frame while
    // the focus moves off a row wider than its panel.)
    const bool pad = (g.IO.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
    const bool pad_led = g.NavCursorVisible && g.NavHighlightItemUnderNav;
    if (!pad || !ImGui::IsWindowAppearing() || !(pad_led || g.FrameCount <= 2)) return;
    ImGui::SetFocusID(g.LastItemData.ID, g.CurrentWindow);
    g.NavCursorVisible = true;
    g.NavHighlightItemUnderNav = true;
}

void begin_row() {
    const ImGuiID scope = ImGui::GetID("##row_focus");
    g_row_focused = GImGui->NavFocusScopeId == scope;
    ImGui::PushFocusScope(scope);
}

void row_buttons() {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !g_row_focused);
}

void end_row() {
    ImGui::PopItemFlag();
    ImGui::PopFocusScope();
}

void PadFeed::mouse_moved(ImVec2 position) {
    // Windows repeats a mouse move where the mouse already was when a window
    // changes under it; only a move to a new place is the mouse or a finger.
    if (pointing_ && moved_away(position, mouse_)) pointing_ = false;
    mouse_ = position;
}

} // namespace dingosdk::launcher_gui
