#pragma once
#include <QWidget>
#include <QObject>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <mutex>
#include <vector>

namespace pioneer_dashboard_app {

class LidarWidget : public QWidget {
  Q_OBJECT
public:
  explicit LidarWidget(QWidget* parent=nullptr);
  void setScan(const sensor_msgs::msg::LaserScan& scan);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  std::mutex m_;
  float angle_min_{0.f};
  float angle_inc_{0.f};
  float range_max_{10.f};
  std::vector<float> ranges_;
};

} // namespace