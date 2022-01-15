#include "buff_predictor.hpp"

#include <ctime>

#include "common.hpp"

using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;

namespace {

const auto kCV_FONT = cv::FONT_HERSHEY_SIMPLEX;
const cv::Scalar kGREEN(0., 255., 0.);
const cv::Scalar kRED(0., 0., 255.);
const cv::Scalar kYELLOW(0., 255., 255.);
const std::chrono::seconds kGAME_TIME(150);

const double kDELTA = 3;  //总延迟时间

}  // namespace

/**
 * @brief 辅助函数：旋转角角度计算
 *
 * @param p 圆周
 * @param ctr 圆心
 * @return double 旋转角(-pi~pi)
 */
static double CalRotatedAngle(const cv::Point2f &p, const cv::Point2f &ctr) {
  auto rel = p - ctr;
  return std::atan2(rel.x, rel.y);
}

/**
 * @brief 辅助函数：积分运算预测旋转角
 *
 * @param t 当前时刻
 * @return double 旋转角
 */
static double PredictIntegralRotatedAngle(double t) {
  return 1.305 * kDELTA +
         0.785 / 1.884 * (cos(1.884 * t) - cos(1.884 * (t + kDELTA)));
}

void BuffPredictor::InitDefaultParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);

  if (method_ == Method::kEKF) {
    fs << "is_EKF" << true;
    fs << "Q_mat" << EKF::Matx55d::eye();
    fs << "R_mat" << EKF::Matx33d::eye();
    fs << "Q_AC_mat" << EKF::Matx55d::eye();
    fs << "R_AC_mat" << EKF::Matx33d::eye();
    fs << "is_KF" << false;
  } else if (method_ == Method::kKF) {
    fs << "is_EKF" << false;
    fs << "Q_mat" << EKF::Matx55d::zeros();
    fs << "R_mat" << EKF::Matx33d::zeros();
    fs << "Q_AC_mat" << EKF::Matx55d::zeros();
    fs << "R_AC_mat" << EKF::Matx33d::zeros();
    fs << "is_KF" << true;
  }
  fs << "delay_time" << 0.1542;
  fs << "error_frame" << 5;

  SPDLOG_DEBUG("Inited params.");
}

bool BuffPredictor::PrepareParams(const std::string &params_path) {
  cv::FileStorage fs(params_path,
                     cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);

  if (fs.isOpened()) {
    params_.is_EKF = (int)fs["is_EKF"] != 0 ? true : false;
    params_.Q_mat = fs["Q_mat"].mat();
    params_.R_mat = fs["R_mat"].mat();
    params_.Q_AC_mat = fs["Q_AC_mat"].mat();
    params_.R_AC_mat = fs["R_AC_mat"].mat();
    params_.is_EKF = (int)fs["is_EKF"] != 0 ? true : false;
    params_.delay_time = fs["delay_time"];
    params_.error_frame = fs["error_frame"];
    if (params_.is_EKF)
      method_ = Method::kEKF;
    else if (params_.is_KF)
      method_ = Method::kKF;
    return true;
  } else {
    SPDLOG_ERROR("Can not load params.");
    return false;
  }
}

/**
 * @brief 匹配旋转方向
 *
 */
void BuffPredictor::MatchDirection() {
  const auto start = std::chrono::system_clock::now();
  SPDLOG_WARN("start MatchDirection");

  if (direction_ == component::Direction::kUNKNOWN) {
    cv::Point2f center = buff_.GetCenter();
    double angle, sum = 0;
    std::vector<double> angles;

    if (circumference_.size() == 5) {
      for (auto point : circumference_) {
        angle = CalRotatedAngle(point, center);
        angles.emplace_back(angle);
      }

      for (auto i = circumference_.size(); i > circumference_.size() - 4; i--) {
        double delta = angles[i] - angles[i - 1];
        sum += delta;
      }

      if (sum > 0)
        direction_ = component::Direction::kCCW;
      else if (sum == 0)
        direction_ = component::Direction::kUNKNOWN;
      else
        direction_ = component::Direction::kCW;

      circumference_.emplace_back(buff_.GetTarget().ImageCenter());
      SPDLOG_DEBUG("Back of circumference point {}, {}.",
                   buff_.GetTarget().ImageCenter().x,
                   buff_.GetTarget().ImageCenter().y);
    }

    SPDLOG_WARN("Buff's Direction is {}", /* TODO */
                component::DirectionToString(direction_));
    const auto stop = std::chrono::system_clock::now();
    duration_direction_ =
        duration_cast<std::chrono::milliseconds>(stop - start);
  }
}

/**
 * @brief 根据原装甲板和角度模拟旋转装甲板
 *
 * @param armor 原始装甲板
 * @param theta 旋转角度
 * @param center 旋转中心
 * @return Armor 旋转后装甲板
 */
Armor BuffPredictor::RotateArmor(double theta, const cv::Point2f &center) {
  cv::Point2f predict_point[4];
  cv::Matx22d rot(cos(theta), -sin(theta), sin(theta), cos(theta));

  auto vertices = buff_.GetTarget().ImageVertices();
  for (int i = 0; i < 3; i++) {
    cv::Matx21d vec(vertices[i].x - center.x, vertices[i].y - center.y);
    cv::Matx21d mat = rot * vec;
    predict_point[i] =
        cv::Point2f(mat.val[0] + center.x, mat.val[1] + center.y);
  }
  return Armor(
      cv::RotatedRect(predict_point[0], predict_point[1], predict_point[2]));
}

/**
 * @brief 匹配预测器
 *
 */
void BuffPredictor::MatchPredict() {
  const auto start = std::chrono::system_clock::now();
  predict_ = Armor();

  if (cv::Point2f(0, 0) == buff_.GetCenter()) {
    SPDLOG_ERROR("Center is empty.");
    return;
  }
  if (cv::Point2f(0, 0) == buff_.GetTarget().ImageCenter()) {
    SPDLOG_ERROR("Target center is empty.");
    return;
  }
  if (component::Direction::kUNKNOWN == direction_) return;

  cv::Point2f target_center = buff_.GetTarget().ImageCenter();
  cv::Point2f center = buff_.GetCenter();
  SPDLOG_DEBUG("center is {},{}", buff_.GetCenter().x, buff_.GetCenter().y);
  Armor predict;

  double angle = CalRotatedAngle(target_center, center);
  double theta = PredictIntegralRotatedAngle(GetTime());
  SPDLOG_WARN("Delta theta : {}", theta);
  while (angle > 90) angle -= 90;
  if (direction_ == component::Direction::kCW) theta = -theta;

  theta = theta / 180 * CV_PI;
  SPDLOG_WARN("Theta : {}", theta);
  Armor armor = RotateArmor(theta, center);
  predict_ = armor;
  const auto stop = std::chrono::system_clock::now();
  duration_predict_ = duration_cast<std::chrono::milliseconds>(stop - start);
}

/**
 * @brief Construct a new BuffPredictor object
 *
 */
BuffPredictor::BuffPredictor() { SPDLOG_TRACE("Constructed."); }

/**
 * @brief Construct a new BuffPredictor object
 *
 * @param buffs 传入的每帧得到的Buff
 */
BuffPredictor::BuffPredictor(const std::string &param,
                             const std::vector<Buff> &buffs) {
  LoadParams(param);
  if (circumference_.size() < 5)
    for (auto buff : buffs) {
      circumference_.push_back(buff.GetTarget().ImageCenter());
      SPDLOG_DEBUG("Get Buff Center{},{} ", buff_.GetTarget().ImageCenter().x,
                   buff_.GetTarget().ImageCenter().y);
    }
  buff_ = buffs.back();
  num_ = buff_.GetArmors().size();
  SPDLOG_TRACE("Constructed.");
}

/**
 * @brief Destroy the BuffPredictor object
 *
 */
BuffPredictor::~BuffPredictor() { SPDLOG_TRACE("Destructed."); }

/**
 * @brief Get the Buff object
 *
 * @return const Buff& 返回buff_
 */
const Buff &BuffPredictor::GetBuff() const { return buff_; }

/**
 * @brief Set the Buff object
 *
 * @param buff 传入buff_
 */
void BuffPredictor::SetBuff(const Buff &buff) {
  SPDLOG_DEBUG("Buff center is {}, {}", buff.GetCenter().x, buff.GetCenter().y);
  buff_ = buff;
}

/**
 * @brief Get the Time object
 *
 * @return double 得到当前时间
 */
double BuffPredictor::GetTime() const {
  auto time = end_time_ - high_resolution_clock::now();
  SPDLOG_WARN("time_: {}ms", time.count() / 1000000.);
  return (double)time.count() / 1000000.;
}

/**
 * @brief Set the Time object
 *
 * @param time 传入当前时间
 */
void BuffPredictor::SetTime(double time) {
  double duration = 90 - time;
  SPDLOG_WARN("duration : {}", duration);
  auto now = high_resolution_clock::now();
  auto end = now + std::chrono::seconds((int64_t)duration);
  end_time_ = end;

  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::time_t end_time = std::chrono::system_clock::to_time_t(end);
  SPDLOG_WARN("Now Ctime : {}", std::ctime(&now_time));
  SPDLOG_WARN("End Ctime : {}", std::ctime(&end_time));
}

/**
 * @brief 重新计时
 *
 */
void BuffPredictor::ResetTime() {
  if (buff_.GetArmors().size() < num_) SetTime(0);
  SPDLOG_WARN("Reset time.");
}

/**
 * @brief 预测主函数
 *
 * @return std::vector<Armor> 返回预测装甲板
 */
const std::vector<Armor> &BuffPredictor::Predict() {
  SPDLOG_DEBUG("Predicting.");
  MatchDirection();
  MatchPredict();
  SPDLOG_ERROR("Predicted.");
  predicts_.emplace_back(predict_);
  return predicts_;
}

/**
 * @brief 绘图函数
 *
 * @param output 所绘制图像
 * @param add_lable 标签等级
 */
void BuffPredictor::VisualizePrediction(const cv::Mat &output, bool add_lable) {
  SPDLOG_DEBUG("{}, {}", predict_.ImageCenter().x, predict_.ImageCenter().y);
  if (cv::Point2f(0, 0) != predict_.ImageCenter()) {
    auto vertices = predict_.ImageVertices();
    for (std::size_t i = 0; i < vertices.size(); ++i)
      cv::line(output, vertices[i], vertices[(i + 1) % 4], kYELLOW, 8);
    cv::line(output, buff_.GetCenter(), predict_.ImageCenter(), kRED, 3);
    if (add_lable) {
      std::ostringstream buf;
      std::string label;
      int baseLine, v_pos = 0;

      buf << predict_.ImageCenter().x << ", " << predict_.ImageCenter().y;
      cv::putText(output, buf.str(), vertices[1], kCV_FONT, 1.0, kRED);

      label = cv::format("Direction %s in %ld ms.",
                         component::DirectionToString(direction_).c_str(),
                         duration_direction_.count());
      cv::Size text_size = cv::getTextSize(label, kCV_FONT, 1.0, 2, &baseLine);
      v_pos += 3 * static_cast<int>(1.3 * text_size.height);
      cv::putText(output, label, cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);

      label = cv::format("Find predict in %ld ms.", duration_predict_.count());
      v_pos += static_cast<int>(1.3 * text_size.height);
      cv::putText(output, label, cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);
    }
  }
}