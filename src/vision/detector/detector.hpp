#pragma once

#include <chrono>
#include <vector>

#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"

template <typename Target, typename Param>
class Detector {
 private:
  virtual void InitDefaultParams(const std::string &path) = 0;
  virtual bool PrepareParams(const std::string &path) = 0;

 public:
  cv::Size frame_size_;
  std::vector<Target> targets_;
  Param params_;

  void LoadParams(const std::string &path) {
    if (!PrepareParams(path)) {
      InitDefaultParams(path);
      PrepareParams(path);
      SPDLOG_WARN("Can not find params file. Created and reloaded.");
    }
    SPDLOG_DEBUG("Params loaded.");
  }

  virtual const std::vector<Target> &Detect(const cv::Mat &frame) = 0;
  virtual void VisualizeResult(const cv::Mat &output, int verbose = 1) = 0;
};
