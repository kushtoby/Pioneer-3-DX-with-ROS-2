#include <QApplication>
#include <rclcpp/rclcpp.hpp>
#include <thread>
#include "pioneer_dashboard_app/dashboard_window.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  QApplication app(argc, argv);

  auto node = std::make_shared<rclcpp::Node>("pioneer_dashboard_app");
  pioneer_dashboard_app::DashboardWindow win(node);
  win.show();

  // Spin ROS in a background thread
  std::thread spin_thread([&](){ rclcpp::spin(node); });
  const int ret = app.exec();

  rclcpp::shutdown();
  spin_thread.join();
  return ret;
}