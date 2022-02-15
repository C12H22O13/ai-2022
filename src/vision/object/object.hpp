#pragma once

#include <vector>

#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"

namespace {

const auto kCV_FONT = cv::FONT_HERSHEY_SIMPLEX;

const cv::Scalar kBLUE(255., 0., 0.);
const cv::Scalar kGREEN(0., 255., 0.);
const cv::Scalar kRED(0., 0., 255.);
const cv::Scalar kYELLOW(0., 255., 255.);
const cv::Scalar kBLACK(0., 0., 0.);

}  // namespace

class ImageObject {
 public:
  std::vector<cv::Point2f> image_vertices_;
  cv::Point2f image_center_;
  cv::Size face_size_;
  cv::Mat trans_;
  float image_angle_;
  double image_ratio_;

  const cv::Point2f &ImageCenter() const { return image_center_; }

  std::vector<cv::Point2f> ImageVertices() const { return image_vertices_; }

  double ImageAngle() const { return image_angle_; }

  double ImageAspectRatio() const { return image_ratio_; }

  cv::Mat ImageFace(const cv::Mat &frame) const {
    cv::Mat face;
    cv::warpPerspective(frame, face, trans_, face_size_);
    cv::cvtColor(face, face, cv::COLOR_RGB2GRAY);
    cv::medianBlur(face, face, 1);
#if 0
  cv::equalizeHist(face, face); /* Tried. No help. */
#endif
    cv::threshold(face, face, 0., 255.,
                  cv::THRESH_BINARY | cv::THRESH_TRIANGLE);

    /* 截取中间正方形 */
    float min_edge = std::min(face.cols, face.rows);
    const int offset_w = (face.cols - min_edge) / 2;
    const int offset_h = (face.rows - min_edge) / 2;
    face = face(cv::Rect(offset_w, offset_h, min_edge, min_edge));
    return face;
  }

  void VisualizeObject(const cv::Mat &output, bool add_lable,
                       const cv::Scalar color = kGREEN,
                       cv::MarkerTypes type = cv::MarkerTypes::MARKER_DIAMOND) {
    auto vertices = ImageVertices();
    auto num_vertices = vertices.size();
    for (std::size_t i = 0; i < num_vertices; ++i)
      cv::line(output, vertices[i], vertices[(i + 1) % num_vertices], color);

    cv::drawMarker(output, ImageCenter(), color, type);

    if (add_lable) {
      cv::putText(output,
                  cv::format("%.2f, %.2f", ImageCenter().x, ImageCenter().y),
                  vertices[1], kCV_FONT, 1.0, color);
    }
  }
};

class PhysicObject {
 public:
  cv::Mat rot_vec_, rot_mat_, trans_vec_, physic_vertices_;

  const cv::Mat &GetRotVec() const { return rot_vec_; }
  void SetRotVec(const cv::Mat &rot_vec) {
    rot_vec_ = rot_vec;
    cv::Rodrigues(rot_vec_, rot_mat_);
  }

  const cv::Mat &GetRotMat() const { return rot_mat_; }
  void SetRotMat(const cv::Mat &rot_mat) {
    rot_mat_ = rot_mat;
    cv::Rodrigues(rot_mat_, rot_vec_);
  }

  const cv::Mat &GetTransVec() const { return trans_vec_; }
  void SetTransVec(const cv::Mat &trans_vec) { trans_vec_ = trans_vec; }

  cv::Vec3d RotationAxis() const {
    cv::Vec3d axis(rot_mat_.at<double>(2, 1) - rot_mat_.at<double>(1, 2),
                   rot_mat_.at<double>(0, 2) - rot_mat_.at<double>(2, 0),
                   rot_mat_.at<double>(1, 0) - rot_mat_.at<double>(0, 1));
    return axis;
  }
  const cv::Mat PhysicVertices() const { return physic_vertices_; }
};
