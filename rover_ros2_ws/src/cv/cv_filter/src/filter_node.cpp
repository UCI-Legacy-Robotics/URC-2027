#include <rclcpp/rclcpp.hpp>
#include <cv_interfaces/msg/gesture_prediction.hpp>
#include <cv_interfaces/msg/gesture_event.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

//  turns softmax probs into one event per real gesture:
//  smooth probabilities, EMA time based so it works at any fps
//  non none gesture has to stay above threshold for hold_sec
//  fire a GestureEvent
//  no more events until cooldown_sec has passed and tje fired gesture dropped below
//     release_threshold so holding the pose doesn't keep it firing

class FilterNode : public rclcpp::Node {
public:
    FilterNode() : Node("filter_node") {
        smooth_sec_        = declare_parameter("smooth_sec", 0.3);         // exp moving avg time constant
        threshold_         = declare_parameter("threshold", 0.8);          // smoothed prob needed to start holding
        hold_sec_          = declare_parameter("hold_sec", 0.5);           // how long it has to stay above threshold
        cooldown_sec_      = declare_parameter("cooldown_sec", 2.0);       // min time between events
        release_threshold_ = declare_parameter("release_threshold", 0.4);  // fired gesture must drop below this to re-arm
        max_gap_sec_       = declare_parameter("max_gap_sec", 1.0);        // bigger gap between predictions -> start over
        none_class_        = declare_parameter("none_class", std::string("none"));

        smoothed_publisher_ = create_publisher<cv_interfaces::msg::GesturePrediction>("/cv/gesture/smoothed", 10);
        events_publisher_   = create_publisher<cv_interfaces::msg::GestureEvent>("/cv/gesture/events", 10);
        predictions_subscriber_ = create_subscription<cv_interfaces::msg::GesturePrediction>(
            "/cv/gesture/predictions", 10,
            [this](cv_interfaces::msg::GesturePrediction::ConstSharedPtr msg) { prediction_callback(*msg); });

        RCLCPP_INFO(get_logger(), "filter: threshold %.2f, hold %.2fs, cooldown %.2fs, release %.2f, smooth %.2fs",
                    threshold_, hold_sec_, cooldown_sec_, release_threshold_, smooth_sec_);
    }

private:
    void reset() {
        smoothed_.clear();
        candidate_ = -1;
        armed_ = true;
        fired_.clear();
    }

    void prediction_callback(const cv_interfaces::msg::GesturePrediction &msg) {
        const double t = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9;
        const auto &probs = msg.probabilities;
        if (probs.empty() || probs.size() != msg.class_names.size()) return;

        //start over if time went backwards/restarted bag, a long gap, or the class list changed
        if (smoothed_.empty() || t <= last_t_ || t - last_t_ > max_gap_sec_ || msg.class_names != classes_) {
            if (!smoothed_.empty()) RCLCPP_INFO(get_logger(), "prediction stream restarted, resetting filter");
            reset();
            classes_ = msg.class_names;
            smoothed_.assign(probs.begin(), probs.end());
        } else {
            //time based EMA, same smoothing over diff fps
            const double a = 1.0 - std::exp(-(t - last_t_) / smooth_sec_);
            for (size_t i = 0; i < probs.size(); i++) smoothed_[i] += a * (probs[i] - smoothed_[i]);
        }
        last_t_ = t;

        const int top = std::max_element(smoothed_.begin(), smoothed_.end()) - smoothed_.begin();

        //re-arm when cooldown is over and the last gesture was let go
        if (!armed_) {
            const auto fired = std::find(classes_.begin(), classes_.end(), fired_);
            const bool released = fired == classes_.end() || smoothed_[fired - classes_.begin()] < release_threshold_;
            if (t - last_fire_t_ >= cooldown_sec_ && released) armed_ = true;
        }

        //hold the same gesture above threshold for hold_sec
        const bool above = classes_[top] != none_class_ && smoothed_[top] >= threshold_;
        if (!above) {
            candidate_ = -1;
        } else if (top != candidate_) {
            candidate_ = top;
            candidate_start_t_ = t;
        } else if (armed_ && t - candidate_start_t_ >= hold_sec_) {
            cv_interfaces::msg::GestureEvent event_msg;
            event_msg.header     = msg.header;
            event_msg.gesture    = classes_[top];
            event_msg.confidence = smoothed_[top];
            event_msg.held_sec   = t - candidate_start_t_;
            events_publisher_->publish(event_msg);
            RCLCPP_INFO(get_logger(), "GESTURE: %s (%.2f, held %.2fs)", event_msg.gesture.c_str(), event_msg.confidence, event_msg.held_sec);

            armed_ = false;
            fired_ = classes_[top];
            last_fire_t_ = t;
        }

        //smoothed probabilities for tuning in rqt_plot
        cv_interfaces::msg::GesturePrediction smoothed_msg;
        smoothed_msg.header          = msg.header;
        smoothed_msg.person_detected = msg.person_detected;
        smoothed_msg.class_names     = classes_;
        smoothed_msg.probabilities.assign(smoothed_.begin(), smoothed_.end());
        smoothed_msg.top_class       = classes_[top];
        smoothed_msg.top_confidence  = smoothed_[top];
        smoothed_publisher_->publish(smoothed_msg);
    }

    //params
    double smooth_sec_, threshold_, hold_sec_, cooldown_sec_, release_threshold_, max_gap_sec_;
    std::string none_class_;

    //state
    std::vector<std::string> classes_;
    std::vector<double> smoothed_;
    double last_t_ = 0.0;
    int candidate_ = -1;           // class index currently being held, -1 = none
    double candidate_start_t_ = 0.0;
    bool armed_ = true;
    std::string fired_;            // last gesture that fired
    double last_fire_t_ = 0.0;

    rclcpp::Subscription<cv_interfaces::msg::GesturePrediction>::SharedPtr predictions_subscriber_;
    rclcpp::Publisher<cv_interfaces::msg::GesturePrediction>::SharedPtr smoothed_publisher_;
    rclcpp::Publisher<cv_interfaces::msg::GestureEvent>::SharedPtr events_publisher_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<FilterNode>());
    } catch (const std::exception &e) {
        RCLCPP_ERROR(rclcpp::get_logger("filter_node"), "Fatal error: %s", e.what());
    }
    rclcpp::shutdown();
    return 0;
}
