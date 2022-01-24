#include "buff_detector.hpp"

#include <cmath>
#include <execution>

#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"

using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;

namespace {

const auto kCV_FONT = cv::FONT_HERSHEY_SIMPLEX;
const cv::Scalar kGREEN(0., 255., 0.);
const cv::Scalar kRED(0., 0., 255.);
const cv::Scalar kYELLOW(0., 255., 255.);

}  // namespace

void BuffDetector::InitDefaultParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);

  fs << "binary_th" << 220;
  fs << "se_erosion" << 2;
  fs << "ap_erosion" << 1.;

  fs << "contour_size_low_th" << 2;
  fs << "rect_ratio_low_th" << 0.4;
  fs << "rect_ratio_high_th" << 2.5;

  fs << "contour_center_area_low_th" << 100;
  fs << "contour_center_area_high_th" << 1000;
  fs << "rect_center_ratio_low_th" << 0.6;
  fs << "rect_center_ratio_high_th" << 1.67;
  SPDLOG_DEBUG("Inited params.");
}

bool BuffDetector::PrepareParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
  if (fs.isOpened()) {
    params_.binary_th = fs["binary_th"];
    params_.se_erosion = fs["se_erosion"];
    params_.ap_erosion = fs["ap_erosion"];

    params_.contour_size_low_th = static_cast<int>(fs["contour_size_low_th"]);
    params_.rect_ratio_low_th = fs["rect_ratio_low_th"];
    params_.rect_ratio_high_th = fs["rect_ratio_high_th"];

    params_.contour_center_area_low_th = fs["contour_center_area_low_th"];
    params_.contour_center_area_high_th = fs["contour_center_area_high_th"];
    params_.rect_center_ratio_low_th = fs["rect_center_ratio_low_th"];
    params_.rect_center_ratio_high_th = fs["rect_center_ratio_high_th"];
    return true;
  } else {
    SPDLOG_ERROR("Can not load params.");
    return false;
  }
}

void BuffDetector::FindRects(const cv::Mat &frame) {
  const auto start = high_resolution_clock::now();
  float center_rect_area = params_.contour_center_area_low_th * 1.5;
  rects_.clear();
  hammer_ = cv::RotatedRect();

  frame_size_ = cv::Size(frame.cols, frame.rows);

  cv::Mat channels[3], img;
  cv::split(frame, channels);

#if 1
  if (team_ == game::Team::kBLUE) {
    img = channels[0] - channels[2];
  } else if (team_ == game::Team::kRED) {
    img = channels[2] - channels[0];
  }
#else
  if (team_ == game::Team::kBLUE) {
    result = channels[0] - channels[2];
  } else if (team_ == game::Team::kRED) {
    result = channels[2] - channels[0];
  }
#endif

  cv::threshold(img, img, params_.binary_th, 255., cv::THRESH_BINARY);

  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_RECT,
      cv::Size2i(2 * params_.se_erosion + 1, 2 * params_.se_erosion + 1),
      cv::Point(params_.se_erosion, params_.se_erosion));

  cv::dilate(img, img, kernel);
  cv::morphologyEx(img, img, cv::MORPH_CLOSE, kernel);
  cv::findContours(img, contours_, cv::RETR_TREE, cv::CHAIN_APPROX_NONE);

#if 1
  contours_poly_.resize(contours_.size());
  for (size_t i = 0; i < contours_.size(); ++i) {
    cv::approxPolyDP(cv::Mat(contours_[i]), contours_poly_[i],
                     params_.ap_erosion, true);
  }
#endif

  SPDLOG_DEBUG("Found contours: {}", contours_.size());

  auto check_armor = [&](const auto &contour) {
    if (contour.size() < static_cast<std::size_t>(params_.contour_size_low_th))
      return;

    cv::RotatedRect rect = cv::minAreaRect(contour);
    double rect_ratio = rect.size.aspectRatio();
    double contour_area = cv::contourArea(contour);
    double rect_area = rect.size.area();

    SPDLOG_DEBUG("contour_area is {}", contour_area);
    SPDLOG_DEBUG("rect_area is {}", rect_area);
    if (contour_area > params_.contour_center_area_low_th &&  // 200 - 500
        contour_area < params_.contour_center_area_high_th) {
      if (rect_ratio < params_.rect_center_ratio_high_th &&  // 0.6 - 1.67
          rect_ratio > params_.rect_center_ratio_low_th) {
        buff_.SetCenter(rect.center);
        center_rect_area = rect_area;
        SPDLOG_WARN("center's area is {}", rect_area);
        return;
      }
    }

    if (rect_area > 1.2 * contour_area &&  // 它矩形的面积大于1.5倍的轮廓面积
        rect_area > 20 * center_rect_area &&  // 20倍的圆心矩形面积 10000+
        rect_area < 80 * center_rect_area) {  // 80倍的圆心矩形面积 后续拿到json
      hammer_ = rect;
      SPDLOG_DEBUG("hammer_contour's area is {}", contour_area);
      return;
    }
    if (0 < hammer_.size.area()) {  //宝剑
      if (contour_area > 1.5 * hammer_.size.area()) return;
      if (rect_area > 0.7 * hammer_.size.area()) return;
    }

    SPDLOG_DEBUG("rect_ratio is {}", rect_ratio);  // 0.4 - 2.5
    if (rect_ratio < params_.rect_ratio_low_th) return;
    if (rect_ratio > params_.rect_ratio_high_th) return;

    if (rect_area < 3 * center_rect_area) return;  // 3倍的
    if (rect_area > 15 * center_rect_area) return;

    if (contour_area > rect_area * 1.2) return;
    if (contour_area < rect_area * 0.8) return;

    SPDLOG_DEBUG("armor's area is {}", rect_area);

    rects_.emplace_back(rect);
  };

  std::for_each(std::execution::par_unseq, contours_.begin(), contours_.end(),
                check_armor);

  const auto stop = high_resolution_clock::now();
  duration_rects_ = duration_cast<std::chrono::milliseconds>(stop - start);
}

void BuffDetector::MatchArmors() {
  const auto start = high_resolution_clock::now();
  tbb::concurrent_vector<Armor> armors;

  for (auto &rect : rects_) {
    armors.emplace_back(Armor(rect));
  }

  SPDLOG_DEBUG("armors.size is {}", armors.size());
  SPDLOG_DEBUG("the buff's hammer area is {}", hammer_.size.area());

  if (armors.size() > 0 && hammer_.size.area() > 0) {
    buff_.SetTarget(armors[0]);
    for (auto armor : armors) {
      if (cv::norm(hammer_.center - armor.ImageCenter()) <
          cv::norm(hammer_.center - buff_.GetTarget().ImageCenter())) {
        buff_.SetTarget(armor);
      }
    }
    buff_.SetArmors(armors);
  } else {
    SPDLOG_WARN("can't find buff_armor");
  }
  const auto stop = high_resolution_clock::now();
  duration_armors_ = duration_cast<std::chrono::milliseconds>(stop - start);
}

void BuffDetector::VisualizeArmors(const cv::Mat &output, bool add_lable) {
  tbb::concurrent_vector<Armor> armors = buff_.GetArmors();
  auto draw_armor = [&](const auto &armor) {
    auto vertices = armor.ImageVertices();
    if (vertices == buff_.GetTarget().ImageVertices()) return;

    auto num_vertices = vertices.size();
    for (std::size_t i = 0; i < num_vertices; ++i) {
      cv::line(output, vertices[i], vertices[(i + 1) % num_vertices], kGREEN);
    }
    cv::drawMarker(output, armor.ImageCenter(), kGREEN, cv::MARKER_DIAMOND);

    if (add_lable) {
      cv::putText(output,
                  cv::format("%.2f, %.2f", armor.ImageCenter().x,
                             armor.ImageCenter().y),
                  vertices[1], kCV_FONT, 1.0, kGREEN);
    }
  };
  if (!armors.empty()) {
    std::for_each(std::execution::par_unseq, armors.begin(), armors.end(),
                  draw_armor);
  }

  Armor target = buff_.GetTarget();
  if (cv::Point2f(0, 0) != target.ImageCenter()) {
    auto vertices = target.ImageVertices();
    for (std::size_t i = 0; i < vertices.size(); ++i)
      cv::line(output, vertices[i], vertices[(i + 1) % 4], kRED);
    cv::drawMarker(output, target.ImageCenter(), kRED, cv::MARKER_DIAMOND);
    if (add_lable) {
      std::ostringstream buf;
      buf << target.ImageCenter().x << ", " << target.ImageCenter().y;
      cv::putText(output, buf.str(), vertices[1], kCV_FONT, 1.0, kRED);
    }
  }
}

BuffDetector::BuffDetector() { SPDLOG_TRACE("Constructed."); }

BuffDetector::BuffDetector(const std::string &params_path,
                           game::Team enemy_team) {
  LoadParams(params_path);
  SetTeam(enemy_team);
  SPDLOG_TRACE("Constructed.");
}

BuffDetector::~BuffDetector() { SPDLOG_TRACE("Destructed."); }

void BuffDetector::SetTeam(game::Team enemy_team) {
  if (enemy_team == game::Team::kRED)
    team_ = game::Team::kBLUE;
  else if (enemy_team == game::Team::kBLUE)
    team_ = game::Team::kRED;
  else
    team_ = game::Team::kUNKNOWN;
}

const tbb::concurrent_vector<Buff> &BuffDetector::Detect(const cv::Mat &frame) {
  targets_.clear();
  SPDLOG_DEBUG("Detecting");
  FindRects(frame);
  MatchArmors();
  SPDLOG_DEBUG("Detected.");
  targets_.emplace_back(buff_);
  return targets_;
}

void BuffDetector::VisualizeResult(const cv::Mat &output, int verbose) {
  SPDLOG_DEBUG("Visualizeing Result.");
  if (verbose > 10) {
    cv::drawContours(output, contours_, -1, kRED);
    cv::drawContours(output, contours_poly_, -1, kYELLOW);
  }

  if (verbose > 1) {
    int baseLine, v_pos = 0;

    std::string label =
        cv::format("%ld armors in %ld ms.", buff_.GetArmors().size(),
                   duration_armors_.count());
    cv::Size text_size = cv::getTextSize(label, kCV_FONT, 1.0, 2, &baseLine);
    v_pos += static_cast<int>(1.3 * text_size.height);
    cv::putText(output, label, cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);

    label = cv::format("%ld rects in %ld ms.", rects_.size(),
                       duration_rects_.count());
    text_size = cv::getTextSize(label, kCV_FONT, 1.0, 2, &baseLine);
    v_pos += static_cast<int>(1.3 * text_size.height);
    cv::putText(output, label, cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);
  }
  if (verbose > 3) {
    cv::Point2f vertices[4];
    hammer_.points(vertices);
    for (std::size_t i = 0; i < 4; ++i)
      cv::line(output, vertices[i], vertices[(i + 1) % 4], kRED);

    cv::drawMarker(output, buff_.GetCenter(), kRED, cv::MARKER_DIAMOND);
  }
  VisualizeArmors(output, verbose > 2);
  SPDLOG_DEBUG("Visualized.");
}