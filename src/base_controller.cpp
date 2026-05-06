#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <Aria.h>

#include <std_srvs/srv/trigger.hpp>
#include <mutex>
#include <chrono>

#include <cmath>

class PioneerBaseNode : public rclcpp::Node
{
public:
  explicit PioneerBaseNode(ArRobot* robot)
  : Node("pioneer_base"), robot_(robot)
  {
    using std::placeholders::_1;
    using std::placeholders::_2;

    cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", 10,
      std::bind(&PioneerBaseNode::cmdVelCallback, this, _1));

    torch_od_pin_   = this->declare_parameter<int>("torch_od_pin", 6);
    torch_pulse_ms_ = this->declare_parameter<int>("torch_pulse_ms", 200);
    torch_gap_ms_   = this->declare_parameter<int>("torch_gap_ms", 400);

    torch_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_pulse",
      std::bind(&PioneerBaseNode::torchPulseCb, this, _1, _2));

    torch_off_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_off",
      std::bind(&PioneerBaseNode::torchOffCb, this, _1, _2));
  }

private:
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
    robot_->setVel(msg->linear.x * 1000.0);           // m/s -> mm/s
    robot_->setRotVel(msg->angular.z * 180.0 / M_PI); // rad/s -> deg/s
    robot_->unlock();
  }

  // ----------------------------------------------------------------
  // Low-level DIGOUT helpers
  // ----------------------------------------------------------------
  void pinHigh(int bit)
  {
    std::lock_guard<std::mutex> g(digout_mtx_);
    digout_mask_ |= bit;
    robot_->lock();
    robot_->comInt(ArCommands::DIGOUT, digout_mask_);
    robot_->unlock();
    RCLCPP_INFO(this->get_logger(), "DIGOUT HIGH mask=0x%02x", digout_mask_);
  }

  void pinLow(int bit)
  {
    std::lock_guard<std::mutex> g(digout_mtx_);
    digout_mask_ &= ~bit;
    robot_->lock();
    robot_->comInt(ArCommands::DIGOUT, digout_mask_);
    robot_->unlock();
    RCLCPP_INFO(this->get_logger(), "DIGOUT LOW  mask=0x%02x", digout_mask_);
  }

  // ----------------------------------------------------------------
  // Cancel the active timer safely (never from inside the callback)
  // ----------------------------------------------------------------
  void cancelTimer()
  {
    if (active_timer_) {
      active_timer_->cancel();
      active_timer_.reset();
    }
  }

  // ----------------------------------------------------------------
  // Single-shot timer helper
  // f is called once after delay_ms, then the timer cancels itself
  // ----------------------------------------------------------------
  void oneShotTimer(int delay_ms, std::function<void()> f)
  {
    active_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(delay_ms),
      [this, f]() {
        active_timer_->cancel();  // cancel before running f so f can re-arm
        f();
      }
    );
  }

  // ----------------------------------------------------------------
  // torch_pulse: single button-press pulse (turns the light ON)
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

    const int bit = (1 << torch_od_pin_);
    const int pulse_ms = torch_pulse_ms_;

    cancelTimer();       // clear any pending pulse
    pinHigh(bit);        // relay closes

    oneShotTimer(pulse_ms, [this, bit]() {
      pinLow(bit);       // relay opens after pulse_ms
    });

    res->success = true;
    res->message = "Torch pulse scheduled";
  }

  // ----------------------------------------------------------------
  // torch_off: 3 pulses to cycle Low -> Strobe -> Off
  // Uses a recursive timer chain so the executor is never blocked
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

    cancelTimer();
    fireOffPulse(0);

    res->success = true;
    res->message = "Torch off sequence started";
  }

  // Send pulse number `n` (0-based). After the pulse goes low,
  // wait gap_ms then fire pulse n+1, until all 3 are done.
  void fireOffPulse(int n)
  {
    if (n >= 3) return;   // all done

    const int bit = (1 << torch_od_pin_);
    const int pulse_ms = torch_pulse_ms_;
    const int gap_ms   = torch_gap_ms_;

    RCLCPP_INFO(this->get_logger(), "torch_off pulse %d / 3", n + 1);
    pinHigh(bit);

    oneShotTimer(pulse_ms, [this, bit, n, gap_ms]() {
      pinLow(bit);
      if (n + 1 < 3) {
        // wait gap then fire next pulse
        oneShotTimer(gap_ms, [this, n]() {
          fireOffPulse(n + 1);
        });
      }
    });
  }

  // ----------------------------------------------------------------
  // Members
  // ----------------------------------------------------------------
  ArRobot* robot_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr torch_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr torch_off_srv_;

  int torch_od_pin_{6};
  int torch_pulse_ms_{200};
  int torch_gap_ms_{400};

  int digout_mask_{0};
  std::mutex digout_mtx_;

  // Single shared timer slot - only one timer active at a time
  rclcpp::TimerBase::SharedPtr active_timer_;
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
  robot.setTransVelMax(300.0);  // mm/s
  robot.setRotVelMax(40.0);     // deg/s
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