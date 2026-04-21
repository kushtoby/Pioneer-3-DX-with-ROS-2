#pragma once
#include <QMainWindow>
#include <QImage>
#include <QTimer>
#include <QObject>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <mutex>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <std_srvs/srv/trigger.hpp>

#include "pioneer_dashboard_app/lidar_widget.hpp"

namespace pioneer_dashboard_app {

class VideoCanvas : public QWidget {
  Q_OBJECT
public:
  explicit VideoCanvas(QWidget* parent=nullptr);

  void setFrontFrame(const QImage& img);
  void setRearFrame(const QImage& img);

  // PiP settings
  void setPipEnabled(bool en) { pip_enabled_ = en; update(); }
  void setPipScale(float s) { pip_scale_ = s; update(); } // 0.1-0.5

protected:
  void paintEvent(QPaintEvent* e) override;

private:
  std::mutex m_;
  QImage front_, rear_;
  bool pip_enabled_{true};
  float pip_scale_{0.25f}; // PiP is 25% of width
};

class DashboardWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit DashboardWindow(std::shared_ptr<rclcpp::Node> node);

private:
  void setupUi();
  void setupRos();

  // ROS callbacks
  void frontImgCb(const sensor_msgs::msg::Image::SharedPtr msg);
  void rearImgCb(const sensor_msgs::msg::Image::SharedPtr msg);
  void scanCb(const sensor_msgs::msg::LaserScan::SharedPtr msg);

  // Teleop
  enum class DriveCmd { STOP, FWD, BACK, LEFT, RIGHT };
  void publishStop();
  void publishFromState();

private slots:
  void onApplyTopics();
  void onTeleopTick();
  void onFwdPressed();  void onFwdReleased();
  void onBackPressed(); void onBackReleased();
  void onLeftPressed(); void onLeftReleased();
  void onRightPressed();void onRightReleased();
  void onStopClicked();

private:
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr front_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr rear_sub_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr torch_client_;
  

  // UI
  VideoCanvas* video_{nullptr};
  LidarWidget* lidar_{nullptr};

  QPushButton* torch_btn_{nullptr};
  QLineEdit* front_topic_{nullptr};
  QLineEdit* rear_topic_{nullptr};
  QLineEdit* scan_topic_{nullptr};
  QLineEdit* cmdvel_topic_{nullptr};
  QPushButton* apply_{nullptr};

  QCheckBox* enable_teleop_{nullptr};
  QSlider* lin_slider_{nullptr};
  QSlider* ang_slider_{nullptr};
  QPushButton* fwd_{nullptr};
  QPushButton* back_{nullptr};
  QPushButton* left_{nullptr};
  QPushButton* right_{nullptr};
  QPushButton* stop_{nullptr};

  QTimer teleop_timer_;
  DriveCmd drive_{DriveCmd::STOP};
};

} // namespace