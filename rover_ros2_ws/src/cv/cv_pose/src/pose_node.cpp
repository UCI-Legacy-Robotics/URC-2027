#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <cv_interfaces/msg/keypoints.hpp>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using std::placeholders::_1;

class PoseNode : public rclcpp::Node {
public:
    PoseNode() : Node("pose_node"),
                 ort_env_(ORT_LOGGING_LEVEL_WARNING, "cv_pose")
    {
        //ros parameters
        this->declare_parameter<std::string>("model_path", "");
        this->declare_parameter<double>("confidence_threshold", 0.5);
        this->declare_parameter<double>("keypoint_threshold", 0.5);

        model_path_ = this->get_parameter("model_path").as_string();

        confidence_threshold_ =
            this->get_parameter(
                "confidence_threshold"
            ).as_double();

        keypoint_threshold_ =
            this->get_parameter(
                "keypoint_threshold"
            ).as_double();

        if (model_path_.empty()) {
            throw std::runtime_error(
                "model_path parameter is required"
            );
        }

        //configure ONNX Runtime
        session_options_.SetIntraOpNumThreads(4);

        session_options_.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL
        );

        //load ONNX model
        session_ = std::make_unique<Ort::Session>(
            ort_env_,
            model_path_.c_str(),
            session_options_
        );

        //get model input/output names
        Ort::AllocatorWithDefaultOptions allocator;

        auto input_name =
            session_->GetInputNameAllocated(0, allocator);

        auto output_name =
            session_->GetOutputNameAllocated(0, allocator);

        input_name_ = input_name.get();
        output_name_ = output_name.get();

        RCLCPP_INFO(
            this->get_logger(),
            "YOLO26 ONNX model loaded successfully"
        );

        //publisher pushes annotated cam frames
        annotated_publisher_ =
            this->create_publisher<sensor_msgs::msg::Image>(
                "/cv/pose/annotated",
                rclcpp::SensorDataQoS()
            );

        //publisher for COCO keypoints
        keypoints_publisher_ =
            this->create_publisher<cv_interfaces::msg::Keypoints>(
                "/cv/pose/keypoints",
                rclcpp::SensorDataQoS()
            );

        //subscriber gets the cam frames
        image_subscriber_ =
            this->create_subscription<sensor_msgs::msg::Image>(
                "/camera/image_raw",
                rclcpp::SensorDataQoS(),
                std::bind(&PoseNode::image_callback, this, _1)
            );

        RCLCPP_INFO(this->get_logger(), "YOLO26 pose node initialized");
    }

private:

    static constexpr int INPUT_SIZE = 640;
    static constexpr int NUM_KEYPOINTS = 17;
    static constexpr int DETECTION_SIZE = 57;

    struct LetterboxInfo
    {
        float scale;
        int pad_x;
        int pad_y;
    };

    // COCO skeleton lines
    const std::vector<std::pair<int, int>> skeleton_ =
    {
        {5, 6},
        {5, 7},
        {7, 9},
        {6, 8},
        {8, 10},
        {5, 11},
        {6, 12},
        {11, 12},
        {11, 13},
        {13, 15},
        {12, 14},
        {14, 16},
        {0, 1},
        {0, 2},
        {1, 3},
        {2, 4}
    };

    //resize and preserve aspect ratio
    cv::Mat letterbox(const cv::Mat &frame, LetterboxInfo &info) {

        const float scale = std::min(
            static_cast<float>(INPUT_SIZE) / frame.cols,
            static_cast<float>(INPUT_SIZE) / frame.rows
        );

        const int new_width = static_cast<int>(std::round(frame.cols * scale));

        const int new_height = static_cast<int>(std::round(frame.rows * scale));

        cv::Mat resized;

        cv::resize(
            frame,
            resized,
            cv::Size(new_width, new_height)
        );

        const int remaining_x = INPUT_SIZE - new_width;
        const int remaining_y = INPUT_SIZE - new_height;

        const int left = remaining_x / 2;
        const int top = remaining_y / 2;

        const int right = remaining_x - left;
        const int bottom = remaining_y - top;

        cv::Mat padded;

        cv::copyMakeBorder(
            resized,
            padded,
            top,
            bottom,
            left,
            right,
            cv::BORDER_CONSTANT,
            cv::Scalar(114, 114, 114)
        );

        info.scale = scale;
        info.pad_x = left;
        info.pad_y = top;

        return padded;
    }

    cv::Point2f restore_point(float x, float y, const LetterboxInfo &info) {
        return {
            (x - info.pad_x) / info.scale,
            (y - info.pad_y) / info.scale
        };
    }

    void draw_skeleton(
        cv::Mat &frame,
        const std::array<cv::Point2f, NUM_KEYPOINTS> &points,
        const std::array<float, NUM_KEYPOINTS> &scores
    )
    {
        //drawing the lines
        for (const auto &connection : skeleton_)
        {
            const int a = connection.first;
            const int b = connection.second;

            if (scores[a] < keypoint_threshold_ || scores[b] < keypoint_threshold_) {
                continue;
            }

            cv::line(
                frame,
                points[a],
                points[b],
                cv::Scalar(0, 255, 0),
                2
            );
        }

        //draw keypoints
        for (int i = 0; i < NUM_KEYPOINTS; i++)
        {
            if (scores[i] < keypoint_threshold_) {
                continue;
            }

            cv::circle(
                frame,
                points[i],
                4,
                cv::Scalar(0, 0, 255),
                -1
            );
        }
    }

    void process_detections(const cv::Mat &output, cv::Mat &frame, const LetterboxInfo &info, const std_msgs::msg::Header &header) {
        if (output.dims != 3 || output.size[0] != 1 || output.size[2] != DETECTION_SIZE || output.type() != CV_32F) {
            throw std::runtime_error(
                "Unexpected YOLO output shape or type. "
                "Expected float32 [1, N, 57]."
            );
        }

        const int num_detections = output.size[1];

        const float *data = output.ptr<float>();

        //one keypoint message per processed frame, including no-detection frames
        cv_interfaces::msg::Keypoints keypoints_msg;
        keypoints_msg.header = header;
        keypoints_msg.detected = false;
        keypoints_msg.person_confidence = 0.0f;

        float best_confidence = -1.0f;

        for (int i = 0; i < num_detections; i++) {
            const float *det = data + i * DETECTION_SIZE;
            const float confidence = det[4];
            const int class_id = static_cast<int>(std::round(det[5]));

            if (confidence < confidence_threshold_) {
                continue;
            }

            // COCO pose model: class 0 is person
            if (class_id != 0) {
                continue;
            }

            //restore bounding box coordinates
            const cv::Point2f p1 = restore_point(det[0], det[1], info);
            const cv::Point2f p2 = restore_point(det[2], det[3], info);

            cv::rectangle(
                frame,
                p1,
                p2,
                cv::Scalar(255, 0, 0),
                2
            );

            std::array<cv::Point2f, NUM_KEYPOINTS> points;
            std::array<float, NUM_KEYPOINTS> scores;

            //extract all 17 keypoints
            for (int k = 0; k < NUM_KEYPOINTS; k++)
            {
                const int offset = 6 + 3 * k;

                const float x = det[offset];
                const float y = det[offset + 1];
                const float score = det[offset + 2];

                points[k] = restore_point(x, y, info);
                scores[k] = score;
            }

            draw_skeleton(frame, points, scores);

            //select highest-confidence person for downstream recognition
            if (confidence > best_confidence) {
                best_confidence = confidence;
                keypoints_msg.detected = true;
                keypoints_msg.person_confidence = confidence;

                for (int k = 0; k < NUM_KEYPOINTS; k++) {
                    keypoints_msg.x[k] = points[k].x;
                    keypoints_msg.y[k] = points[k].y;
                    keypoints_msg.confidence[k] = scores[k];
                }
            }

            cv::putText(
                frame,
                cv::format("Person %.2f", confidence),
                p1,
                cv::FONT_HERSHEY_SIMPLEX,
                0.6,
                cv::Scalar(255, 0, 0),
                2
            );
        }

        keypoints_publisher_->publish(keypoints_msg);
    }

    void image_callback(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            // Convert ros image to opencv
            auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
            cv::Mat frame = cv_ptr->image;

            //image preprocessing
            LetterboxInfo info;
            cv::Mat input_image = letterbox(frame, info);

            //convert BGR to RGB
            cv::Mat rgb;
            cv::cvtColor(input_image, rgb, cv::COLOR_BGR2RGB);

            //normalize pixels to float32 [0, 1]
            cv::Mat normalized;
            rgb.convertTo(normalized, CV_32F, 1.0 / 255.0);

            //convert HWC image into CHW tensor
            std::vector<float> input_tensor_values(
                3 * INPUT_SIZE * INPUT_SIZE
            );

            std::vector<cv::Mat> channels(3);

            for (int c = 0; c < 3; c++) {
                channels[c] = cv::Mat(
                    INPUT_SIZE,
                    INPUT_SIZE,
                    CV_32F,
                    input_tensor_values.data() +
                        c * INPUT_SIZE * INPUT_SIZE
                );
            }

            cv::split(normalized, channels);

            //input shape [batch, channels, height, width]
            std::array<int64_t, 4> input_shape = {
                1, 3, INPUT_SIZE, INPUT_SIZE
            };

            //create ONNX memory information
            Ort::MemoryInfo memory_info =
                Ort::MemoryInfo::CreateCpu(
                    OrtArenaAllocator,
                    OrtMemTypeDefault
                );

            //create input tensor
            Ort::Value input_tensor =
                Ort::Value::CreateTensor<float>(
                    memory_info,
                    input_tensor_values.data(),
                    input_tensor_values.size(),
                    input_shape.data(),
                    input_shape.size()
                );

            const char* input_names[] = {
                input_name_.c_str()
            };

            const char* output_names[] = {
                output_name_.c_str()
            };

            //run inference
            auto output_tensors = session_->Run(
                Ort::RunOptions{nullptr},
                input_names,
                &input_tensor,
                1,
                output_names,
                1
            );

            //get output tensor shape
            auto shape_info =
                output_tensors[0].GetTensorTypeAndShapeInfo();

            auto output_shape = shape_info.GetShape();

            if (
                output_shape.size() != 3 ||
                output_shape[0] != 1 ||
                output_shape[2] != DETECTION_SIZE
            ) {
                throw std::runtime_error(
                    "Unexpected YOLO26 output tensor shape"
                );
            }

            //get raw inference data
            const float* output_data =
                output_tensors[0].GetTensorData<float>();

            int sizes[3] = {
                static_cast<int>(output_shape[0]),
                static_cast<int>(output_shape[1]),
                static_cast<int>(output_shape[2])
            };

            //wrap ONNX output in OpenCV matrix
            cv::Mat output(
                3,
                sizes,
                CV_32F,
                const_cast<float*>(output_data)
            );

            //decode detections and draw skeleton
            process_detections(output, frame, info, msg->header);

            //publish annotated frame
            auto annotated_msg =
                cv_bridge::CvImage(
                    msg->header,
                    "bgr8",
                    frame
                ).toImageMsg();

            annotated_publisher_->publish(*annotated_msg);
        }

        catch (const cv::Exception &e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "OpenCV error: %s",
                e.what()
            );
        }
        catch (const std::exception &e)
        {
            RCLCPP_ERROR(
                this->get_logger(),
                "Pose inference error: %s",
                e.what()
            );
        }
    }

    std::string model_path_;

    double confidence_threshold_;
    double keypoint_threshold_;

    //ONNX Runtime
    Ort::Env ort_env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;

    std::string input_name_;
    std::string output_name_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscriber_;

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr annotated_publisher_;

    rclcpp::Publisher<cv_interfaces::msg::Keypoints>::SharedPtr keypoints_publisher_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    try
    {
        auto node = std::make_shared<PoseNode>();
        rclcpp::spin(node);
    }
    catch (const std::exception &e)
    {
        RCLCPP_ERROR(
            rclcpp::get_logger("pose_node"),
            "Fatal error: %s",
            e.what()
        );
    }

    rclcpp::shutdown();

    return 0;
}