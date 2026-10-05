#include "Extension/Customization/developer_board_material.h"
#include "Extension/Customization/developer_hoodie_material.h"
#include "Extension/Multiplayer/developer_identity.h"
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

using namespace dingosdk::multiplayer;
namespace {
void check(bool ok, const std::string &message) {
    if (ok) return;
    std::cerr << message << '\n';
    std::exit(1);
}
bool rejected(std::string_view json) {
    try {
        parse_identity_lists(json);
    } catch (const std::exception &) {
        return true;
    }
    return false;
}
constexpr std::uint64_t dev = 76561198000000001ULL, other_dev = 76561198000000002ULL, homie = 76561198000000003ULL,
                        creator = 76561198000000004ULL, stranger = 76561198000000005ULL;
using L = IdentityList;

void parsing() {
    // The answer as the backend sends it; a list comes back sorted, each player once.
    const auto lists = parse_identity_lists(
        R"({"categories":{"dev":["76561198000000002","76561198000000001","76561198000000002"],)"
        R"("homie":["76561198000000003"],"content_creator":["76561198000000004"]}})");
    check(lists[0] == std::vector{dev, other_dev} && lists[1] == std::vector{homie} && lists[2] == std::vector{creator} &&
              lists[3].empty(),
          "The backend's answer was not read as its three lists, with nobody banned");
    // The ban list comes beside the categories; a player can be in both.
    const auto bans = parse_identity_lists(
        R"({"categories":{"dev":["76561198000000001"]},"banned":["76561198000000005","76561198000000001","76561198000000005"]})");
    check(bans[0] == std::vector{dev} && bans[3] == std::vector{dev, stranger}, "The ban list was not read");
    // A newer backend may list more categories and fields, and an older one fewer.
    const auto partial = parse_identity_lists(
        R"({"updated":"2026-10-04","categories":{"moderator":["76561198000000005"],"dev":["76561198000000001"]}})");
    check(partial[0] == std::vector{dev} && partial[1].empty() && partial[2].empty(),
          "An unknown category was read, or a missing one was not empty");
    // The ends of the range players' SteamID64s come from.
    check(!rejected(R"({"categories":{"dev":["76561197960265729","76561202255233023"]}})"), "A valid SteamID64 was refused");

    for (const std::string_view wrong : {
             "", "go away\n", "<html>Just a moment...</html>", "[]", "{}", R"({"categories":[]})",
             R"({"categories":{"dev":"76561198000000001"}})",   // not a list
             R"({"categories":{"dev":[76561198000000001]}})",   // a number, which JSON readers round
             R"({"categories":{"dev":["76561198000000001 "]}})", R"({"categories":{"dev":["+76561198000000001"]}})",
             R"({"categories":{"dev":["zee_x64"]}})", R"({"categories":{"dev":[""]}})",
             R"({"categories":{"dev":["76561197960265728"]}})", // account 0
             R"({"categories":{"dev":["76561202255233024"]}})", // past the last account
             R"({"categories":{"dev":["103582791429521408"]}})", // a Steam group
             R"({"categories":{"homie":["1"]}})", R"({"categories":{},"banned":"76561198000000005"})",
             R"({"categories":{},"banned":[76561198000000005]})", R"({"categories":{},"banned":["everyone"]})"})
        check(rejected(wrong), "An answer that is not the lists was accepted: " + std::string(wrong));
}

void lookup() {
    check(!reskate_developer(dev) && !identity_listed(homie, L::homie), "Someone was listed before any lists arrived");
    check(publish_identity_lists({{{dev, other_dev}, {homie}, {creator}}}), "The first lists did not count as a change");
    check(reskate_developer(dev) && reskate_developer(other_dev) && identity_listed(homie, L::homie) &&
              identity_listed(creator, L::content_creator),
          "A listed player was not found");
    check(!reskate_developer(homie) && !reskate_developer(creator) && !reskate_developer(stranger) &&
              !identity_listed(dev, L::homie) && !identity_listed(homie, L::content_creator),
          "A player was found in a list they are not in");
    check(!reskate_developer(0) && !identity_listed(dev, L::count), "No player, or no list, matched");
    check(!reskate_banned(dev) && !reskate_banned(stranger), "Someone is banned though the lists ban nobody");
    check(publish_identity_lists({{{dev, other_dev}, {homie}, {creator}, {dev, stranger}}}) && reskate_banned(stranger) &&
              reskate_banned(dev) && reskate_developer(dev) && !reskate_banned(other_dev) && !reskate_banned(0),
          "The ban list did not ban exactly the players on it");
    check(publish_identity_lists({{{dev, other_dev}, {homie}, {creator}}}) && !reskate_banned(stranger), "A lifted ban stayed");
    check(!publish_identity_lists({{{dev, other_dev}, {homie}, {creator}}}), "The same lists counted as a change");
    // The one mark a player carries: a developer's before a content creator's before a homie's.
    check(publish_identity_lists({{{dev}, {dev, homie, creator}, {dev, creator}}}) && identity_mark(dev) == L::developer &&
              identity_mark(creator) == L::content_creator && identity_mark(homie) == L::homie && !identity_mark(stranger) &&
              !identity_mark(0),
          "A player's mark is not the first list they are on");
    check(own_marks_shown(), "A player's marks start hidden");
    show_own_marks(false);
    check(!own_marks_shown() && identity_mark(dev) == L::developer, "Hiding their own marks did not take, or changed the lists");
    show_own_marks(true);
    check(publish_identity_lists({{{dev, other_dev}, {homie}, {creator}}}), "The lists did not go back");
    // Removing someone in the panel takes their mark away at the next refresh.
    check(publish_identity_lists({{{dev}, {}, {}}}) && reskate_developer(dev) && !reskate_developer(other_dev) &&
              !identity_listed(homie, L::homie),
          "Replaced lists kept a player who was removed");
}
// The hoodie and board: a developer's rainbow, a content creator's red, a homie's gold.
void items() {
    using namespace dingosdk::developer_hoodie_detail;
    publish_identity_lists({{{dev}, {dev, homie, creator}, {dev, creator}}});
    check(animation_for(dev) == Animation::rainbow && animation_for(creator) == Animation::red &&
              animation_for(homie) == Animation::gold,
          "A list did not give its animation, a developer's first and a content creator's before a homie's");
    check(animation_for(stranger) == Animation::none && animation_for(0) == Animation::none, "An unlisted player's items animate");
    check(item_color(Animation::rainbow, 0) == rainbow(0) && item_color(Animation::rainbow, 3000) == rainbow(3000),
          "The developer's rainbow changed");
    // Each goes from a deep shade to a bright one, on into the colour next to it, and back
    // every three seconds.
    for (const auto animation : {Animation::red, Animation::gold}) {
        const auto deep = item_color(animation, 0), bright = item_color(animation, 750), far = item_color(animation, 1500);
        check(item_color(animation, 3000) == deep && item_color(animation, 4500) == far, "An animation does not repeat every three seconds");
        check(bright[0] > deep[0] * 2 && far[0] >= bright[0], "An animation does not go from deep to bright");
        if (animation == Animation::red) {
            // A content creator's: red, then a reddish pink.
            check(bright[1] < bright[0] * .1f && bright[2] < bright[0] * .1f, "Red is not red");
            check(far[2] > bright[2] * 3 && far[2] < far[0] * .4f && far[1] < far[0] * .15f, "Red does not go on into a reddish pink");
        } else {
            // A homie's: gold, then yellow.
            check(bright[1] > bright[0] * .5f && bright[1] < bright[0] * .75f && bright[2] < bright[0] * .15f, "Gold is not gold");
            check(far[1] > bright[1] * 1.2f && far[1] < far[0] && far[2] < far[1] * .4f, "Gold does not go on into yellow");
        }
        for (std::uint64_t at = 0; at < 6000; at += 125) {
            // The material's gamut, and red always the strongest part of the colour.
            const auto color = item_color(animation, at);
            check(color[0] <= .801f && color[1] > 0 && color[2] > 0 && color[0] > color[1] && color[0] > color[2],
                  "An animation leaves its colours");
        }
    }
}

// Turning the animation off (the Special page, or a list that no longer has the player) puts an
// item's own colours back, and publishes them the extra times the renderer needs to show them.
void settling() {
    using namespace dingosdk::developer_hoodie_detail;
    const Color black{.02f, .02f, .02f}, navy{.02f, .02f, .2f}, teal{.02f, .3f, .3f};
    // What the game holds for each colour region; the materials are read afresh each tick, as in game.
    std::array<Color, 2> held{black, navy};
    dingosdk::DeveloperHoodieState state;
    Materials live;
    live.component = 1, live.appearance = 2, live.item = 3, live.eligible = true, live.count = 2;
    live.bindings[0] = {10, 100, 7, color_keys[0], black};
    live.bindings[1] = {10, 101, 7, color_keys[3], navy};
    int publishes{};
    auto write = [&](const Binding &binding, const Color &color) {
        held[binding.node - 100] = color;
        return true;
    };
    auto publish = [&](std::uintptr_t) { ++publishes; };
    const auto tick = [&](Animation animation, std::uint64_t at) {
        for (std::size_t i = 0; i < held.size(); ++i) live.bindings[i].color = held[i];
        animate(state, live, 5, 6, animation, at, write, publish);
    };
    tick(Animation::red, 0);
    tick(Animation::red, 700);
    check(state.count == 2 && held[0] == item_color(Animation::red, 700) && held[1] == held[0] && publishes == 2,
          "The animation did not colour the hoodie");
    publishes = 0;
    tick(Animation::none, 800);
    check(held[0] == black && held[1] == navy && publishes == 1, "Turning it off did not bring the hoodie's own colours back");
    for (std::uint64_t at = 900; at < 910; ++at) tick(Animation::none, at);
    check(publishes == 1 + settle_publishes && !state.count && held[0] == black && held[1] == navy,
          "The own colours were not published the extra times, or the hoodie was not left alone after");
    // A colour the game itself sets meanwhile is the game's to keep.
    tick(Animation::gold, 1000);
    held[0] = teal;
    tick(Animation::none, 1100);
    check(held[0] == teal && held[1] == navy, "Turning it off overwrote a colour the game had set");
    // And it comes back on with the own colours still known.
    for (std::uint64_t at = 1200; at < 1210; ++at) tick(Animation::none, at);
    tick(Animation::red, 2000);
    tick(Animation::none, 2100);
    check(held[0] == teal && held[1] == navy, "A second round lost the hoodie's own colours");

    // The board does the same, part by part.
    namespace board = dingosdk::developer_board_detail;
    std::array<Color, 2> painted{black, navy};
    dingosdk::DeveloperBoardState deck;
    board::Materials parts;
    parts.component = 1, parts.appearance = 2, parts.count = 2;
    parts.bindings[0] = {30, 10, 100, 7, board::parameter_key(board::base_color, 1), black, true};
    parts.bindings[1] = {31, 11, 101, 7, board::parameter_key(board::base_color, 1), navy, true};
    int shown{};
    auto paint = [&](const board::Binding &binding, const Color &color) {
        painted[binding.node - 100] = color;
        return true;
    };
    auto show = [&](std::uintptr_t) { ++shown; };
    const auto roll = [&](Animation animation, std::uint64_t at) {
        for (std::size_t i = 0; i < painted.size(); ++i) parts.bindings[i].color = painted[i];
        board::animate(deck, parts, 5, 6, animation, at, paint, show);
    };
    roll(Animation::gold, 0);
    roll(Animation::gold, 700);
    check(deck.count == 2 && painted[0] == item_color(Animation::gold, 700) && painted[1] == painted[0], "The animation did not colour the board");
    shown = 0;
    for (std::uint64_t at = 800; at < 810; ++at) roll(Animation::none, at);
    check(painted[0] == black && painted[1] == navy && shown == 2 * (1 + settle_publishes) && !deck.count,
          "Turning it off did not bring the board's own colours back and publish each part the extra times");
}

// --live <SteamID64>: the deployed backend, read the way the game reads it. Not
// part of the test run, which stays off the network.
int live(const char *player) {
    const auto id = std::strtoull(player, nullptr, 10);
    for (int waited = 0; waited < 150 && !reskate_developer(id); ++waited) {
        refresh_identity_lists();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << player << (reskate_developer(id) ? " is" : " is not") << " a developer in the lists the backend sent\n";
    return reskate_developer(id) ? 0 : 1;
}
} // namespace

int main(int count, char **arguments) {
    if (count == 3 && std::string_view(arguments[1]) == "--live") return live(arguments[2]);
    parsing();
    lookup();
    items();
    settling();
    std::cout << "developer identity tests passed\n";
}
