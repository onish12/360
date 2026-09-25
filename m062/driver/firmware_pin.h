// SPDX-License-Identifier: MIT
#pragma once
namespace phaser360 { namespace windows {
constexpr unsigned kPinnedImageBytes=287488;
constexpr unsigned kPinnedXmanBytes=768;
constexpr unsigned kPinnedPayloadBytes=286720;
// Fixed upstream reference identity, not hardware/deployment approval.
// Kernel caller: PASSIVE_LEVEL, readable kernel buffer of the supplied extent.
bool MatchesFirmwarePin(const unsigned char*,unsigned bytes) noexcept;
}}
