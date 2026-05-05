#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <Aria.h>

#include <std_srvs/srv/trigger.hpp>
#include <mutex>
#include <chrono>
#include <thread>

#include <cmath>

class PioneerBaseNode : public rclcpp::Node
{
public:
  explicit PioneerBaseNode(ArRobot* robot)
  : Node("pioneer_base"), robot_(robot)
  {
    using std::placeholders::_1;
    cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", 10,
      std::bind(&PioneerBaseNode::cmdVelCallback, this, _1));

    // Torch parameters (optional)
    torch_od_pin_   = this->declare_parameter<int>("torch_od_pin", 6);
    torch_pulse_ms_ = this->declare_parameter<int>("torch_pulse_ms", 200);

    // Torch service: /pioneer_base/torch_pulse
    torch_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_pulse",
      std::bind(&PioneerBaseNode::torchPulseCb, this,
                std::placeholders::_1, std::placeholders::_2));

    torch_off_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/torch_off",
      std::bind(&PioneerBaseNode::torchOffCb, this,
                std::placeholders::_1, std::placeholders::_2));
  }

private:
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
  {
    if (!robot_->isConnected()) {
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Robot not connected; ignoring cmd_vel");
      return;
    }

    double v_lin = msg->linear.x * 1000.0; // m/s -> mm/s
    double v_rot = msg->angular.z * 180.0 / M_PI; // rad/s -> deg/s

    robot_->lock();
    robot_->setVel(v_lin);
    robot_->setRotVel(v_rot);
    robot_->unlock();
  }

  void torchPulseCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (!robot_ || !robot_->isConnected()) {
      res->success = false;
      res->message = "Robot not connected";
      return;
    }

    const int pin = torch_od_pin_;
    const int ms  = torch_pulse_ms_;

    if (pin < 0 || pin > 30) {
      res->success = false;
      res->message = "Invalid torch_od_pin (expected 0..30)";
      return;
    }

    const int bit = (1 << pin);

    // Turn ON (preserve other digout bits)
    {
      std::lock_guard<std::mutex> g(digout_mtx_);
      digout_mask_ |= bit;

      robot_->lock();
      robot_->comInt(ArCommands::DIGOUT, digout_mask_);
      robot_->unlock();
    }

    // Cancel any pending OFF timer and schedule a new one-shot OFF
    if (torch_off_timer_) {
      torch_off_timer_->cancel();
      torch_off_timer_.reset();
    }

    torch_off_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(ms),
      [this, bit]()
      {
        if (!robot_ || !robot_->isConnected()) {
          // best effort cleanup
          std::lock_guard<std::mutex> g(digout_mtx_);
          digout_mask_ &= ~bit;
          return;
        }

        std::lock_guard<std::mutex> g(digout_mtx_);
        digout_mask_ &= ~bit;

        robot_->lock();
        robot_->comInt(ArCommands::DIGOUT, digout_mask_);
        robot_->unlock();

        // one-shot timer cleanup
        torch_off_timer_->cancel();
        torch_off_timer_.reset();
      }
    );

    res->success = true;
    res->message = "Torch pulse scheduled";
  }

  void torchOffCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (!robot_ || !robot_->isConnected()) {
      res->success = false;
      res->message = "Robot not connected";
      return;
    }

    // Fire 3 pulses 400ms apart to cycle Low -> Strobe -> Off
    // Each pulse: pin high for torch_pulse_ms_, then low, then wait 400ms

    // Cancel any pending off timer from a previous torch_pulse call
    if (torch_off_timer_) {
      torch_off_timer_->cancel();
      torch_off_timer_.reset();
    }
    for (int i = 0; i < 3; ++i) {
      const int pin = torch_od_pin_;
      const int bit = (1 << pin);

      {
        std::lock_guard<std::mutex> g(digout_mtx_);
        digout_mask_ |= bit;
        robot_->lock();
        robot_->comInt(ArCommands::DIGOUT, digout_mask_);
        robot_->unlock();
      }

      // Hold for pulse duration
      std::this_thread::sleep_for(std::chrono::milliseconds(torch_pulse_ms_));

      {
        std::lock_guard<std::mutex> g(digout_mtx_);
        digout_mask_ &= ~bit;
        robot_->lock();
        robot_->comInt(ArCommands::DIGOUT, digout_mask_);
        robot_->unlock();
      }

      // Wait between pulses (skip the wait after the last pulse)
      if (i < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
      }
    }

    res->success = true;
    res->message = "Torch off sequence sent";
  }

  ArRobot* robot_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;

  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr torch_off_srv_;

  // Torch service + state
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr torch_srv_;
  int torch_od_pin_{6};
  int torch_pulse_ms_{200};

  int digout_mask_{0};
  std::mutex digout_mtx_;
  rclcpp::TimerBase::SharedPtr torch_off_timer_;
};

int main(int argc, char** argv)
{
  // ARIA init and connect
  Aria::init();

  ArRobot robot;
  ArArgumentParser parser(&argc, argv);
  parser.loadDefaultArguments();

  ArRobotConnector robotConnector(&parser, &robot);

  if (!robotConnector.connectRobot()) {
    ArLog::log(ArLog::Terse, "Could not connect to the robot. Check -robotPort etc.");
    return 1;
  }

  ArLog::log(ArLog::Normal, "Connected to robot.");

  // Enable motors and set some safe limits
  robot.lock();
  robot.enableMotors();
  robot.setTransVelMax(300.0); // mm/s (0.3 m/s)
  robot.setRotVelMax(40.0); // deg/s
  robot.unlock();

  robot.runAsync(true); // start ARIA background loop

  // ROS2 init and spin
  rclcpp::init(argc, argv);
  auto node = std::make_shared<PioneerBaseNode>(&robot);
  rclcpp::spin(node);

  // Cleanup
  robot.lock();
  robot.stop();
  robot.disableMotors();
  robot.unlock();
  robot.disconnect();
  Aria::exit(0);
  rclcpp::shutdown();

  return 0;
}
