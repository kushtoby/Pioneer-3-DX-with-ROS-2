#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"

using namespace std::chrono_literals;

class BaseController : public rclcpp::Node
{
public:
  BaseController()
  : Node("base_controller")
  {
    cmd_vel_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 10);

    // Example: periodically send a small forward command.
    timer_ = this->create_wall_timer(
      200ms,
      std::bind(&BaseController::timer_callback, this));
  }

private:
  void timer_callback()
  {
    auto msg = geometry_msgs::msg::Twist();
    msg.linear.x = 0.1;  // 0.1 m/s forward
    msg.angular.z = 0.0;
    cmd_vel_pub_->publish(msg);
  }

  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BaseController>());
  rclcpp::shutdown();
  return 0;
}
