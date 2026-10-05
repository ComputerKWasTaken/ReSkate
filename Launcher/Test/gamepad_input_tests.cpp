// Controller input in the launcher: focus, back, the trackpad pointer and its click,
// checked through real ImGui frames.
#include "Launcher/gamepad_input.h"

#include <imgui.h>

#include <iostream>
#include <optional>
#include <string>

using dingosdk::launcher_gui::PadFeed;
using dingosdk::launcher_gui::PadState;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

constexpr std::uint16_t dpad_down = 0x0002, right_thumb = 0x0080, button_a = 0x1000, button_b = 0x2000;

struct Seen {
    std::string pressed;   // the button that reported a press this frame
    bool escape{};
    bool menu_open{};
};

// One launcher frame: the pad goes in before NewFrame, as gui.cpp does it.
// Two buttons stacked at the top left, and a menu popup opened on request.
Seen frame(PadFeed& feed, const PadState& pad, bool open_menu = false, std::optional<ImVec2>* warp = nullptr) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    const auto moved = feed.update(io, pad, 1.0f);
    if (warp) *warp = moved;
    ImGui::NewFrame();
    Seen seen;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::SetCursorScreenPos(ImVec2(100, 100));
    if (ImGui::InvisibleButton("first", ImVec2(200, 50), ImGuiButtonFlags_EnableNav)) seen.pressed = "first";
    ImGui::SetCursorScreenPos(ImVec2(100, 200));
    if (ImGui::InvisibleButton("second", ImVec2(200, 50), ImGuiButtonFlags_EnableNav)) seen.pressed = "second";
    if (open_menu) ImGui::OpenPopup("##menu");
    if (ImGui::BeginPopup("##menu")) {
        seen.menu_open = true;
        ImGui::MenuItem("Details");
        ImGui::EndPopup();
    }
    seen.escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::End();
    ImGui::Render();
    return seen;
}

PadState pad(std::uint16_t buttons = 0, std::int16_t right_x = 0, std::int16_t right_y = 0) {
    PadState state;
    state.connected = true;
    state.buttons = buttons;
    state.right_x = right_x;
    state.right_y = right_y;
    return state;
}

// Presses and lets go, returning what the frames saw.
std::string press(PadFeed& feed, std::uint16_t button) {
    std::string pressed = frame(feed, pad(button)).pressed;
    const auto released = frame(feed, pad()).pressed;
    return pressed.empty() ? released : pressed;
}

void fresh() {
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
}

} // namespace

int main() {
    // ------------------------------------------------ focus
    {
        fresh();
        PadFeed feed;
        frame(feed, pad());
        frame(feed, pad());
        press(feed, dpad_down);
        check(ImGui::GetIO().NavVisible, "The D-pad shows the focus");
        // The first move lands on the first item; the next one goes down.
        press(feed, dpad_down);
        check(press(feed, button_a) == "second", "A presses the item the D-pad moved to");
        check(!feed.pointing() && !ImGui::GetIO().MouseDrawCursor, "No pointer is drawn while the focus is used");
    }
    {
        fresh();
        PadFeed feed;
        frame(feed, pad());
        frame(feed, pad(button_a));
        check(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown), "A (Cross) is the gamepad's activate button");
        PadState gone;   // unplugged with A held
        frame(feed, gone);
        frame(feed, gone);
        check(!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown), "An unplugged pad lets go of A");
        check(!(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad), "An unplugged pad is no gamepad");
    }

    // ------------------------------------------------ back
    {
        fresh();
        PadFeed feed;
        frame(feed, pad());
        frame(feed, pad());
        frame(feed, pad(button_b));
        check(ImGui::IsKeyPressed(ImGuiKey_Escape, false), "B is Escape with nothing open");
        frame(feed, pad());
        check(!ImGui::IsKeyDown(ImGuiKey_Escape), "Letting go of B lets go of Escape");
    }
    {
        fresh();
        PadFeed feed;
        frame(feed, pad());
        check(frame(feed, pad(), true).menu_open, "The menu opens");
        frame(feed, pad());
        const auto with_b = frame(feed, pad(button_b));
        const auto after = frame(feed, pad());
        check(!after.menu_open, "B closes an open menu");
        check(!with_b.escape && !after.escape, "Closing the menu is not also Escape, which would leave the page");
    }
    {
        fresh();
        PadFeed feed;
        frame(feed, pad());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);   // the keyboard's Escape, held
        frame(feed, pad());
        frame(feed, pad());
        check(ImGui::IsKeyDown(ImGuiKey_Escape), "An idle pad does not let go of the keyboard's Escape");
    }

    // ------------------------------------------------ trackpad pointer
    {
        fresh();
        PadFeed feed;
        ImGui::GetIO().AddMousePosEvent(400, 300);
        frame(feed, pad());
        std::optional<ImVec2> warp;
        frame(feed, pad(0, 3000, 0), false, &warp);
        check(!warp && !feed.pointing(), "A thumb resting near the middle does not move the pointer");

        frame(feed, pad(0, 16000, 0), false, &warp);
        check(feed.pointing() && ImGui::GetIO().MouseDrawCursor, "Tilting takes the pointer and draws it");
        check(warp && warp->x > 400 && warp->y == 300, "Tilting right moves the pointer right, from the mouse's place");
        for (int i = 0; i < 120; ++i) frame(feed, pad(0, 32767, 32767), false, &warp);
        check(warp && warp->x == 799 && warp->y == 0, "The pointer stays inside the window");
        frame(feed, pad());
        check(ImGui::GetIO().MousePos.x == 799 && ImGui::GetIO().MousePos.y == 0, "ImGui's mouse is where the pad left it");

        // The system cursor reporting a stale place (Proton, gamescope) does not take the pointer back.
        ImGui::GetIO().AddMousePosEvent(10, 10);
        frame(feed, pad(), false, &warp);
        frame(feed, pad());
        check(!warp && ImGui::GetIO().MousePos.x == 799, "Without tilt the pointer stays put");

        feed.mouse_moved(ImVec2(799, 0));   // the echo of our own cursor move
        check(feed.pointing(), "The echo of our own cursor move keeps the pointer");
        feed.mouse_moved(ImVec2(50, 60));   // the mouse, or a finger on the screen
        ImGui::GetIO().AddMousePosEvent(50, 60);
        frame(feed, pad());
        frame(feed, pad());
        check(!feed.pointing() && !ImGui::GetIO().MouseDrawCursor, "The mouse takes the pointer back");
        check(ImGui::GetIO().MousePos.x == 50, "The mouse's place wins once it moved");
    }
    {
        fresh();
        PadFeed feed;
        ImGui::GetIO().AddMousePosEvent(150, 225);   // over "second"
        frame(feed, pad());
        frame(feed, pad());
        std::string pressed = frame(feed, pad(right_thumb)).pressed;
        check(ImGui::GetIO().MouseDown[0], "The trackpad's click (R3) holds the left button");
        const auto released = frame(feed, pad()).pressed;
        if (pressed.empty()) pressed = released;
        check(pressed == "second", "The trackpad's click presses what the pointer is over");
        check(!ImGui::GetIO().MouseDown[0], "Letting go of R3 lets go of the left button");

        frame(feed, pad(0, 20000, 0));
        check(feed.pointing(), "The pointer is the pad's");
        press(feed, dpad_down);
        check(!feed.pointing() && ImGui::GetIO().NavVisible, "The D-pad hands the screen back to the focus");
    }

    ImGui::DestroyContext();
    if (failures) {
        std::cerr << failures << " gamepad input check(s) failed\n";
        return 1;
    }
    std::cout << "Gamepad input checks passed.\n";
    return 0;
}
