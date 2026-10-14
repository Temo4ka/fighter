//===- ai/ai_config.hpp - Parameters of the simple AI -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares AiConfig, the parameters of AiController that decide
/// how it plays: reaction time, how often it blocks and attacks, the range it
/// assumes for a move that gives none. They are read from data/ai.json, so
/// F5 in the sandbox picks up edited values (docs/TUNING.md, section "AI").
///
/// \code
///   // data/ai.json; every key is optional
///   {"reactionDelaySec": 0.2, "blockChance": 0.5, "attackChance": 0.6,
///    "defaultRangeM": [0.6, 1.0], "seed": 1}
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fighter::ai {

struct AiConfig {
    /// How long after the opponent's attack starts the AI notices it and may
    /// raise a block, s. A strike with a shorter startup lands anyway.
    float ReactionDelaySec = 0.2f;
    /// The chance, rolled once per opponent attack, that the AI blocks it.
    float BlockChance = 0.5f;
    /// How long the block is held after the opponent's attack is over, s.
    float BlockHoldSec = 0.3f;
    /// The AI blocks only while the opponent is closer than this, m.
    float BlockRangeM = 1.6f;
    /// How often the AI decides whether to strike while in range, s.
    float DecisionIntervalSec = 0.3f;
    /// The chance that a decision in range is a strike.
    float AttackChance = 0.6f;
    /// How long the attack buttons are held, s.
    float PressSec = 0.1f;
    /// The least time of doing nothing after a strike, s.
    float RecoverSec = 0.35f;
    /// The AI does not strike below this share of its stamina.
    float MinStaminaFraction = 0.15f;
    /// The pelvis distance a move is meant for when its "ai" gives none, m.
    float DefaultMinRangeM = 0.6f;
    float DefaultMaxRangeM = 1.0f;
    /// How much closer than the far end of a move's range the AI stops while
    /// approaching, m, so that small drifts do not take it out of range.
    float RangeMarginM = 0.05f;
    /// Seed of the random generator of an AiController (rolls are the only
    /// randomness: the same seed and input give the same fight).
    uint64_t Seed = 1;
};

/// Parses data/ai.json from text. Throws std::runtime_error that names the
/// key and the value; an unknown key is an error.
AiConfig parseAiConfig(std::string_view JsonText);

/// Reads \p Path. Throws std::runtime_error that names the file.
AiConfig loadAiConfig(const std::filesystem::path& Path);

} // namespace fighter::ai
