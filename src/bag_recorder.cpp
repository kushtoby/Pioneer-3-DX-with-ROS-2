#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <cstdlib>
#include <ctime>
#include <string>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

class BagRecorderNode : public rclcpp::Node
{
public:
  BagRecorderNode() : Node("bag_recorder")
  {
    laptop_user_ = this->declare_parameter<std::string>("laptop_user", "kush");
    laptop_ip_   = this->declare_parameter<std::string>("laptop_ip",   "192.168.1.8");
    laptop_dir_  = this->declare_parameter<std::string>("laptop_dir",  "~/bags");
    robot_dir_   = this->declare_parameter<std::string>("robot_dir",   "/home/easel/bags");
    scan_topic_  = this->declare_parameter<std::string>("scan_topic",  "/scan");

    // Make sure the bags directory exists
    std::system(("mkdir -p " + robot_dir_).c_str());

    start_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/start_recording",
      std::bind(&BagRecorderNode::startCb, this,
                std::placeholders::_1, std::placeholders::_2));

    stop_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "~/stop_recording",
      std::bind(&BagRecorderNode::stopCb, this,
                std::placeholders::_1, std::placeholders::_2));

    RCLCPP_INFO(this->get_logger(), "BagRecorder ready. Services: ~/start_recording, ~/stop_recording");
  }

private:
  // ----------------------------------------------------------------
  // Start recording
  // ----------------------------------------------------------------
  void startCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (recording_) {
      res->success = false;
      res->message = "Already recording: " + current_bag_;
      return;
    }

    // Timestamped bag name
    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", std::localtime(&now));
    current_bag_ = robot_dir_ + "/session_" + std::string(ts);

    // Fork ros2 bag record as a child process
    bag_pid_ = ::fork();
    if (bag_pid_ < 0) {
      res->success = false;
      res->message = "fork() failed";
      return;
    }

    if (bag_pid_ == 0) {
      // Child: exec ros2 bag record
      // Source ROS so the child can find ros2
      ::execlp("ros2", "ros2", "bag", "record",
               scan_topic_.c_str(),
               "-o", current_bag_.c_str(),
               nullptr);
      ::_exit(1); // exec failed
    }

    recording_ = true;
    RCLCPP_INFO(this->get_logger(), "Recording started: %s (pid=%d)",
                current_bag_.c_str(), (int)bag_pid_);

    res->success = true;
    res->message = "Recording started: " + current_bag_;
  }

  // ----------------------------------------------------------------
  // Stop recording + transfer to laptop
  // ----------------------------------------------------------------
  void stopCb(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>,
    std::shared_ptr<std_srvs::srv::Trigger::Response> res)
  {
    if (!recording_) {
      res->success = false;
      res->message = "Not currently recording";
      return;
    }

    // Send SIGINT to ros2 bag record so it flushes and closes cleanly
    ::kill(bag_pid_, SIGINT);
    int status = 0;
    ::waitpid(bag_pid_, &status, 0);
    bag_pid_   = -1;
    recording_ = false;

    RCLCPP_INFO(this->get_logger(), "Recording stopped: %s", current_bag_.c_str());

    // Transfer bag to laptop
    const std::string dest = laptop_user_ + "@" + laptop_ip_ + ":" + laptop_dir_ + "/";
    const std::string cmd  = "ssh " + laptop_user_ + "@" + laptop_ip_ +
                             " 'mkdir -p " + laptop_dir_ + "' && "
                             "rsync -az --progress " +
                             current_bag_ + " " + dest;

    RCLCPP_INFO(this->get_logger(), "Transferring bag to laptop: %s", cmd.c_str());
    int rc = std::system(cmd.c_str());

    if (rc == 0) {
      RCLCPP_INFO(this->get_logger(), "Transfer complete: %s -> %s:%s",
                  current_bag_.c_str(), laptop_ip_.c_str(), laptop_dir_.c_str());
      res->success = true;
      res->message = "Stopped and transferred: " + current_bag_;
    } else {
      RCLCPP_WARN(this->get_logger(), "Transfer failed (rc=%d)", rc);
      res->success = false;
      res->message = "Recording stopped but transfer failed. Bag saved at: " + current_bag_;
    }

    current_bag_.clear();
  }

  // ----------------------------------------------------------------
  // Members
  // ----------------------------------------------------------------
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr start_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_srv_;

  std::string laptop_user_;
  std::string laptop_ip_;
  std::string laptop_dir_;
  std::string robot_dir_;
  std::string scan_topic_;

  bool        recording_{false};
  pid_t       bag_pid_{-1};
  std::string current_bag_;
};

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BagRecorderNode>());
  rclcpp::shutdown();
  return 0;
}
