#pragma once

#include <array>
#include <cstdint>

struct CanFrame {
  uint32_t id{0};
  uint8_t dlc{0};
  bool is_extended{false};
  std::array<uint8_t, 8> data{};

  static CanFrame make_can_simple(uint8_t node_id, uint8_t cmd_id, uint8_t len = 8) noexcept {
    CanFrame f{};
    f.id = ((static_cast<uint32_t>(node_id) & 0x3FU) << 5) | (cmd_id & 0x1FU);
    f.dlc = len > 8 ? 8 : len;
    return f;
  }

  static CanFrame make_extended(uint32_t ext_id, uint8_t len = 8) noexcept {
    CanFrame f{};
    f.id = ext_id & 0x1FFFFFFFU;
    f.dlc = len > 8 ? 8 : len;
    f.is_extended = true;
    return f;
  }

  uint8_t can_simple_node_id() const noexcept { return (id >> 5) & 0x3FU; }
  uint8_t can_simple_cmd_id() const noexcept { return id & 0x1FU; }
};

struct CanFilter {
  static constexpr uint32_t kEffFlag = 0x80000000U;
  static constexpr uint32_t kRtrFlag = 0x40000000U;

  uint32_t id{0};
  uint32_t mask{0};

  // Matches all 32 CANSimple command IDs for a 6-bit node_id (0..63)
  static constexpr CanFilter can_simple(uint8_t node_id) noexcept {
    return {(static_cast<uint32_t>(node_id & 0x3FU) << 5), (0x3FU << 5) | kEffFlag | kRtrFlag};
  }

  // Matches 29-bit CubeMars extended frames where low byte [7:0] == controller_id
  static constexpr CanFilter cubemars(uint8_t controller_id) noexcept {
    return {static_cast<uint32_t>(controller_id) | kEffFlag, 0xFFU | kEffFlag | kRtrFlag};
  }

  bool matches(const CanFrame& f) const noexcept {
    const uint32_t wire_id = f.is_extended ? ((f.id & 0x1FFFFFFFU) | kEffFlag) : (f.id & 0x7FFU);
    return (wire_id & mask) == (id & mask);
  }

  constexpr bool overlaps(const CanFilter& other) const noexcept {
    return ((id ^ other.id) & (mask & other.mask)) == 0U;
  }
};