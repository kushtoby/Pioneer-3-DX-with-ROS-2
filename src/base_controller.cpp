#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <Aria.h>

#include <std_srvs/srv/trigger.hpp>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <functional>

#include <cmath>

class PioneerBaseNode : public rclcpp::Node
{
public:
  explicit PioneerBaseNode(ArRobot* robot)
  : Node("pioneer_base"), robot_(robot), worker_stop_(false)
  {
    using std::placeholders::_1;
    using std::placeholders::_2;

    cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", 10,
      std::bind(&PioneerBaseNode::cmdVelCallback, this, _1));

    torch_od_pin_   = this->declare_parameter<int>("torch_od_pin", 6);
    torch_pulse_ms_ = this->declare_parameter<int>("torch_pulse_ms", 300);
    torch_gap_ms_   = this->declare_parameter<int>("torch_gap_ms", 500);

    torch_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_pulse",
      std::bind(&PioneerBaseNode::torchPulseCb, this, _1, _2));

    torch_off_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_off",
      std::bind(&PioneerBaseNode::torchOffCb, this, _1, _2));

    // Start the dedicated worker thread
    worker_thread_ = std::thread(&PioneerBaseNode::workerLoop, this);
  }

  ~PioneerBaseNode()
  {
    {
      std::lock_guard<std::mutex> lk(queue_mtx_);
      worker_stop_ = true;
    }
    queue_cv_.notify_one();
    if (worker_thread_.joinable()) worker_thread_.join();
  }

private:
  // ----------------------------------------------------------------
  // Worker thread - runs independently of the ROS executor
  // ----------------------------------------------------------------
  void workerLoop()
  {
    while (true) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lk(queue_mtx_);
        queue_cv_.wait(lk, [this] {
          return worker_stop_ || !job_queue_.empty();
        });
        if (worker_stop_ && job_queue_.empty()) break;
        job = std::move(job_queue_.front());
        job_queue_.pop();
      }
      job();
    }
  }

  void enqueueJob(std::function<void()> f)
  {
    {
      std::lock_guard<std::mutex> lk(queue_mtx_);
      while (!job_queue_.empty()) job_queue_.pop();
      job_queue_.push(std::move(f));
    }
    queue_cv_.notify_one();
  }

  // ----------------------------------------------------------------
  // Raw DIGOUT helpers (called only from worker thread)
  // ----------------------------------------------------------------
  void pinHigh(int bit)
  {
    std::lock_guard<std::mutex> g(digout_mtx_);
    digout_mask_ |= bit;
    // High byte = mask (which bits to touch), low byte = value (what to set them to)
    int digout_arg = (bit << 8) | digout_mask_;
    robot_->lock();
    robot_->comInt(ArCommands::DIGOUT, digout_arg);
    robot_->unlock();
    RCLCPP_INFO(this->get_logger(), "DIGOUT HIGH arg=0x%04x mask=0x%02x", digout_arg, digout_mask_);
  }

  void pinLow(int bit)
  {
    std::lock_guard<std::mutex> g(digout_mtx_);
    digout_mask_ &= ~bit;
    // High byte = mask (which bits to touch), low byte = value (0 to clear)
    int digout_arg = (bit << 8) | digout_mask_;
    robot_->lock();
    robot_->comInt(ArCommands::DIGOUT, digout_arg);
    robot_->unlock();
    RCLCPP_INFO(this->get_logger(), "DIGOUT LOW  arg=0x%04x mask=0x%02x", digout_arg, digout_mask_);
  }   

  // ----------------------------------------------------------------
  // cmd_vel
  // ----------------------------------------------------------------
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
  {
    if (!robot_->isConnected()) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "Robot not connected; ignoring cmd_vel");
      return;
    }
    robot_->lock();
    robot_->setVel(msg->linear.x * 1000.0);
    robot_->setRotVel(msg->angular.z * 180.0 / M_PI);
    robot_->unlock();
  }

  // ----------------------------------------------------------------
  // torch_pulse: single pulse (turns light ON)
  // ----------------------------------------------------------------
  void torchPulseCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (!robot_ || !robot_->isConnected()) {
      res->success = false;
      res->message = "Robot not connected";
      return;
    }

    const int bit      = (1 << torch_od_pin_);
    const int pulse_ms = torch_pulse_ms_;

    enqueueJob([this, bit, pulse_ms]() {
      pinHigh(bit);
      std::this_thread::sleep_for(std::chrono::milliseconds(pulse_ms));
      pinLow(bit);
    });

    res->success = true;
    res->message = "Torch pulse queued";
  }

  // ----------------------------------------------------------------
  // torch_off: 3 pulses to cycle Low -> Strobe -> Off
  // ----------------------------------------------------------------
  void torchOffCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (!robot_ || !robot_->isConnected()) {
      res->success = false;
      res->message = "Robot not connected";
      return;
    }

    const int bit      = (1 << torch_od_pin_);
    const int pulse_ms = torch_pulse_ms_;
    const int gap_ms   = torch_gap_ms_;

    enqueueJob([this, bit, pulse_ms, gap_ms]() {
      for (int i = 0; i < 3; ++i) {
        RCLCPP_INFO(this->get_logger(), "torch_off pulse %d/3", i + 1);
        pinHigh(bit);
        std::this_thread::sleep_for(std::chrono::milliseconds(pulse_ms));
        pinLow(bit);
        if (i < 2) {
          std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
        }
      }
      RCLCPP_INFO(this->get_logger(), "torch_off sequence complete");
    });

    res->success = true;
    res->message = "Torch off sequence queued";
  }

  // ----------------------------------------------------------------
  // Members
  // ----------------------------------------------------------------
  ArRobot* robot_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr         torch_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr         torch_off_srv_;

  int torch_od_pin_{6};
  int torch_pulse_ms_{300};
  int torch_gap_ms_{500};

  int digout_mask_{0};
  std::mutex digout_mtx_;

  std::thread                        worker_thread_;
  std::mutex                         queue_mtx_;
  std::condition_variable            queue_cv_;
  std::queue<std::function<void()>>  job_queue_;
  bool                               worker_stop_;
};

// ----------------------------------------------------------------
// main
// ----------------------------------------------------------------
int main(int argc, char** argv)
{
  Aria::init();

  ArRobot robot;
  ArArgumentParser parser(&argc, argv);
  parser.loadDefaultArguments();

  ArRobotConnector robotConnector(&parser, &robot);
  if (!robotConnector.connectRobot()) {
    ArLog::log(ArLog::Terse, "Could not connect to robot.");
    return 1;
  }
  ArLog::log(ArLog::Normal, "Connected to robot.");

  robot.lock();
  robot.enableMotors();
  robot.setTransVelMax(300.0);
  robot.setRotVelMax(40.0);
  robot.unlock();

  robot.runAsync(true);

  rclcpp::init(argc, argv);
  auto node = std::make_shared<PioneerBaseNode>(&robot);
  rclcpp::spin(node);

  robot.lock();
  robot.stop();
  robot.disableMotors();
  robot.unlock();
  robot.disconnect();
  Aria::exit(0);
  rclcpp::shutdown();

  return 0;
}
