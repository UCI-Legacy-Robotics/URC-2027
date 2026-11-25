#include <rclcpp/rclcpp.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cv_interfaces/msg/keypoints.hpp>
#include <cv_interfaces/msg/gesture_prediction.hpp>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "preprocess.hpp"  // cv_gesture/cv_gesture/preprocess.hpp

namespace cg = cv_gesture;

//keeps the last couple seconds of normalized keypoint frames, runs the gesture CNN on the
//latest window every frame and publishes per-class probabilities
//deciding if a gesture really happened will be in the filter node

class ClassifierNode : public rclcpp::Node {
public:
    ClassifierNode() : Node("classifier_node"), ort_env_(ORT_LOGGING_LEVEL_WARNING, "cv_classifier") {
        const std::string models = ament_index_cpp::get_package_share_directory("cv_classifier") + "/models/";
        const auto model_path    = declare_parameter("model_path", models + "gesture_cnn.onnx");
        const auto classes_path  = declare_parameter("classes_path", models + "classes.txt");

        load_classes(classes_path);

        //load ONNX model
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);  // tiny model, one thread is plenty
        session_ = std::make_unique<Ort::Session>(ort_env_, model_path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions allocator;
        input_name_  = session_->GetInputNameAllocated(0, allocator).get();
        output_name_ = session_->GetOutputNameAllocated(0, allocator).get();
        check_model();

        //no-person output: all probability on "none" if the model has that class
        no_person_probs_.assign(classes_.size(), 0.0f);
        const auto none = std::find(classes_.begin(), classes_.end(), "none");
        if (none != classes_.end()) no_person_probs_[none - classes_.begin()] = 1.0f;

        predictions_publisher_ = create_publisher<cv_interfaces::msg::GesturePrediction>("/cv/gesture/predictions", 10);
        keypoints_subscriber_ = create_subscription<cv_interfaces::msg::Keypoints>(
            "/cv/pose/keypoints", rclcpp::SensorDataQoS(),
            [this](cv_interfaces::msg::Keypoints::ConstSharedPtr msg) { keypoints_callback(*msg); });

        std::string names;
        for (const auto &c : classes_) names += c + " ";
        RCLCPP_INFO(get_logger(), "gesture model loaded: %s, classes: %s", model_path.c_str(), names.c_str());
    }

private:
    static constexpr double BUFFER_SEC = cg::WINDOW_SEC + 0.5;  // a bit more history than one window
    static constexpr double MIN_VALID  = 0.8;

    void load_classes(const std::string &path) {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("cannot open classes file " + path);
        for (std::string line; std::getline(file, line);) {
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (!line.empty()) classes_.push_back(line);
        }
        if (classes_.empty()) throw std::runtime_error("classes file " + path + " is empty");
    }

    //fail if model doesn't match the preprocessing or the class list
    void check_model() {
        const auto shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3 || shape[1] != cg::WINDOW_LEN || shape[2] != cg::NUM_FEATURES) {
            throw std::runtime_error("model input shape doesn't match preprocessing [1, " + std::to_string(cg::WINDOW_LEN) + ", " + std::to_string(cg::NUM_FEATURES) + "]");
        }
        cg::Window dummy{};
        const auto logits = infer(dummy);
        if (logits.size() != classes_.size()) {
            throw std::runtime_error("model outputs " + std::to_string(logits.size()) + " classes but classes file has " + std::to_string(classes_.size()));
        }
    }

    std::vector<float> infer(cg::Window &window) {
        const std::array<int64_t, 3> input_shape{1, cg::WINDOW_LEN, cg::NUM_FEATURES};
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, window.data(), window.size(), input_shape.data(), input_shape.size());

        const char *input_names[]  = {input_name_.c_str()};
        const char *output_names[] = {output_name_.c_str()};
        auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

        const float *logits = outputs[0].GetTensorData<float>();
        const size_t n = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
        return {logits, logits + n};
    }

    static std::vector<float> softmax(const std::vector<float> &logits) {
        const float max = *std::max_element(logits.begin(), logits.end());  // subtract max for stability
        std::vector<float> probs(logits.size());
        float sum = 0.0f;
        for (size_t i = 0; i < logits.size(); i++) {
            probs[i] = std::exp(logits[i] - max);
            sum += probs[i];
        }
        for (auto &p : probs) p /= sum;
        return probs;
    }

    void keypoints_callback(const cv_interfaces::msg::Keypoints &msg) {
        //capture time, same as preprocess.stamp_to_sec
        const double t = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9;
        if (!times_.empty() && t <= times_.back()) {
            times_.clear();  // time went backwards (e.g. bag restarted)
            frames_.clear();
        }
        times_.push_back(t);
        frames_.push_back(cg::normalize_frame(msg.x, msg.y, msg.confidence, msg.detected));
        while (!times_.empty() && times_.front() < t - BUFFER_SEC) {
            times_.pop_front();
            frames_.pop_front();
        }

        auto window = cg::resample_window(times_, frames_);
        if (!window) return;  // still filling first window

        //checks if a person is usable in enough of the last WINDOW_SEC?
        int in_window = 0, valid = 0;
        for (size_t i = 0; i < times_.size(); i++) {
            if (times_[i] > t - cg::WINDOW_SEC) {
                in_window++;
                valid += cg::frame_valid(frames_[i]);
            }
        }
        const bool person = in_window > 0 && valid >= MIN_VALID * in_window;
        const std::vector<float> probs = person ? softmax(infer(*window)) : no_person_probs_;

        cv_interfaces::msg::GesturePrediction prediction_msg;
        prediction_msg.header          = msg.header;
        prediction_msg.person_detected = person;
        prediction_msg.class_names     = classes_;
        prediction_msg.probabilities   = probs;
        const size_t top = std::max_element(probs.begin(), probs.end()) - probs.begin();
        prediction_msg.top_class      = classes_[top];
        prediction_msg.top_confidence = probs[top];
        predictions_publisher_->publish(prediction_msg);
    }

    std::vector<std::string> classes_;
    std::vector<float> no_person_probs_;
    std::deque<double> times_;
    std::deque<cg::Frame> frames_;

    //ONNX Runtime
    Ort::Env ort_env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string input_name_, output_name_;

    rclcpp::Subscription<cv_interfaces::msg::Keypoints>::SharedPtr keypoints_subscriber_;
    rclcpp::Publisher<cv_interfaces::msg::GesturePrediction>::SharedPtr predictions_publisher_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<ClassifierNode>());
    } catch (const std::exception &e) {
        RCLCPP_ERROR(rclcpp::get_logger("classifier_node"), "Fatal error: %s", e.what());
    }
    rclcpp::shutdown();
    return 0;
}
