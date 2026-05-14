#include "pioneer_dashboard_rviz/dashboard_panel.hpp"

#include <cmath>
#include <algorithm>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QKeyEvent>
#include <QPixmap>

#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgproc.hpp>

#include <rviz_common/display_context.hpp>
#include <pluginlib/class_list_macros.hpp>

namespace pioneer_dashboard_rviz
{

// ---------------- LidarWidget ----------------
LidarWidget::LidarWidget(QWidget * parent) : QWidget(parent)
{
  setMinimumHeight(220);
}

void LidarWidget::setScan(const sensor_msgs::msg::LaserScan & scan)
{
  std::lock_guard<std::mutex> lock(m_);
  angle_min_ = scan.angle_min;
  angle_inc_ = scan.angle_increment;
  range_max_ = std::max(0.1f, scan.range_max);
  ranges_ = scan.ranges;
}

void LidarWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.fillRect(rect(), Qt::black);

  const int w = width();
  const int h = height();
  const QPointF c(w * 0.5, h * 0.5);
  const float radius = std::min(w, h) * 0.48f;

  // draw rings
  p.setPen(QPen(Qt::darkGray, 1));
  p.drawEllipse(c, radius, radius);
  p.drawEllipse(c, radius * 0.66f, radius * 0.66f);
  p.drawEllipse(c, radius * 0.33f, radius * 0.33f);

  // draw axes
  p.setPen(QPen(Qt::gray, 1));
  p.drawLine(QPointF(c.x() - radius, c.y()), QPointF(c.x() + radius, c.y()));
  p.drawLine(QPointF(c.x(), c.y() - radius), QPointF(c.x(), c.y() + radius));

  // copy scan under lock
  float amin, ainc, rmax;
  std::vector<float> rr;
  {
    std::lock_guard<std::mutex> lock(m_);
    amin = angle_min_;
    ainc = angle_inc_;
    rmax = range_max_;
    rr = ranges_;
  }

  // plot points
  p.setPen(QPen(Qt::green, 2));
  for (size_t i = 0; i < rr.size(); ++i) {
    const float r = rr[i];
    if (!std::isfinite(r) || r <= 0.02f) continue;
    const float clamped = std::min(r, rmax);
    const float a = amin + static_cast<float>(i) * ainc;
    const float x = (clamped / rmax) * radius * std::cos(a);
    const float y = (clamped / rmax) * radius * std::sin(a);
    p.drawPoint(QPointF(c.x() + x, c.y() - y)); // invert y for screen coords
  }

  // robot marker
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::red);
  p.drawEllipse(c, 4, 4);
}

// ---------------- DashboardPanel ----------------
DashboardPanel::DashboardPanel(QWidget * parent) : rviz_common::Panel(parent)
{
  setupUi();
  connectSignals();

  teleop_timer_.setInterval(50); // 20 Hz
  connect(&teleop_timer_, &QTimer::timeout, this, &DashboardPanel::onTeleopTick);

  // so key events can be received when you click inside the panel
  setFocusPolicy(Qt::StrongFocus);
}

void DashboardPanel::onInitialize()
{
  // Use RViz’s internal node (per the Jazzy tutorial)
  node_ptr_ = getDisplayContext()->getRosNodeAbstraction().lock();
  makeOrRemakeRosInterfaces();
}

void DashboardPanel::setupUi()
{
  auto * root = new QVBoxLayout(this);

  // -------- Topics --------
  auto * topicsBox = new QGroupBox("Topics");
  auto * topicsGrid = new QGridLayout(topicsBox);

  topicsGrid->addWidget(new QLabel("Image:"), 0, 0);
  image_topic_edit_ = new QLineEdit("/camera/image_raw");
  topicsGrid->addWidget(image_topic_edit_, 0, 1);

  topicsGrid->addWidget(new QLabel("Scan:"), 1, 0);
  scan_topic_edit_ = new QLineEdit("/scan");
  topicsGrid->addWidget(scan_topic_edit_, 1, 1);

  topicsGrid->addWidget(new QLabel("cmd_vel:"), 2, 0);
  cmdvel_topic_edit_ = new QLineEdit("cmd_vel");
  topicsGrid->addWidget(cmdvel_topic_edit_, 2, 1);

  apply_topics_btn_ = new QPushButton("Apply / Reconnect");
  topicsGrid->addWidget(apply_topics_btn_, 3, 0, 1, 2);

  root->addWidget(topicsBox);

  // -------- Camera --------
  auto * camBox = new QGroupBox("Camera");
  auto * camLayout = new QVBoxLayout(camBox);
  cam_label_ = new QLabel("No camera frames yet");
  cam_label_->setMinimumHeight(200);
  cam_label_->setAlignment(Qt::AlignCenter);
  cam_label_->setStyleSheet("QLabel { background: #111; color: #bbb; }");
  camLayout->addWidget(cam_label_);
  root->addWidget(camBox);

  // -------- Lidar mini-view --------
  auto * lidarBox = new QGroupBox("LiDAR (mini view)");
  auto * lidarLayout = new QVBoxLayout(lidarBox);
  lidar_widget_ = new LidarWidget();
  lidarLayout->addWidget(lidar_widget_);
  root->addWidget(lidarBox);

  // -------- Teleop --------
  auto * teleopBox = new QGroupBox("Teleop");
  auto * teleopLayout = new QVBoxLayout(teleopBox);

  enable_teleop_ = new QCheckBox("Enable teleop (deadman)");
  enable_keyboard_ = new QCheckBox("Keyboard (WASD + Space), only when panel focused");
  teleopLayout->addWidget(enable_teleop_);
  teleopLayout->addWidget(enable_keyboard_);

  auto * speeds = new QGridLayout();
  speeds->addWidget(new QLabel("Linear speed (m/s x100):"), 0, 0);
  lin_slider_ = new QSlider(Qt::Horizontal);
  lin_slider_->setRange(0, 120);   // 0.00 -> 0.80 m/s
  lin_slider_->setValue(80);      // 0.20 m/s
  speeds->addWidget(lin_slider_, 0, 1);

  speeds->addWidget(new QLabel("Angular speed (rad/s x100):"), 1, 0);
  ang_slider_ = new QSlider(Qt::Horizontal);
  ang_slider_->setRange(0, 300);  // 0.00 -> 3.00 rad/s
  ang_slider_->setValue(120);     // 1.20 rad/s
  speeds->addWidget(ang_slider_, 1, 1);

  teleopLayout->addLayout(speeds);

  auto * btnGrid = new QGridLayout();
  fwd_btn_  = new QPushButton("▲");
  back_btn_ = new QPushButton("▼");
  left_btn_ = new QPushButton("⟲");
  right_btn_= new QPushButton("⟳");
  stop_btn_ = new QPushButton("STOP");

  fwd_btn_->setAutoRepeat(false);
  back_btn_->setAutoRepeat(false);
  left_btn_->setAutoRepeat(false);
  right_btn_->setAutoRepeat(false);

  stop_btn_->setStyleSheet("QPushButton { font-weight: bold; }");

  btnGrid->addWidget(fwd_btn_, 0, 1);
  btnGrid->addWidget(left_btn_, 1, 0);
  btnGrid->addWidget(stop_btn_, 1, 1);
  btnGrid->addWidget(right_btn_, 1, 2);
  btnGrid->addWidget(back_btn_, 2, 1);

  teleopLayout->addLayout(btnGrid);
  root->addWidget(teleopBox);

  root->addStretch(1);
}

void DashboardPanel::connectSignals()
{
  connect(apply_topics_btn_, &QPushButton::released, this, &DashboardPanel::onApplyTopics);
  connect(enable_teleop_, &QCheckBox::stateChanged, this, &DashboardPanel::onEnableTeleopChanged);

  connect(fwd_btn_,  &QPushButton::pressed,  this, &DashboardPanel::onFwdPressed);
  connect(fwd_btn_,  &QPushButton::released, this, &DashboardPanel::onFwdReleased);

  connect(back_btn_, &QPushButton::pressed,  this, &DashboardPanel::onBackPressed);
  connect(back_btn_, &QPushButton::released, this, &DashboardPanel::onBackReleased);

  connect(left_btn_, &QPushButton::pressed,  this, &DashboardPanel::onLeftPressed);
  connect(left_btn_, &QPushButton::released, this, &DashboardPanel::onLeftReleased);

  connect(right_btn_, &QPushButton::pressed,  this, &DashboardPanel::onRightPressed);
  connect(right_btn_, &QPushButton::released, this, &DashboardPanel::onRightReleased);

  connect(stop_btn_, &QPushButton::released, this, &DashboardPanel::onStopClicked);

  // camera updates must land on GUI thread
  connect(this, &DashboardPanel::newCameraFrame, this, &DashboardPanel::onNewCameraFrame, Qt::QueuedConnection);
}

void DashboardPanel::makeOrRemakeRosInterfaces()
{
  if (!node_ptr_) return;
  auto node = node_ptr_->get_raw_node();

  const std::string cmd_topic = cmdvel_topic_edit_->text().toStdString();
  const std::string img_topic = image_topic_edit_->text().toStdString();
  const std::string scan_topic = scan_topic_edit_->text().toStdString();

  // (re)create publisher
  cmd_pub_ = node->create_publisher<geometry_msgs::msg::Twist>(cmd_topic, rclcpp::QoS(10));

  // (re)create subscriptions
  img_sub_.reset();
  scan_sub_.reset();

  img_sub_ = node->create_subscription<sensor_msgs::msg::Image>(
    img_topic, rclcpp::QoS(5),
    std::bind(&DashboardPanel::imageCb, this, std::placeholders::_1));

  scan_sub_ = node->create_subscription<sensor_msgs::msg::LaserScan>(
    scan_topic, rclcpp::QoS(10),
    std::bind(&DashboardPanel::scanCb, this, std::placeholders::_1));
}

void DashboardPanel::onApplyTopics()
{
  makeOrRemakeRosInterfaces();
}

void DashboardPanel::onEnableTeleopChanged(int state)
{
  const bool enabled = (state != 0);
  if (enabled) {
    teleop_timer_.start();
  } else {
    teleop_timer_.stop();
    drive_cmd_ = DriveCmd::STOP;
    publishStop();
  }
}

void DashboardPanel::publishStop()
{
  if (!cmd_pub_) return;
  geometry_msgs::msg::Twist t;
  t.linear.x = 0.0;
  t.angular.z = 0.0;
  cmd_pub_->publish(t);
}

void DashboardPanel::publishFromState()
{
  if (!cmd_pub_) return;

  geometry_msgs::msg::Twist t;
  const double lin = lin_slider_->value() / 100.0; // m/s
  const double ang = ang_slider_->value() / 100.0; // rad/s

  switch (drive_cmd_) {
    case DriveCmd::FWD:  t.linear.x =  lin; break;
    case DriveCmd::BACK: t.linear.x = -lin; break;
    case DriveCmd::LEFT: t.angular.z =  ang; break;
    case DriveCmd::RIGHT:t.angular.z = -ang; break;
    case DriveCmd::STOP: default: break;
  }
  cmd_pub_->publish(t);
}

void DashboardPanel::onTeleopTick()
{
  if (!enable_teleop_->isChecked()) return;

  // Safety: if no button pressed / no key pressed -> stop
  if (drive_cmd_ == DriveCmd::STOP) {
    publishStop();
  } else {
    publishFromState();
  }
}

// Button handlers
void DashboardPanel::onFwdPressed()   { if (enable_teleop_->isChecked()) drive_cmd_ = DriveCmd::FWD; }
void DashboardPanel::onFwdReleased()  { drive_cmd_ = DriveCmd::STOP; }
void DashboardPanel::onBackPressed()  { if (enable_teleop_->isChecked()) drive_cmd_ = DriveCmd::BACK; }
void DashboardPanel::onBackReleased() { drive_cmd_ = DriveCmd::STOP; }
void DashboardPanel::onLeftPressed()  { if (enable_teleop_->isChecked()) drive_cmd_ = DriveCmd::LEFT; }
void DashboardPanel::onLeftReleased() { drive_cmd_ = DriveCmd::STOP; }
void DashboardPanel::onRightPressed() { if (enable_teleop_->isChecked()) drive_cmd_ = DriveCmd::RIGHT; }
void DashboardPanel::onRightReleased(){ drive_cmd_ = DriveCmd::STOP; }
void DashboardPanel::onStopClicked()  { drive_cmd_ = DriveCmd::STOP; publishStop(); }

// Keyboard (only when enabled + checkbox + panel focused)
void DashboardPanel::keyPressEvent(QKeyEvent * event)
{
  if (!enable_teleop_->isChecked() || !enable_keyboard_->isChecked()) return;

  switch (event->key()) {
    case Qt::Key_W: drive_cmd_ = DriveCmd::FWD; break;
    case Qt::Key_S: drive_cmd_ = DriveCmd::BACK; break;
    case Qt::Key_A: drive_cmd_ = DriveCmd::LEFT; break;
    case Qt::Key_D: drive_cmd_ = DriveCmd::RIGHT; break;
    case Qt::Key_Space: drive_cmd_ = DriveCmd::STOP; publishStop(); break;
    default: break;
  }
}

void DashboardPanel::keyReleaseEvent(QKeyEvent * event)
{
  if (!enable_teleop_->isChecked() || !enable_keyboard_->isChecked()) return;

  // stop on release for these keys
  switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_S:
    case Qt::Key_A:
    case Qt::Key_D:
      drive_cmd_ = DriveCmd::STOP;
      break;
    default: break;
  }
}

// ROS callbacks
void DashboardPanel::scanCb(const sensor_msgs::msg::LaserScan::SharedPtr msg)
{
  if (!msg || !lidar_widget_) return;
  lidar_widget_->setScan(*msg);

  // schedule a repaint on GUI thread
  QMetaObject::invokeMethod(lidar_widget_, "update", Qt::QueuedConnection);
}

void DashboardPanel::imageCb(const sensor_msgs::msg::Image::SharedPtr msg)
{
  if (!msg) return;

  try {
    // cv_bridge handles most common encodings
    auto cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
    cv::Mat img = cv_ptr->image;

    // convert to RGB for QImage
    cv::Mat rgb;
    if (img.channels() == 1) {
      cv::cvtColor(img, rgb, cv::COLOR_GRAY2RGB);
    } else {
      cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
    }

    QImage qimg(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    emit newCameraFrame(qimg.copy()); // copy because rgb will go out of scope
  } catch (...) {
    // ignore bad frames
  }
}

void DashboardPanel::onNewCameraFrame(const QImage & img)
{
  if (!cam_label_) return;
  cam_label_->setPixmap(QPixmap::fromImage(img).scaled(
    cam_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}  // namespace pioneer_dashboard_rviz

PLUGINLIB_EXPORT_CLASS(pioneer_dashboard_rviz::DashboardPanel, rviz_common::Panel)
