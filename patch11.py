import sys

with open('Extension/UI/Overlay/skate_menu_skater.cpp', 'r', encoding='utf-8') as f:
    c = f.read()

t_slider = '''        ImGui::BeginDisabled(!debug.up_velocity_available || !callbacks.queue_debug);
        if (button_row(menu, "Up Boost", "Instantly add vertical velocity. (On board only.)"))
            debug_request(menu, callbacks, {DebugAction::add_up_velocity});
        ImGui::EndDisabled();
    }'''
r_slider = '''        ImGui::BeginDisabled(!debug.up_velocity_available || !callbacks.queue_debug);
        if (button_row(menu, "Up Boost", "Instantly add vertical velocity. (On board only.)"))
            debug_request(menu, callbacks, {DebugAction::add_up_velocity});
        ImGui::EndDisabled();
    }
    
    {
        field(menu, "Off-board Up Boost speed");
        int offboard_up = static_cast<int>(std::lround(debug.offboard_up_velocity_speed));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::BeginDisabled(!callbacks.queue_debug);
        if (ImGui::SliderInt("##offboard-up-speed", &offboard_up, 1, 25, "%d", ImGuiSliderFlags_AlwaysClamp))
            debug_request(menu, callbacks, {DebugAction::set_offboard_up_velocity_speed, false, static_cast<float>(offboard_up)});
        ImGui::EndDisabled();
        if (!debug.offboard_up_velocity_available) warn(debug.offboard_up_velocity_unavailable.c_str());
        ImGui::BeginDisabled(!debug.offboard_up_velocity_available || !callbacks.queue_debug);
        if (button_row(menu, "Off-board Up Boost", "Instantly add vertical velocity. (Off board only.)"))
            debug_request(menu, callbacks, {DebugAction::add_offboard_up_velocity});
        ImGui::EndDisabled();
    }'''

c = c.replace(t_slider, r_slider)
with open('Extension/UI/Overlay/skate_menu_skater.cpp', 'w', encoding='utf-8') as f:
    f.write(c)

with open('Extension/UI/Overlay/skate_menu_settings.cpp', 'r', encoding='utf-8') as f:
    c = f.read()

c = c.replace('row(6, "TP to Freecam", model.bindings.tp_to_freecam_combo);', 'row(6, "TP to Freecam", model.bindings.tp_to_freecam_combo);\n        row(7, "Off-board Up Boost", model.bindings.offboard_up_velocity_combo);')
c = c.replace('action == 5 ? "upvelocity " : "tptofreecam "', 'action == 5 ? "upvelocity " : action == 6 ? "tptofreecam " : "offboardupvelocity "')

with open('Extension/UI/Overlay/skate_menu_settings.cpp', 'w', encoding='utf-8') as f:
    f.write(c)
print("Menus updated")
