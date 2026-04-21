#include "pioneer_dashboard_app/lidar_widget.hpp"
#include <QPainter>
#include <cmath>
#include <algorithm>

namespace pioneer_dashboard_app {

LidarWidget::LidarWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(200, 200);
}

void LidarWidget::setScan(const sensor_msgs::msg::LaserScan& scan) {
  std::lock_guard<std::mutex> lock(m_);
  angle_min_ = scan.angle_min;
  angle_inc_ = scan.angle_increment;
  range_max_  = std::max(0.1f, scan.range_max);
  ranges_ = scan.ranges;
  update();
}

void LidarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), Qt::black);

  const int w = width(), h = height();
  const QPointF c(w*0.5, h*0.5);
  const float R = std::min(w, h)*0.48f;

  p.setPen(QPen(Qt::darkGray, 1));
  p.drawEllipse(c, R, R);
  p.drawEllipse(c, R*0.66f, R*0.66f);
  p.drawEllipse(c, R*0.33f, R*0.33f);
  p.drawLine(QPointF(c.x()-R, c.y()), QPointF(c.x()+R, c.y()));
  p.drawLine(QPointF(c.x(), c.y()-R), QPointF(c.x(), c.y()+R));

  float amin, ainc, rmax;
  std::vector<float> rr;
  {
    std::lock_guard<std::mutex> lock(m_);
    amin = angle_min_;
    ainc = angle_inc_;
    rmax = range_max_;
    rr = ranges_;
  }

  p.setPen(QPen(Qt::green, 2));
  for (size_t i=0;i<rr.size();++i) {
    float r = rr[i];
    if (!std::isfinite(r) || r < 0.02f) continue;
    r = std::min(r, rmax);
    const float a = amin + float(i)*ainc;
    const float x = (r/rmax)*R*std::cos(a);
    const float y = (r/rmax)*R*std::sin(a);
    p.drawPoint(QPointF(c.x()+x, c.y()-y));
  }

  p.setBrush(Qt::red);
  p.setPen(Qt::NoPen);
  p.drawEllipse(c, 4, 4);
}

} // namespace