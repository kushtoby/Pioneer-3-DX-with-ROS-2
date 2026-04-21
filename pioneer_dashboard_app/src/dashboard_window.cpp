#include "pioneer_dashboard_app/dashboard_window.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QFrame>
#include <QMetaObject>

#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgproc.hpp>

namespace pioneer_dashboard_app {

// ---------- VideoCanvas ----------
VideoCanvas::VideoCanvas(QWidget* parent) : QWidget(parent) {
  setMinimumSize(640, 360);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoCanvas::setFrontFrame(const QImage& img) {
  std::lock_guard<std::mutex> lock(m_);
  front_ = img;
  update();
}

void VideoCanvas::setRearFrame(const QImage& img) {
  std::lock_guard<std::mutex> lock(m_);
  rear_ = img;
  update();
}

void VideoCanvas::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), Qt::black);

  QImage front, rear;
  {
    std::lock_guard<std::mutex> lock(m_);
    front = front_;
    rear  = rear_;
  }

  if (front.isNull()) {
    p.setPen(Qt::gray);
    p.drawText(rect(), Qt::AlignCenter, "No front camera frames");
    return;
  }

  // draw main (front) scaled to fit
  p.drawImage(rect(), front);

  // PiP rear (top-right)
  if (pip_enabled_ && !rear.isNull()) {
    const int pipW = int(width() * pip_scale_);
    const int pipH = int(pipW * (rear.height() / double(rear.width())));
    const int margin = 12;
    QRect pipRect(width() - pipW - margin, margin, pipW, pipH);

    p.drawImage(pipRect, rear);
    p.setPen(QPen(Qt::white, 2));
    p.drawRect(pipRect);
  }
}

// ---------- DashboardWindow ----------
DashboardWindow::DashboardWindow(std::shared_ptr<rclcpp::Node> node)
: node_(std::move(node)) {
  setupUi();
  setupRos();

  teleop_timer_.setInterval(50); // 20 Hz
  connect(&teleop_timer_, &QTimer::timeout, this, &DashboardWindow::onTeleopTick);
}

void DashboardWindow::setupUi() {
  QWidget* central = new QWidget(this);
  setCentralWidget(central);

  auto* root = new QHBoxLayout(central);

  // Left control column
  auto* leftCol = new QVBoxLayout();
  leftCol->setContentsMargins(0,0,0,0);

  // Topics box
  auto* topicsBox = new QGroupBox("Topics");
  auto* tg = new QGridLayout(topicsBox);

  front_topic_ = new QLineEdit("/oak/rgb/image_raw");
  rear_topic_  = new QLineEdit("/rear/image_raw"); // change to your rear cam topic later
  scan_topic_  = new QLineEdit("/scan");
  cmdvel_topic_= new QLineEdit("/cmd_vel");

  tg->addWidget(new QLabel("Front image:"), 0,0); tg->addWidget(front_topic_, 0,1);
  tg->addWidget(new QLabel("Rear image:"),  1,0); tg->addWidget(rear_topic_,  1,1);
  tg->addWidget(new QLabel("Scan:"),        2,0); tg->addWidget(scan_topic_,  2,1);
  tg->addWidget(new QLabel("cmd_vel:"),     3,0); tg->addWidget(cmdvel_topic_,3,1);

  apply_ = new QPushButton("Apply / Reconnect");
  tg->addWidget(apply_, 4,0, 1,2);
  connect(apply_, &QPushButton::clicked, this, &DashboardWindow::onApplyTopics);

  leftCol->addWidget(topicsBox);

  // Teleop box
  auto* teleBox = new QGroupBox("Teleop");
  auto* tv = new QVBoxLayout(teleBox);

  enable_teleop_ = new QCheckBox("Enable teleop (deadman)");
  tv->addWidget(enable_teleop_);
  connect(enable_teleop_, &QCheckBox::stateChanged, this, [this](int st){
    if (st) teleop_timer_.start();
    else { teleop_timer_.stop(); drive_ = DriveCmd::STOP; publishStop(); }
  });

  auto* sg = new QGridLayout();
  lin_slider_ = new QSlider(Qt::Horizontal);
  lin_slider_->setRange(0, 80);  lin_slider_->setValue(20); // 0.20 m/s
  ang_slider_ = new QSlider(Qt::Horizontal);
  ang_slider_->setRange(0, 300); ang_slider_->setValue(120); // 1.20 rad/s
  sg->addWidget(new QLabel("Linear (m/s x100)"),0,0); sg->addWidget(lin_slider_,0,1);
  sg->addWidget(new QLabel("Angular (rad/s x100)"),1,0); sg->addWidget(ang_slider_,1,1);
  tv->addLayout(sg);

  auto* bg = new QGridLayout();
  fwd_ = new QPushButton("▲");
  back_= new QPushButton("▼");
  left_= new QPushButton("⟲");
  right_=new QPushButton("⟳");
  stop_= new QPushButton("STOP");
  stop_->setStyleSheet("QPushButton{font-weight:bold;}");

  torch_btn_ = new QPushButton("TORCH");
  torch_btn_->setToolTip("Pulse OD6 relay (~200 ms) to toggle the torch");
  tv->addWidget(torch_btn_);

  bg->addWidget(fwd_, 0,1);
  bg->addWidget(left_,1,0);
  bg->addWidget(stop_,1,1);
  bg->addWidget(right_,1,2);
  bg->addWidget(back_,2,1);
  tv->addLayout(bg);

  connect(fwd_,  &QPushButton::pressed,  this, &DashboardWindow::onFwdPressed);
  connect(fwd_,  &QPushButton::released, this, &DashboardWindow::onFwdReleased);
  connect(back_, &QPushButton::pressed,  this, &DashboardWindow::onBackPressed);
  connect(back_, &QPushButton::released, this, &DashboardWindow::onBackReleased);
  connect(left_, &QPushButton::pressed,  this, &DashboardWindow::onLeftPressed);
  connect(left_, &QPushButton::released, this, &DashboardWindow::onLeftReleased);
  connect(right_,&QPushButton::pressed,  this, &DashboardWindow::onRightPressed);
  connect(right_,&QPushButton::released, this, &DashboardWindow::onRightReleased);
  connect(stop_, &QPushButton::clicked,  this, &DashboardWindow::onStopClicked);

  connect(torch_btn_, &QPushButton::clicked, this, [this](){
    if (!torch_client_) return;

    if (!torch_client_->service_is_ready()) {
      // Optional: print warning
      RCLCPP_WARN(node_->get_logger(), "Torch service not available: /pioneer_base/torch_pulse");
      return;
    }

    auto req = std::make_shared<std_srvs::srv::Trigger::Request>();
    torch_client_->async_send_request(req);
  });

  leftCol->addWidget(teleBox);

  // LiDAR widget
  auto* lidarBox = new QGroupBox("LiDAR (mini)");
  auto* lv = new QVBoxLayout(lidarBox);
  lidar_ = new LidarWidget();
  lv->addWidget(lidar_);
  leftCol->addWidget(lidarBox);

  leftCol->addStretch(1);

  // Right: big video
  video_ = new VideoCanvas();
  root->addLayout(leftCol, 0);
  root->addWidget(video_, 1);

  setWindowTitle("Pioneer Dashboard (Front + Rear PiP + LiDAR + Teleop)");
  resize(1400, 800);

  QTimer::singleShot(0, this, SLOT(onApplyTopics()));
}

void DashboardWindow::setupRos() {
  // nothing needed here (or keep ROS setup that doesn't subscribe)
  //onApplyTopics();
}

static QImage cvMatToQImageRGB(const cv::Mat& bgr) {
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}

void DashboardWindow::frontImgCb(const sensor_msgs::msg::Image::SharedPtr msg) {
  try {
    RCLCPP_INFO_THROTTLE(
      node_->get_logger(), *node_->get_clock(), 1000,
      "frontImgCb firing: %ux%u enc=%s step=%u",
      msg->width, msg->height, msg->encoding.c_str(), msg->step);

    auto cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
    cv::Mat img = cv_ptr->image;

    if (msg->encoding == "rgb8") cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
    if (img.channels() == 1) cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);

    const QImage qimg = cvMatToQImageRGB(img); // already .copy() inside

    // IMPORTANT: update Qt widget on GUI thread
    QMetaObject::invokeMethod(video_, [this, qimg]() {
      video_->setFrontFrame(qimg);
    }, Qt::QueuedConnection);

  } catch (const std::exception& e) {
    RCLCPP_WARN(node_->get_logger(), "frontImgCb exception: %s", e.what());
  } catch (...) {
    RCLCPP_WARN(node_->get_logger(), "frontImgCb unknown exception");
  }
}

void DashboardWindow::rearImgCb(const sensor_msgs::msg::Image::SharedPtr msg) {
  try {
    RCLCPP_INFO_THROTTLE(
      node_->get_logger(), *node_->get_clock(), 1000,
      "rearImgCb firing: %ux%u enc=%s",
      msg->width, msg->height, msg->encoding.c_str());

    auto cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
    cv::Mat img = cv_ptr->image;

    if (msg->encoding == "rgb8") cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
    if (img.channels() == 1) cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);

    const QImage qimg = cvMatToQImageRGB(img);

    QMetaObject::invokeMethod(video_, [this, qimg]() {
      video_->setRearFrame(qimg);
    }, Qt::QueuedConnection);

  } catch (const std::exception& e) {
    RCLCPP_WARN(node_->get_logger(), "rearImgCb exception: %s", e.what());
  } catch (...) {
    RCLCPP_WARN(node_->get_logger(), "rearImgCb unknown exception");
  }
}

void DashboardWindow::scanCb(const sensor_msgs::msg::LaserScan::SharedPtr msg) {
  if (msg) lidar_->setScan(*msg);
}

void DashboardWindow::onApplyTopics() {
  // Create publisher
  cmd_pub_ = node_->create_publisher<geometry_msgs::msg::Twist>(cmdvel_topic_->text().toStdString(), rclcpp::QoS(10));

  torch_client_ = node_->create_client<std_srvs::srv::Trigger>("/pioneer_base/torch_pulse");

  auto image_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();
  auto sensor_qos = rclcpp::SensorDataQoS();
  
  front_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
    front_topic_->text().toStdString(), image_qos,
    std::bind(&DashboardWindow::frontImgCb, this, std::placeholders::_1));

  rear_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
    rear_topic_->text().toStdString(), image_qos,
    std::bind(&DashboardWindow::rearImgCb, this, std::placeholders::_1));
    
  scan_sub_ = node_->create_subscription<sensor_msgs::msg::LaserScan>(
    scan_topic_->text().toStdString(), sensor_qos,
    std::bind(&DashboardWindow::scanCb, this, std::placeholders::_1));
}

void DashboardWindow::publishStop() {
  if (!cmd_pub_) return;
  geometry_msgs::msg::Twist t;
  cmd_pub_->publish(t);
}

void DashboardWindow::publishFromState() {
  if (!cmd_pub_) return;
  geometry_msgs::msg::Twist t;
  const double lin = lin_slider_->value()/100.0;
  const double ang = ang_slider_->value()/100.0;

  switch (drive_) {
    case DriveCmd::FWD:  t.linear.x =  lin; break;
    case DriveCmd::BACK: t.linear.x = -lin; break;
    case DriveCmd::LEFT: t.angular.z =  ang; break;
    case DriveCmd::RIGHT:t.angular.z = -ang; break;
    default: break;
  }
  cmd_pub_->publish(t);
}

void DashboardWindow::onTeleopTick() {
  if (!enable_teleop_->isChecked()) return;
  if (drive_ == DriveCmd::STOP) publishStop();
  else publishFromState();
}

void DashboardWindow::onFwdPressed()   { if (enable_teleop_->isChecked()) drive_ = DriveCmd::FWD; }
void DashboardWindow::onBackPressed()  { if (enable_teleop_->isChecked()) drive_ = DriveCmd::BACK; }
void DashboardWindow::onLeftPressed()  { if (enable_teleop_->isChecked()) drive_ = DriveCmd::LEFT; }
void DashboardWindow::onRightPressed() { if (enable_teleop_->isChecked()) drive_ = DriveCmd::RIGHT; }

void DashboardWindow::onFwdReleased()  { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onBackReleased() { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onLeftReleased() { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onRightReleased(){ drive_ = DriveCmd::STOP; publishStop(); }

void DashboardWindow::onStopClicked()  { drive_ = DriveCmd::STOP; publishStop(); }

} // namespace