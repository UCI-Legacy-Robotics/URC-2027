#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <cv_interfaces/msg/keypoints.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <onnxruntime_cxx_api.h>

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

class PoseNode : public rclcpp::Node {
public:
    PoseNode() : Node("pose_node"), ort_env_(ORT_LOGGING_LEVEL_WARNING, "cv_pose") {
        const auto model_path   = declare_parameter("model_path", std::string{});
        confidence_threshold_   = declare_parameter("confidence_threshold", 0.5);
        keypoint_threshold_     = declare_parameter("keypoint_threshold", 0.5);
        if (model_path.empty()) throw std::runtime_error("model_path parameter is required");

        //configure and load ONNX model
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(ort_env_, model_path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions allocator;
        input_name_  = session_->GetInputNameAllocated(0, allocator).get();
        output_name_ = session_->GetOutputNameAllocated(0, allocator).get();
        RCLCPP_INFO(get_logger(), "YOLO26 ONNX model loaded successfully");

        const auto qos = rclcpp::SensorDataQoS();
        annotated_publisher_ = create_publisher<sensor_msgs::msg::Image>("/cv/pose/annotated", qos);
        keypoints_publisher_ = create_publisher<cv_interfaces::msg::Keypoints>("/cv/pose/keypoints", qos);
        image_subscriber_ = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image_raw", qos,
            [this](sensor_msgs::msg::Image::SharedPtr msg) { image_callback(msg); });

        RCLCPP_INFO(get_logger(), "YOLO26 pose node initialized");
    }

private:
    static constexpr int INPUT_SIZE     = 640;
    static constexpr int NUM_KEYPOINTS  = 17;
    static constexpr int DETECTION_SIZE = 6 + 3 * NUM_KEYPOINTS;  // box(4) + conf + class + kpts

    inline static const cv::Scalar BOX_COLOR{255, 0, 0}, LINE_COLOR{0, 255, 0},
                                   POINT_COLOR{0, 0, 255}, PAD_COLOR{114, 114, 114};

    // COCO skeleton lines
    static constexpr std::array<std::pair<int, int>, 16> SKELETON{{
        {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10}, {5, 11}, {6, 12}, {11, 12},
        {11, 13}, {13, 15}, {12, 14}, {14, 16}, {0, 1}, {0, 2}, {1, 3}, {2, 4}}};

    struct LetterboxInfo { float scale; int pad_x; int pad_y; };

    using Points = std::array<cv::Point2f, NUM_KEYPOINTS>;
    using Scores = std::array<float, NUM_KEYPOINTS>;

    //resize and preserve aspect ratio
    static cv::Mat letterbox(const cv::Mat &frame, LetterboxInfo &info) {
        info.scale = std::min(float(INPUT_SIZE) / frame.cols, float(INPUT_SIZE) / frame.rows);
        const int w = std::round(frame.cols * info.scale);
        const int h = std::round(frame.rows * info.scale);
        info.pad_x = (INPUT_SIZE - w) / 2;
        info.pad_y = (INPUT_SIZE - h) / 2;

        cv::Mat resized, padded;
        cv::resize(frame, resized, {w, h});
        cv::copyMakeBorder(resized, padded,
                           info.pad_y, INPUT_SIZE - h - info.pad_y,
                           info.pad_x, INPUT_SIZE - w - info.pad_x,
                           cv::BORDER_CONSTANT, PAD_COLOR);
        return padded;
    }

    static cv::Point2f restore_point(float x, float y, const LetterboxInfo &info) {
        return {(x - info.pad_x) / info.scale, (y - info.pad_y) / info.scale};
    }

    void draw_skeleton(cv::Mat &frame, const Points &pts, const Scores &scores) const {
        for (const auto &[a, b] : SKELETON)
            if (scores[a] >= keypoint_threshold_ && scores[b] >= keypoint_threshold_)
                cv::line(frame, pts[a], pts[b], LINE_COLOR, 2);

        for (int i = 0; i < NUM_KEYPOINTS; i++)
            if (scores[i] >= keypoint_threshold_)
                cv::circle(frame, pts[i], 4, POINT_COLOR, -1);
    }

    void process_detections(const float *data, int num_detections, cv::Mat &frame,
                            const LetterboxInfo &info, const std_msgs::msg::Header &header) {
        //one keypoint message per processed frame, including no-detection frames
        cv_interfaces::msg::Keypoints kp_msg;
        kp_msg.header = header;
        kp_msg.detected = false;
        kp_msg.person_confidence = 0.0f;

        for (int i = 0; i < num_detections; i++) {
            const float *det = data + i * DETECTION_SIZE;
            const float confidence = det[4];
            // COCO pose model: class 0 is person
            if (confidence < confidence_threshold_ || std::lround(det[5]) != 0) continue;

            const cv::Point2f p1 = restore_point(det[0], det[1], info);
            const cv::Point2f p2 = restore_point(det[2], det[3], info);
            cv::rectangle(frame, p1, p2, BOX_COLOR, 2);

            Points pts;
            Scores scores;
            for (int k = 0; k < NUM_KEYPOINTS; k++) {
                const float *kp = det + 6 + 3 * k;
                pts[k] = restore_point(kp[0], kp[1], info);
                scores[k] = kp[2];
            }
            draw_skeleton(frame, pts, scores);
            cv::putText(frame, cv::format("Person %.2f", confidence), p1,
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, BOX_COLOR, 2);

            //select highest-confidence person for downstream recognition
            if (confidence > kp_msg.person_confidence) {
                kp_msg.detected = true;
                kp_msg.person_confidence = confidence;
                for (int k = 0; k < NUM_KEYPOINTS; k++) {
                    kp_msg.x[k] = pts[k].x;
                    kp_msg.y[k] = pts[k].y;
                    kp_msg.confidence[k] = scores[k];
                }
            }
        }
        keypoints_publisher_->publish(kp_msg);
    }

    void image_callback(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            cv::Mat frame = cv_bridge::toCvCopy(msg, "bgr8")->image;

            //letterbox, BGR->RGB, scale to [0,1], HWC->NCHW in one step
            LetterboxInfo info;
            cv::Mat blob = cv::dnn::blobFromImage(letterbox(frame, info), 1.0 / 255.0,
                                                  cv::Size(), cv::Scalar(), /*swapRB=*/true);

            const std::array<int64_t, 4> input_shape{1, 3, INPUT_SIZE, INPUT_SIZE};
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info_, blob.ptr<float>(), blob.total(),
                input_shape.data(), input_shape.size());

            const char *input_names[]  = {input_name_.c_str()};
            const char *output_names[] = {output_name_.c_str()};
            auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                         input_names, &input_tensor, 1, output_names, 1);

            const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() != 3 || shape[0] != 1 || shape[2] != DETECTION_SIZE)
                throw std::runtime_error("Unexpected YOLO26 output shape, expected [1, N, 57]");

            process_detections(outputs[0].GetTensorData<float>(), static_cast<int>(shape[1]),
                               frame, info, msg->header);

            annotated_publisher_->publish(*cv_bridge::CvImage(msg->header, "bgr8", frame).toImageMsg());
        } catch (const std::exception &e) {
            RCLCPP_ERROR(get_logger(), "Pose inference error: %s", e.what());
        }
    }

    double confidence_threshold_;
    double keypoint_threshold_;

    //ONNX Runtime
    Ort::Env ort_env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string input_name_, output_name_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscriber_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr annotated_publisher_;
    rclcpp::Publisher<cv_interfaces::msg::Keypoints>::SharedPtr keypoints_publisher_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<PoseNode>());
    } catch (const std::exception &e) {
        RCLCPP_ERROR(rclcpp::get_logger("pose_node"), "Fatal error: %s", e.what());
    }
    rclcpp::shutdown();
    return 0;
}