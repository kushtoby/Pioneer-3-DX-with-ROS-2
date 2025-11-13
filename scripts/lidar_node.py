#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan


class LidarNode(Node):
    def __init__(self):
        super().__init__("lidar_node")

        # Subscribe to the raw lidar scan; adjust topic if your driver uses a different name.
        self.subscription = self.create_subscription(
            LaserScan,
            "scan",
            self.scan_callback,
            10,
        )

    def scan_callback(self, msg: LaserScan):
        # Simple example: log the minimum range in the scan
        if msg.ranges:
            min_range = min(msg.ranges)
            self.get_logger().info(f"Nearest obstacle: {min_range:.2f} m")


def main(args=None):
    rclpy.init(args=args)
    node = LidarNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
