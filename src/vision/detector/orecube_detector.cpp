#include "orecube_detector.hpp"

#include <execution>

void OreCubeDetector::InitDefaultParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);

  fs << "hue_low_th" << 26;
  fs << "hue_high_th" << 34;
  fs << "saturation_low_th" << 43;
  fs << "saturation_high_th" << 255;
  fs << "value_low_th" << 46;
  fs << "value_high_th" << 255;
  fs << "binary_th" << 120;
  fs << "area_low_th" << 5000;
  fs << "area_high_th" << 75000;
  SPDLOG_DEBUG("Inited params.");
}

bool OreCubeDetector::PrepareParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
  if (fs.isOpened()) {
    params_.hue_low_th = fs["hue_low_th"];
    params_.hue_high_th = fs["hue_high_th"];
    params_.saturation_low_th = fs["saturation_low_th"];
    params_.saturation_high_th = fs["saturation_high_th"];
    params_.value_low_th = fs["value_low_th"];
    params_.value_high_th = fs["value_high_th"];
    params_.binary_th = fs["binary_th"];
    params_.area_low_th = fs["area_low_th"];
    params_.area_high_th = fs["area_high_th"];

    return true;
  } else {
    SPDLOG_ERROR("Can not load params.");
    return false;
  }
}

void OreCubeDetector::FindOreCube(const cv::Mat &frame) {
  targets_.clear();
  duration_cube_.Start();

  cv::Mat result;
  cv::cvtColor(frame, result, cv::COLOR_BGR2HSV);
  cv::inRange(result,
              cv::Scalar(params_.hue_low_th, params_.saturation_low_th,
                         params_.value_low_th),
              cv::Scalar(params_.hue_high_th, params_.saturation_high_th,
                         params_.value_high_th),
              result);
  cv::threshold(result, result, params_.binary_th, 255, cv::THRESH_BINARY);

#if 0 /* 是否进行形态学运算区别不大 */
  cv::Mat kernel = cv::getStructuringElement(0, cv::Size(3, 3));
  cv::morphologyEx(result, result, cv::MorphTypes::MORPH_CLOSE, kernel);
#endif

  cv::findContours(result, contours_, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

#if 1
  contours_poly_.resize(contours_.size());
  for (size_t k = 0; k < contours_.size(); ++k) {
    cv::approxPolyDP(cv::Mat(contours_[k]), contours_poly_[k], 1, true);
  }
#endif

  SPDLOG_DEBUG("Found contours: {}", contours_.size());

  auto check_orecube = [&](const auto &contour) {
    double c_area = cv::contourArea(contour);
    SPDLOG_INFO("c_area is {}", c_area);

    if (c_area < params_.area_low_th) return;
    if (c_area > params_.area_high_th) return;
    OreCube cube(cv::minAreaRect(contour));

    targets_.emplace_back(cube);
  };

  std::for_each(std::execution::par_unseq, contours_.begin(), contours_.end(),
                check_orecube);

  duration_cube_.Calc("Find Ore Cubes.");

  SPDLOG_DEBUG("Find {} ore cube.", targets_.size());
}

OreCubeDetector::OreCubeDetector() { SPDLOG_TRACE("Constructed."); }

OreCubeDetector::OreCubeDetector(const std::string &params_path) {
  LoadParams(params_path);
  SPDLOG_TRACE("Constructed.");
}

OreCubeDetector::~OreCubeDetector() { SPDLOG_TRACE("Destructed."); }

const tbb::concurrent_vector<OreCube> &OreCubeDetector::Detect(
    const cv::Mat &frame) {
  SPDLOG_WARN("Start Detect");
  FindOreCube(frame);
  SPDLOG_WARN("Detected.");
  return targets_;
}

void OreCubeDetector::VisualizeResult(const cv::Mat &output, int verbose) {
  auto draw_orecube = [&](OreCube cube) {
    cube.VisualizeObject(output, verbose > 2, draw::kBLUE);
  };

  if (verbose > 1) {
    cv::drawContours(output, contours_, -1, draw::kBLUE, 3);
    cv::drawContours(output, contours_poly_, -1, draw::kRED, 3);
  }
  if (verbose > 2) {
    std::string label = cv::format("%ld cubes in %ld ms.", targets_.size(),
                                   duration_cube_.Count());
    draw::VisualizeLabel(output, label, 1, draw::kBLACK);
  }
  if (!targets_.empty()) {
    std::for_each(std::execution::par_unseq, targets_.begin(), targets_.end(),
                  draw_orecube);
  }
}
