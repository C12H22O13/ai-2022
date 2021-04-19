#include "buff_detector.hpp"

#include "gtest/gtest.h"
#include "opencv2/opencv.hpp"

TEST(TestVision, TestBuffDetector) {
  BuffDetector buff_detector("../../../runtime/RMUT2021_Buff.json",
                             game::Team::kRED);

  cv::Mat frame = cv::imread("../../../image/test_buff.png", cv::IMREAD_COLOR);
  ASSERT_FALSE(frame.empty()) << "Can not opening image.";

  buff_detector.Detect(frame);
  buff_detector.VisualizeResult(frame, 2);

  cv::Mat result = frame.clone();
  cv::imwrite("../../../image/test_buff_result.png", result);
  SUCCEED();
}

TEST(TestVision, TestVideo) {
  BuffDetector buff_detector("../../../../runtime/RMUT2021_Buff.json",
                             game::Team::kRED);

  cv::VideoCapture cap("../../../../redbuff.avi");
  cv::Mat frame;
  SPDLOG_WARN("cap.isOpened {}", cap.isOpened());
  while (cap.isOpened()) {
    cap >> frame;
    ASSERT_FALSE(frame.empty()) << "Can not opening image.";
    buff_detector.Detect(frame);
    buff_detector.VisualizeResult(frame, 10);
    cv::imshow("win", frame);
    cv::waitKey(1);
  }

  // cv::Mat result = frame.clone();

  // cv::imwrite("../../../image/test_buff_result.png", result);
  SUCCEED();
}