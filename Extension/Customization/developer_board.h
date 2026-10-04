#pragma once
#include "developer_board_material.h"

namespace dingosdk {
void update_developer_board(std::uintptr_t base, std::uintptr_t board, std::uint64_t steam_id,
                            std::uint64_t generation, DeveloperBoardState &state) noexcept;
void tick_local_developer_board(std::uintptr_t base, std::uintptr_t client, bool ready) noexcept;
} // namespace dingosdk
