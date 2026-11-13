#pragma once
#include "embedded/core/can_device.hpp"
#include "embedded/devices/servo_messages.hpp"
#include <cstdint>


class Servo : public CanDevice {
    public: 
        explicit Servo(uint8_t node_id);
        void process_can_message(const struct can_frame & frame) override;           
        bool generate_periodic_frame(struct can_frame & out_frame) override;
        std::future<bool> set_control(const ControlMessage & msg) override;
};