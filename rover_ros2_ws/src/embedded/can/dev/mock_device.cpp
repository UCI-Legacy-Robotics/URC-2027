#include "embedded/core/CanFrame.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// Mock Odrive Device to simulate real device when testing in virtual CAN interface (vcan0)
// This is useful for testing the ODriveDevice class without requiring a physical ODrive device 
// connected to the system.

namespace {
constexpr uint8_t kCmdHeartbeat = 0x01;
constexpr uint8_t kCmdSetAxisState = 0x07;
constexpr uint8_t kCmdGetEncoderEstimates = 0x09;
constexpr uint8_t kCmdSetInputPosition = 0x0C;
constexpr uint8_t kCmdSetInputVelocity = 0x0D;
constexpr uint8_t kCmdSetInputTorque = 0x0E;
constexpr uint8_t kCmdClearErrors = 0x18;
constexpr uint8_t kCmdSetOrigin = 0x19;
constexpr uint8_t kCmdGetTorques = 0x1C;

struct MockDrive {
  float position{0.0f};
  float velocity{0.0f};
  float torque{0.0f};
  uint8_t axis_state{1};
  std::chrono::steady_clock::time_point last_update{std::chrono::steady_clock::now()};

  void update_position(std::chrono::steady_clock::time_point now) {
    const std::chrono::duration<float> elapsed = now - last_update;
    position += velocity * elapsed.count();
    last_update = now;
  }
};

float read_float(const uint8_t* data) {
  float value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

uint32_t read_u32(const uint8_t* data) {
  uint32_t value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

CanFrame encoder_estimates(uint8_t node_id, const MockDrive& drive) {
  CanFrame response = CanFrame::make_can_simple(node_id, kCmdGetEncoderEstimates, 8);
  std::memcpy(response.data.data(), &drive.position, sizeof(drive.position));
  std::memcpy(response.data.data() + 4, &drive.velocity, sizeof(drive.velocity));
  return response;
}

CanFrame heartbeat(uint8_t node_id, const MockDrive& drive) {
  CanFrame response = CanFrame::make_can_simple(node_id, kCmdHeartbeat, 8);
  response.data[4] = drive.axis_state;
  return response;
}

std::optional<CanFrame> respond(const CanFrame& request, std::array<MockDrive, 64>& drives) {
  if (request.is_extended) {
    return std::nullopt;
  }

  const uint8_t node_id = request.can_simple_node_id();
  MockDrive& drive = drives[node_id];
  drive.update_position(std::chrono::steady_clock::now());

  switch (request.can_simple_cmd_id()) {
    case kCmdSetAxisState:
      if (request.dlc >= 4) {
        drive.axis_state = static_cast<uint8_t>(read_u32(request.data.data()));
      }
      return heartbeat(node_id, drive);
    case kCmdSetInputPosition:
      if (request.dlc >= 4) {
        drive.position = read_float(request.data.data());
        drive.velocity = 0.0f;
      }
      return encoder_estimates(node_id, drive);
    case kCmdSetInputVelocity:
      if (request.dlc >= 4) {
        drive.velocity = read_float(request.data.data());
      }
      return encoder_estimates(node_id, drive);
    case kCmdSetInputTorque: {
      if (request.dlc >= 4) {
        drive.torque = read_float(request.data.data());
      }
      CanFrame response = CanFrame::make_can_simple(node_id, kCmdGetTorques, 8);
      std::memcpy(response.data.data(), &drive.torque, sizeof(drive.torque));
      std::memcpy(response.data.data() + 4, &drive.torque, sizeof(drive.torque));
      return response;
    }
    case kCmdSetOrigin:
      if (request.dlc >= 4) {
        drive.position = read_float(request.data.data());
      }
      return encoder_estimates(node_id, drive);
    case kCmdClearErrors:
      return heartbeat(node_id, drive);
    default:
      return std::nullopt;
  }
}

bool send_frame(int socket_fd, const CanFrame& frame) {
  struct can_frame raw {};
  raw.can_id = frame.id & CAN_SFF_MASK;
  raw.can_dlc = frame.dlc;
  std::memcpy(raw.data, frame.data.data(), raw.can_dlc);
  if (::write(socket_fd, &raw, sizeof(raw)) != static_cast<ssize_t>(sizeof(raw))) {
    std::perror("CAN mock write");
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string interface_name = (argc > 1) ? argv[1] : "vcan0";

  const int sock_fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock_fd < 0) {
    std::perror("CAN mock socket");
    return 1;
  }

  struct ifreq ifr {};
  std::strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
  if (::ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
    std::perror("CAN mock interface lookup");
    ::close(sock_fd);
    return 1;
  }

  struct sockaddr_can addr {};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::perror("CAN mock bind");
    ::close(sock_fd);
    return 1;
  }

  std::array<MockDrive, 64> drives{};
  struct pollfd pfd {};
  pfd.fd = sock_fd;
  pfd.events = POLLIN;

  while (true) {
    const int ready = ::poll(&pfd, 1, -1);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::perror("CAN mock poll");
      break;
    }
    if ((pfd.revents & POLLIN) == 0) {
      continue;
    }

    struct can_frame raw {};
    if (::read(sock_fd, &raw, sizeof(raw)) != static_cast<ssize_t>(sizeof(raw))) {
      if (errno != EINTR) {
        std::perror("CAN mock read");
      }
      continue;
    }

    CanFrame request{};
    request.is_extended = (raw.can_id & CAN_EFF_FLAG) != 0;
    request.id = request.is_extended ? (raw.can_id & CAN_EFF_MASK) : (raw.can_id & CAN_SFF_MASK);
    request.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
    std::memcpy(request.data.data(), raw.data, request.dlc);

    if (const auto response = respond(request, drives); response && !send_frame(sock_fd, *response)) {
      break;
    }
  }

  ::close(sock_fd);
  return 1;
}
