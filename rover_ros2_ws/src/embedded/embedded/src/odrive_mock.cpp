#include "embedded/core/CanFrame.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

using namespace std::chrono_literals;

int main(int argc, char** argv) {
  const std::string interface_name = (argc > 1) ? argv[1] : "vcan0";

  int sock_fd_ = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  struct ifreq ifr {};
  std::strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
  if (::ioctl(sock_fd_, SIOCGIFINDEX, &ifr) < 0) {
    ::close(sock_fd_);
    return false;
  }

  struct sockaddr_can addr {};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(sock_fd_);
    return false;
  }

  while (true) {
    struct can_frame raw {};
    while (::read(sock_fd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
      CanFrame frame{};
      frame.is_extended = (raw.can_id & CAN_EFF_FLAG) != 0;
      frame.id = frame.is_extended ? (raw.can_id & CAN_EFF_MASK) : (raw.can_id & CAN_SFF_MASK);
      frame.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
      std::memcpy(frame.data.data(), raw.data, frame.dlc);

      std::cout << "Received CAN Frame: ID=0x" << std::hex << frame.id
                << " DLC=" << std::dec << static_cast<int>(frame.dlc)
                << " Data=[";
      for (uint8_t i = 0; i < frame.dlc; ++i) {
        std::cout << "0x" << std::hex << static_cast<int>(frame.data[i]);
        if (i < frame.dlc - 1) {
          std::cout << ", ";
        }
      }
      std::cout << "]\n";

      struct can_frame raw {};
      CanFrame new_frame = CanFrame::make_can_simple(1, 9, 8);
      uint8_t heartbeat_data[8] = {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
      std::memcpy(raw.data, heartbeat_data, 8);

      if (frame.can_simple_cmd_id() == 7) { //set axis command
        new_frame = CanFrame::make_can_simple(1, 1, 8); // heartbeat status msg
        uint8_t heartbeat_data[8] = {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
        std::memcpy(raw.data, heartbeat_data, 8);
      }

      if (frame.can_simple_cmd_id() == 0x0C) { //set pos command
        new_frame = CanFrame::make_can_simple(1, 9, 8); // encoder status msg
        uint8_t encoder_data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        std::memcpy(raw.data, encoder_data, 8);
      }

      if (frame.can_simple_cmd_id() == 0x0D) { //set vel command
        new_frame = CanFrame::make_can_simple(1, 9, 8); // encoder status msg
        uint8_t encoder_data[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
        std::memcpy(raw.data, encoder_data, 8);
      }

      raw.can_id = new_frame.is_extended ? ((new_frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG)
                                    : (new_frame.id & CAN_SFF_MASK);
      raw.can_dlc = new_frame.dlc > 8 ? 8 : new_frame.dlc;

      ::write(sock_fd_, &raw, sizeof(raw));
    }
  }
  ::close(sock_fd_);
  return 0;
}