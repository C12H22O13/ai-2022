#include "aim_assitant.hpp"

#include "armor.hpp"

void AimAssitant::Sort(const cv::Mat& frame) {
  cv::Point2f image_center(frame.cols / 2, frame.rows / 2);
  auto weight = [image_center](Armor armor) {
    double center_dis = cv::norm(armor.ImageCenter() - image_center);
    auto corner_points = armor.ImageVertices();
    std::vector<cv::Point2f> cvt_points;
    cv::warpPerspective(corner_points, cvt_points, armor.trans_,
                        armor.face_size_);
    double diff = 0;
    for (std::size_t i = 0; i < corner_points.size(); i++)
      diff += cv::norm((corner_points[i] - cvt_points[i]));
    return center_dis += diff / 4;
  };

  std::sort(armors_.begin(), armors_.end(), [weight](Armor iti, Armor itj) {
    return weight(iti) > weight(itj);
  });
}

AimAssitant::AimAssitant() { SPDLOG_TRACE("Constructed."); }

AimAssitant::AimAssitant(game::Arm arm) {
  arm_ = arm;
  SPDLOG_TRACE("Constructed.");
}

AimAssitant::~AimAssitant() { SPDLOG_TRACE("Destructed."); }

void AimAssitant::LoadParams(const std::string& armor_param,
                             const std::string& buff_param,
                             const std::string& snipe_param,
                             const std::string& armor_pre_param,
                             const std::string& buff_pre_param) {
  a_detector_.LoadParams(armor_param);
  b_detector_.LoadParams(buff_param);
  s_detector_.LoadParams(snipe_param);
  a_predictor_.LoadParams(armor_pre_param);
  b_predictor_.LoadParams(buff_pre_param);
}

void AimAssitant::SetEnemyTeam(game::Team enemy_team) {
  a_detector_.SetEnemyTeam(enemy_team);
  b_detector_.SetTeam(enemy_team);
  s_detector_.SetEnemyTeam(enemy_team);
}

void AimAssitant::SetClassiferParam(const std::string model_path,
                                    const std::string lable_path,
                                    const cv::Size& input_size) {
  classifier_.LoadModel(model_path);
  classifier_.LoadLable(lable_path);
  classifier_.SetInputSize(input_size);
}

void AimAssitant::SetRFID(game::RFID rfid) {
  if (arm_ == game::Arm::kUNKNOWN) {
    method_ = component::AimMethod::kUNKNOWN;
    return;
  }

  if (arm_ == game::Arm::kHERO) {
    if (rfid == game::RFID::kSNIPE) {
      method_ = component::AimMethod::kSNIPE;
    } else if (rfid == game::RFID::kUNKNOWN) {
      method_ = component::AimMethod::kARMOR;
    }
  } else if (arm_ == game::Arm::kINFANTRY) {
    if (rfid == game::RFID::kBUFF) {
      method_ = component::AimMethod::kBUFF;
    } else if (rfid == game::RFID::kUNKNOWN) {
      method_ = component::AimMethod::kARMOR;
    }
  } else if (arm_ == game::Arm::kSENTRY) {
    method_ = component::AimMethod::kARMOR;
  }
  SPDLOG_INFO("Now Arms : {}, AimMethod : {}", game::ArmToString(arm_),
              component::AimMethodToString(method_));
}

void AimAssitant::SetArm(game::Arm arm) {
  arm_ = arm;
  SPDLOG_DEBUG("Arm : {}", game::ArmToString(arm_));
}

void AimAssitant::SetRace(game::Race race) { b_predictor_.SetRace(race); }

void AimAssitant::SetTime(double time) { b_predictor_.SetTime(time); }

const tbb::concurrent_vector<Armor>& AimAssitant::Aim(const cv::Mat& frame) {
  armors_.clear();
  if (method_ == component::AimMethod::kUNKNOWN) {
    method_ = component::AimMethod::kARMOR;
  }

  if (method_ == component::AimMethod::kBUFF) {
    auto buffs = b_detector_.Detect(frame);
    b_predictor_.SetBuff(buffs.back());
    armors_ = b_predictor_.Predict();
  } else {
    if (method_ == component::AimMethod::kARMOR) {
      armors_ = a_detector_.Detect(frame);
      for (auto& armor : armors_) classifier_.ClassifyModel(armor, frame);
      Sort(frame);
    } else if (method_ == component::AimMethod::kSNIPE) {
      armors_ = s_detector_.Detect(frame);
    }

    a_predictor_.SetArmor(armors_.front());
    armors_ = a_predictor_.Predict();
  }
  return armors_;
}

void AimAssitant::VisualizeResult(const cv::Mat& frame, int add_label) {
  if (method_ == component::AimMethod::kARMOR) {
    a_detector_.VisualizeResult(frame, add_label);
    a_predictor_.VisualizePrediction(frame, add_label);
  } else if (method_ == component::AimMethod::kBUFF) {
    b_detector_.VisualizeResult(frame, add_label);
    b_predictor_.VisualizePrediction(frame, add_label);
  } else if (method_ == component::AimMethod::kSNIPE) {
    s_detector_.VisualizeResult(frame, add_label);
    a_predictor_.VisualizePrediction(frame, add_label);
  }
}
