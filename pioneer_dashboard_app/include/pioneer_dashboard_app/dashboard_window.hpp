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
#include <atomic>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_srvs/srv/trigger.hpp>

#include "pioneer_dashboard_app/lidar_widget.hpp"

namespace pioneer_dashboard_app {

class VideoCanvas : public QWidget {
  Q_OBJECT
public:
  explicit VideoCanvas(QWidget* parent = nullptr);
  void setFrontFrame(const QImage& img);
  void setRearFrame(const QImage& img);
  void setPipEnabled(bool en) { pip_enabled_ = en; update(); }
  void setPipScale(float s)   { pip_scale_ = s;    update(); }

protected:
  void paintEvent(QPaintEvent* e) override;

private:
  std::mutex m_;
  QImage front_, rear_;
  bool  pip_enabled_{true};
  float pip_scale_{0.25f};
};

class DashboardWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit DashboardWindow(std::shared_ptr<rclcpp::Node> node);

private:
  void setupUi();
  void setupRos();
  void setTorchButtonState(bool is_on);
  void setRecordingState(bool is_recording);

  // ROS callbacks
  void frontImgCb(const sensor_msgs::msg::Image::SharedPtr msg);
  void frontCompCb(const sensor_msgs::msg::CompressedImage::SharedPtr msg);
  void rearImgCb(const sensor_msgs::msg::Image::SharedPtr msg);
  void scanCb(const sensor_msgs::msg::LaserScan::SharedPtr msg);

  // Teleop
  enum class DriveCmd { STOP, FWD, BACK, LEFT, RIGHT };
  void publishStop();
  void publishFromState();

private slots:
  void onApplyTopics();
  void onTeleopTick();
  void onRenderTick();
  void onFwdPressed();   void onFwdReleased();
  void onBackPressed();  void onBackReleased();
  void onLeftPressed();  void onLeftReleased();
  void onRightPressed(); void onRightReleased();
  void onStopClicked();

private:
  std::shared_ptr<rclcpp::Node> node_;

  // ROS interfaces
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr              cmd_pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr             front_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr   front_comp_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr             rear_sub_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr         scan_sub_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr                    torch_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr                    torch_off_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr                    rec_start_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr                    rec_stop_client_;

  bool torch_is_on_{false};
  bool recording_{false};

  // Latest-only image/scan buffers
  std::mutex              img_mtx_;
  QImage                  latest_front_;
  QImage                  latest_rear_;
  std::atomic<bool>       new_front_{false};
  std::atomic<bool>       new_rear_{false};

  std::mutex              scan_mtx_;
  sensor_msgs::msg::LaserScan latest_scan_;
  std::atomic<bool>       new_scan_{false};

  // UI widgets
  VideoCanvas* video_{nullptr};
  LidarWidget* lidar_{nullptr};
  QPushButton* torch_btn_{nullptr};
  QPushButton* rec_btn_{nullptr};
  QLineEdit*   front_topic_{nullptr};
  QLineEdit*   rear_topic_{nullptr};
  QLineEdit*   scan_topic_{nullptr};
  QLineEdit*   cmdvel_topic_{nullptr};
  QPushButton* apply_{nullptr};
  QCheckBox*   enable_teleop_{nullptr};
  QSlider*     lin_slider_{nullptr};
  QSlider*     ang_slider_{nullptr};
  QPushButton* fwd_{nullptr};
  QPushButton* back_{nullptr};
  QPushButton* left_{nullptr};
  QPushButton* right_{nullptr};
  QPushButton* stop_{nullptr};

  QTimer teleop_timer_;
  QTimer render_timer_;
  DriveCmd drive_{DriveCmd::STOP};
};

} // namespace pioneer_dashboard_app
