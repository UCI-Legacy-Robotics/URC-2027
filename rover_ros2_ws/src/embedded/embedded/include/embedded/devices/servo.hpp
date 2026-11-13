#pragma once
#include "embedded/core/can_device.hpp"
#include "embedded/devices/servo_messages.hpp"
#include <cstdint>
#include <atomic>


class Servo : public CanDevice {
    public: 
        explicit Servo(uint8_t node_id);
        void process_can_message(const struct can_frame & frame) override;           
        bool generate_periodic_frame(struct can_frame & out_frame) override;
        std::future<bool> set_control(const ControlMessage & msg) override;

    private: 
        const uint8_t node_id_;

        // atomic: each read/write happens as one indivisible step
        std::atomic<float> target_pos_{0.0f};
        std::atomic<bool> has_target_{false}; // makes sure nothing is sent before the first command arrives

        // heartbeat defaults, meaningless before first heartbeat, check is_rx_healthy 
        std::atomic<uint32_t> axis_error_{0};
        std::atomic<uint8_t> axis_state_{0};
        std::atomic<uint8_t> procedure_result_{0};
        std::atomic<bool> trajectory_done_{false};
};