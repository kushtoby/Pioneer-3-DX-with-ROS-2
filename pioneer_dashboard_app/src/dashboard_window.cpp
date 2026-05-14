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

  p.drawImage(rect(), front);

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

  teleop_timer_.setInterval(50);
  connect(&teleop_timer_, &QTimer::timeout, this, &DashboardWindow::onTeleopTick);

  render_timer_.setInterval(33);
  connect(&render_timer_, &QTimer::timeout, this, &DashboardWindow::onRenderTick);
  render_timer_.start();

  // Joystick init
  joy_fd_ = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
  if (joy_fd_ >= 0) {
    joy_status_->setText("🎮 Joystick connected");
    joy_status_->setStyleSheet("QLabel{color:#00cc44;font-weight:bold;}");
    joy_running_ = true;
    joy_thread_ = std::thread(&DashboardWindow::joystickThreadFn, this);
    RCLCPP_INFO(node_->get_logger(), "Joystick opened at /dev/input/js0");
  } else {
    joy_status_->setText("No joystick");
    joy_status_->setStyleSheet("QLabel{color:#888;}");
    RCLCPP_INFO(node_->get_logger(), "No joystick at /dev/input/js0 — GUI only");
  }
}

DashboardWindow::~DashboardWindow() {
  joy_running_ = false;
  if (joy_fd_ >= 0) { ::close(joy_fd_); joy_fd_ = -1; }
  if (joy_thread_.joinable()) joy_thread_.join();
}

void DashboardWindow::setupUi() {
  QWidget* central = new QWidget(this);
  setCentralWidget(central);

  auto* root = new QHBoxLayout(central);

  auto* leftCol = new QVBoxLayout();
  leftCol->setContentsMargins(0, 0, 0, 0);

  // Topics box
  auto* topicsBox = new QGroupBox("Topics");
  auto* tg = new QGridLayout(topicsBox);

  front_topic_  = new QLineEdit("/oak/rgb/image_raw/compressed");
  rear_topic_   = new QLineEdit("/rear/image_raw");
  scan_topic_   = new QLineEdit("/scan");
  cmdvel_topic_ = new QLineEdit("/cmd_vel");

  tg->addWidget(new QLabel("Front image:"), 0, 0); tg->addWidget(front_topic_,  0, 1);
  tg->addWidget(new QLabel("Rear image:"),  1, 0); tg->addWidget(rear_topic_,   1, 1);
  tg->addWidget(new QLabel("Scan:"),        2, 0); tg->addWidget(scan_topic_,   2, 1);
  tg->addWidget(new QLabel("cmd_vel:"),     3, 0); tg->addWidget(cmdvel_topic_, 3, 1);

  apply_ = new QPushButton("Apply / Reconnect");
  tg->addWidget(apply_, 4, 0, 1, 2);
  connect(apply_, &QPushButton::clicked, this, &DashboardWindow::onApplyTopics);

  leftCol->addWidget(topicsBox);

  // Joystick status label
  joy_status_ = new QLabel("No joystick");
  joy_status_->setStyleSheet("QLabel{color:#888;}");
  leftCol->addWidget(joy_status_);

  // Teleop box
  auto* teleBox = new QGroupBox("Teleop");
  auto* tv = new QVBoxLayout(teleBox);

  enable_teleop_ = new QCheckBox("Enable teleop (deadman)");
  tv->addWidget(enable_teleop_);
  connect(enable_teleop_, &QCheckBox::stateChanged, this, [this](int st) {
    if (st) teleop_timer_.start();
    else { teleop_timer_.stop(); drive_ = DriveCmd::STOP; publishStop(); }
  });

  auto* sg = new QGridLayout();
  lin_slider_ = new QSlider(Qt::Horizontal);
  lin_slider_->setRange(0, 120);
  lin_slider_->setValue(80);
  ang_slider_ = new QSlider(Qt::Horizontal);
  ang_slider_->setRange(0, 300);
  ang_slider_->setValue(120);
  sg->addWidget(new QLabel("Linear (m/s x100)  [🎮 L1/R1]"),    0, 0); sg->addWidget(lin_slider_, 0, 1);
  sg->addWidget(new QLabel("Angular (rad/s x100)  [🎮 L2/R2]"), 1, 0); sg->addWidget(ang_slider_, 1, 1);
  tv->addLayout(sg);

  // Torch button
  torch_btn_ = new QPushButton("TORCH ON");
  torch_btn_->setToolTip("Click to turn torch on/off");
  setTorchButtonState(false);
  tv->addWidget(torch_btn_);

  connect(torch_btn_, &QPushButton::clicked, this, [this]() {
    if (!torch_is_on_) {
      if (!torch_client_) return;
      RCLCPP_INFO(node_->get_logger(), "TORCH: sending ON pulse");
      torch_client_->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>());
      torch_is_on_ = true;
      setTorchButtonState(true);
    } else {
      if (!torch_off_client_) return;
      RCLCPP_INFO(node_->get_logger(), "TORCH: sending OFF sequence");
      torch_off_client_->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>());
      torch_is_on_ = false;
      setTorchButtonState(false);
    }
  });

  // Recording button
  rec_btn_ = new QPushButton("⏺  START TRIAL");
  rec_btn_->setToolTip("Start/stop LiDAR rosbag recording and transfer to laptop");
  setRecordingState(false);
  tv->addWidget(rec_btn_);

  connect(rec_btn_, &QPushButton::clicked, this, [this]() {
    if (!recording_) {
      if (!rec_start_client_) return;
      RCLCPP_INFO(node_->get_logger(), "BAG: sending start_recording");
      rec_start_client_->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>());
      recording_ = true;
      setRecordingState(true);
    } else {
      if (!rec_stop_client_) return;
      RCLCPP_INFO(node_->get_logger(), "BAG: sending stop_recording");
      rec_stop_client_->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>());
      recording_ = false;
      setRecordingState(false);
    }
  });

  // Drive buttons
  auto* bg = new QGridLayout();
  fwd_   = new QPushButton("▲");
  back_  = new QPushButton("▼");
  left_  = new QPushButton("⟲");
  right_ = new QPushButton("⟳");
  stop_  = new QPushButton("STOP  [🎮 ○]");
  stop_->setStyleSheet("QPushButton { font-weight: bold; }");

  bg->addWidget(fwd_,   0, 1);
  bg->addWidget(left_,  1, 0);
  bg->addWidget(stop_,  1, 1);
  bg->addWidget(right_, 1, 2);
  bg->addWidget(back_,  2, 1);
  tv->addLayout(bg);

  connect(fwd_,   &QPushButton::pressed,  this, &DashboardWindow::onFwdPressed);
  connect(fwd_,   &QPushButton::released, this, &DashboardWindow::onFwdReleased);
  connect(back_,  &QPushButton::pressed,  this, &DashboardWindow::onBackPressed);
  connect(back_,  &QPushButton::released, this, &DashboardWindow::onBackReleased);
  connect(left_,  &QPushButton::pressed,  this, &DashboardWindow::onLeftPressed);
  connect(left_,  &QPushButton::released, this, &DashboardWindow::onLeftReleased);
  connect(right_, &QPushButton::pressed,  this, &DashboardWindow::onRightPressed);
  connect(right_, &QPushButton::released, this, &DashboardWindow::onRightReleased);
  connect(stop_,  &QPushButton::clicked,  this, &DashboardWindow::onStopClicked);

  leftCol->addWidget(teleBox);

  // LiDAR widget
  auto* lidarBox = new QGroupBox("LiDAR (mini)");
  auto* lv = new QVBoxLayout(lidarBox);
  lidar_ = new LidarWidget();
  lv->addWidget(lidar_);
  leftCol->addWidget(lidarBox);

  leftCol->addStretch(1);

  video_ = new VideoCanvas();
  root->addLayout(leftCol, 0);
  root->addWidget(video_, 1);

  setWindowTitle("Pioneer Dashboard");
  resize(1400, 800);

  QTimer::singleShot(0, this, SLOT(onApplyTopics()));
}

void DashboardWindow::setTorchButtonState(bool is_on) {
  if (is_on) {
    torch_btn_->setText("TORCH OFF");
    torch_btn_->setStyleSheet(
      "QPushButton { background-color: #c0392b; color: white; font-weight: bold; }");
  } else {
    torch_btn_->setText("TORCH ON");
    torch_btn_->setStyleSheet(
      "QPushButton { background-color: #27ae60; color: white; font-weight: bold; }");
  }
}

void DashboardWindow::setRecordingState(bool is_recording) {
  if (is_recording) {
    rec_btn_->setText("⏹  STOP TRIAL");
    rec_btn_->setStyleSheet(
      "QPushButton { background-color: #c0392b; color: white; font-weight: bold; }");
  } else {
    rec_btn_->setText("⏺  START TRIAL");
    rec_btn_->setStyleSheet(
      "QPushButton { background-color: #2980b9; color: white; font-weight: bold; }");
  }
}

void DashboardWindow::setupRos() {
  // intentionally empty
}

static QImage cvMatToQImageRGB(const cv::Mat& bgr) {
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}

void DashboardWindow::frontImgCb(const sensor_msgs::msg::Image::SharedPtr msg) {
  if (!msg) return;
  try {
    RCLCPP_INFO_THROTTLE(node_->get_logger(), *node_->get_clock(), 1000,
      "frontImgCb: %ux%u enc=%s", msg->width, msg->height, msg->encoding.c_str());

    auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
    const QImage qimg = cvMatToQImageRGB(cv_ptr->image);

    { std::lock_guard<std::mutex> lk(img_mtx_); latest_front_ = qimg; }
    new_front_.store(true, std::memory_order_release);

  } catch (const std::exception& e) {
    RCLCPP_WARN(node_->get_logger(), "frontImgCb exception: %s", e.what());
  } catch (...) {
    RCLCPP_WARN(node_->get_logger(), "frontImgCb unknown exception");
  }
}

void DashboardWindow::frontCompCb(const sensor_msgs::msg::CompressedImage::SharedPtr msg) {
  if (!msg || msg->data.empty()) return;

  QImage img;
  img.loadFromData(msg->data.data(), static_cast<int>(msg->data.size()));
  if (img.isNull()) {
    RCLCPP_WARN_THROTTLE(node_->get_logger(), *node_->get_clock(), 2000,
      "frontCompCb: QImage decode failed (format=%s)", msg->format.c_str());
    return;
  }
  if (img.format() != QImage::Format_RGB888 && img.format() != QImage::Format_BGR888)
    img = img.convertToFormat(QImage::Format_RGB888);

  { std::lock_guard<std::mutex> lk(img_mtx_); latest_front_ = img; }
  new_front_.store(true, std::memory_order_release);
}

void DashboardWindow::rearImgCb(const sensor_msgs::msg::Image::SharedPtr msg) {
  if (!msg) return;
  try {
    RCLCPP_INFO_THROTTLE(node_->get_logger(), *node_->get_clock(), 1000,
      "rearImgCb: %ux%u enc=%s", msg->width, msg->height, msg->encoding.c_str());

    auto cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
    cv::Mat img = cv_ptr->image;
    if (msg->encoding == "rgb8") cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
    if (img.channels() == 1)    cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);

    const QImage qimg = cvMatToQImageRGB(img);
    { std::lock_guard<std::mutex> lk(img_mtx_); latest_rear_ = qimg; }
    new_rear_.store(true, std::memory_order_release);

  } catch (const std::exception& e) {
    RCLCPP_WARN(node_->get_logger(), "rearImgCb exception: %s", e.what());
  } catch (...) {
    RCLCPP_WARN(node_->get_logger(), "rearImgCb unknown exception");
  }
}

void DashboardWindow::scanCb(const sensor_msgs::msg::LaserScan::SharedPtr msg) {
  if (!msg) return;
  { std::lock_guard<std::mutex> lk(scan_mtx_); latest_scan_ = *msg; }
  new_scan_.store(true, std::memory_order_release);
}

void DashboardWindow::onRenderTick() {
  QImage f, r;
  sensor_msgs::msg::LaserScan scan;
  bool have_scan = false;

  {
    std::lock_guard<std::mutex> lk(img_mtx_);
    if (new_front_.load(std::memory_order_acquire)) {
      f = latest_front_;
      new_front_.store(false, std::memory_order_release);
    }
    if (new_rear_.load(std::memory_order_acquire)) {
      r = latest_rear_;
      new_rear_.store(false, std::memory_order_release);
    }
  }
  {
    std::lock_guard<std::mutex> lk(scan_mtx_);
    if (new_scan_.load(std::memory_order_acquire)) {
      scan = latest_scan_;
      new_scan_.store(false, std::memory_order_release);
      have_scan = true;
    }
  }

  if (!f.isNull()) video_->setFrontFrame(f);
  if (!r.isNull()) video_->setRearFrame(r);
  if (have_scan)   lidar_->setScan(scan);
}

void DashboardWindow::onApplyTopics() {
  front_sub_.reset();
  front_comp_sub_.reset();
  rear_sub_.reset();
  scan_sub_.reset();
  cmd_pub_.reset();
  torch_client_.reset();
  torch_off_client_.reset();
  rec_start_client_.reset();
  rec_stop_client_.reset();

  cmd_pub_ = node_->create_publisher<geometry_msgs::msg::Twist>(
    cmdvel_topic_->text().toStdString(), rclcpp::QoS(10));

  torch_client_     = node_->create_client<std_srvs::srv::Trigger>("/pioneer_base/torch_pulse");
  torch_off_client_ = node_->create_client<std_srvs::srv::Trigger>("/pioneer_base/torch_off");
  rec_start_client_ = node_->create_client<std_srvs::srv::Trigger>("/bag_recorder/start_recording");
  rec_stop_client_  = node_->create_client<std_srvs::srv::Trigger>("/bag_recorder/stop_recording");

  auto image_qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable();
  auto scan_qos  = rclcpp::QoS(rclcpp::KeepLast(1)).reliable();

  const std::string front_topic = front_topic_->text().toStdString();
  const bool is_compressed =
    front_topic.size() >= 11 &&
    front_topic.rfind("/compressed") == front_topic.size() - 11;

  if (is_compressed) {
    front_comp_sub_ = node_->create_subscription<sensor_msgs::msg::CompressedImage>(
      front_topic, image_qos,
      std::bind(&DashboardWindow::frontCompCb, this, std::placeholders::_1));
    RCLCPP_INFO(node_->get_logger(), "Front: CompressedImage @ %s", front_topic.c_str());
  } else {
    front_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
      front_topic, image_qos,
      std::bind(&DashboardWindow::frontImgCb, this, std::placeholders::_1));
    RCLCPP_INFO(node_->get_logger(), "Front: Image @ %s", front_topic.c_str());
  }

  rear_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
    rear_topic_->text().toStdString(), image_qos,
    std::bind(&DashboardWindow::rearImgCb, this, std::placeholders::_1));

  scan_sub_ = node_->create_subscription<sensor_msgs::msg::LaserScan>(
    scan_topic_->text().toStdString(), scan_qos,
    std::bind(&DashboardWindow::scanCb, this, std::placeholders::_1));
}

void DashboardWindow::publishStop() {
  if (!cmd_pub_) return;
  cmd_pub_->publish(geometry_msgs::msg::Twist());
}

void DashboardWindow::publishFromState() {
  if (!cmd_pub_) return;
  geometry_msgs::msg::Twist t;
  const double lin = lin_slider_->value() / 100.0;
  const double ang = ang_slider_->value() / 100.0;
  switch (drive_) {
    case DriveCmd::FWD:   t.linear.x  =  lin; break;
    case DriveCmd::BACK:  t.linear.x  = -lin; break;
    case DriveCmd::LEFT:  t.angular.z =  ang; break;
    case DriveCmd::RIGHT: t.angular.z = -ang; break;
    default: break;
  }
  cmd_pub_->publish(t);
}

void DashboardWindow::onTeleopTick() {
  if (!enable_teleop_->isChecked()) return;
  if (drive_ == DriveCmd::STOP) publishStop();
  else publishFromState();
}

void DashboardWindow::onFwdPressed()    { if (enable_teleop_->isChecked()) drive_ = DriveCmd::FWD; }
void DashboardWindow::onBackPressed()   { if (enable_teleop_->isChecked()) drive_ = DriveCmd::BACK; }
void DashboardWindow::onLeftPressed()   { if (enable_teleop_->isChecked()) drive_ = DriveCmd::LEFT; }
void DashboardWindow::onRightPressed()  { if (enable_teleop_->isChecked()) drive_ = DriveCmd::RIGHT; }

void DashboardWindow::onFwdReleased()   { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onBackReleased()  { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onLeftReleased()  { drive_ = DriveCmd::STOP; publishStop(); }
void DashboardWindow::onRightReleased() { drive_ = DriveCmd::STOP; publishStop(); }

void DashboardWindow::onStopClicked()   { drive_ = DriveCmd::STOP; publishStop(); }

// -------- Joystick thread --------
void DashboardWindow::joystickThreadFn() {
  struct js_event event;
  while (joy_running_) {
    ssize_t n = read(joy_fd_, &event, sizeof(event));
    if (n != sizeof(event)) {
      if (errno != EAGAIN) {
        RCLCPP_WARN(node_->get_logger(), "Joystick disconnected or read error");
        QMetaObject::invokeMethod(joy_status_, [this]() {
          joy_status_->setText("Joystick disconnected");
          joy_status_->setStyleSheet("QLabel{color:#cc4400;font-weight:bold;}");
        }, Qt::QueuedConnection);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    if (event.type & JS_EVENT_INIT) continue;

    if (event.type == JS_EVENT_AXIS) {
      const int val = static_cast<int>(event.value);

      if (event.number == JOY_AXIS_LY) {
        DriveCmd cmd = DriveCmd::STOP;
        if (val < -JOY_DEADZONE)     cmd = DriveCmd::FWD;
        else if (val > JOY_DEADZONE) cmd = DriveCmd::BACK;
        if (cmd != joy_last_drive_) {
          joy_last_drive_ = cmd;
          QMetaObject::invokeMethod(this, [this, cmd]() { onJoyDrive(cmd); }, Qt::QueuedConnection);
        }
      } else if (event.number == JOY_AXIS_LX) {
        // Y-axis takes priority — don't override an active fwd/back
        if (joy_last_drive_ == DriveCmd::FWD || joy_last_drive_ == DriveCmd::BACK) continue;
        DriveCmd cmd = DriveCmd::STOP;
        if (val < -JOY_DEADZONE)     cmd = DriveCmd::LEFT;
        else if (val > JOY_DEADZONE) cmd = DriveCmd::RIGHT;
        if (cmd != joy_last_drive_) {
          joy_last_drive_ = cmd;
          QMetaObject::invokeMethod(this, [this, cmd]() { onJoyDrive(cmd); }, Qt::QueuedConnection);
        }
      }
    } else if (event.type == JS_EVENT_BUTTON && event.value == 1) {
      switch (event.number) {
        case JOY_BTN_X:
          QMetaObject::invokeMethod(this, [this]() { onJoyTorch(); },      Qt::QueuedConnection); break;
        case JOY_BTN_O:
          QMetaObject::invokeMethod(this, [this]() { onJoyStop(); },       Qt::QueuedConnection); break;
        case JOY_BTN_SQ:
          QMetaObject::invokeMethod(this, [this]() { onJoyStartTrial(); }, Qt::QueuedConnection); break;
        case JOY_BTN_TRI:
          QMetaObject::invokeMethod(this, [this]() { onJoyReconnect(); },  Qt::QueuedConnection); break;
        case JOY_BTN_L1:
          QMetaObject::invokeMethod(this, [this]() { onJoyLinStep(-5); },  Qt::QueuedConnection); break;
        case JOY_BTN_R1:
          QMetaObject::invokeMethod(this, [this]() { onJoyLinStep(+5); },  Qt::QueuedConnection); break;
        case JOY_BTN_L2:
          QMetaObject::invokeMethod(this, [this]() { onJoyAngStep(-5); },  Qt::QueuedConnection); break;
        case JOY_BTN_R2:
          QMetaObject::invokeMethod(this, [this]() { onJoyAngStep(+5); },  Qt::QueuedConnection); break;
        default: break;
      }
    }
  }
}

void DashboardWindow::onJoyDrive(DriveCmd cmd) {
  if (!enable_teleop_->isChecked()) return;
  drive_ = cmd;
  fwd_->setDown(cmd == DriveCmd::FWD);
  back_->setDown(cmd == DriveCmd::BACK);
  left_->setDown(cmd == DriveCmd::LEFT);
  right_->setDown(cmd == DriveCmd::RIGHT);
}

void DashboardWindow::onJoyLinStep(int delta) {
  lin_slider_->setValue(lin_slider_->value() + delta);
}

void DashboardWindow::onJoyAngStep(int delta) {
  ang_slider_->setValue(ang_slider_->value() + delta);
}

void DashboardWindow::onJoyStop() {
  drive_ = DriveCmd::STOP;
  joy_last_drive_ = DriveCmd::STOP;
  fwd_->setDown(false); back_->setDown(false);
  left_->setDown(false); right_->setDown(false);
  publishStop();
}

void DashboardWindow::onJoyTorch() {
  // Simulate a click — reuses all existing torch toggle logic and button styling
  torch_btn_->click();
}

void DashboardWindow::onJoyReconnect() {
  onApplyTopics();
}

void DashboardWindow::onJoyStartTrial() {
  // Simulate a click on the recording button — toggles start/stop just like the GUI button
  rec_btn_->click();
}

} // namespace pioneer_dashboard_app
