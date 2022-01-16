#pragma once

#include <vector>

#include "common.hpp"
#include "opencv2/opencv.hpp"

enum class Method {
  kUNKNOWN,
  kKF,
  kEKF,
};

class Filter {
 private:
  bool predict_condition_;

 public:
  Method method_ = Method::kUNKNOWN;
  unsigned int measurements_, states_;

  virtual const cv::Mat& Predict(const cv::Mat& measurements,
                                 const cv::Mat& frame) = 0;
};
