# Pioneer 3-DX Live Experiments Stack (ROS 2 Jazzy)
## Build from scratch → Auto-bringup on boot → Laptop operator dashboard

## Project provenance (where this started)

This work **started from** the original Pioneer 3-DX course repository by **z1910335**:
- Original repo (starting point): https://github.com/z1910335/Pioneer-3-DX-with-ROS-2

That original repo focused on a baseline bringup (base control + LiDAR + RViz + keyboard teleop).
**This repo extends it for live experiments**, adding:
- Headless bringup on boot (`systemd`) for field use
- Dual-camera pipeline (front OAK-D-Pro + rear USB camera)
- Standalone Qt operator dashboard (front + PiP rear + mini LiDAR + teleop + torch)
- Laptop-first ops workflow (multi-machine ROS 2 + repeatable commands)

---

# 0) Hardware + network assumptions

## 0.1 Hardware
- Pioneer 3‑DX with onboard PC
- RPLIDAR A1 (USB)
- OAK‑D‑Pro (USB)
- Rear USB webcam (C920 or similar)
- Optional: monitor/keyboard/mouse for initial robot setup
- Laptop on same network for operation

## 0.2 Network
- Robot and laptop on same Wi‑Fi/Ethernet subnet
- We use:
  - `ROS_DOMAIN_ID=7`
  - `rmw_cyclonedds_cpp`
  - subnet discovery

---

# 1) Robot PC — Fresh install (Ubuntu 24.04 + ROS 2 Jazzy)

## 1.1 Install ROS 2 Jazzy
On robot:
```bash
sudo apt update
sudo apt install -y ros-jazzy-desktop
```

Sanity check:
```bash
source /opt/ros/jazzy/setup.bash
ros2 --help
```

## 1.2 Base tools + rosdep
```bash
sudo apt update
sudo apt install -y git make g++ cmake \
  python3-colcon-common-extensions python3-rosdep python3-vcstool
sudo rosdep init || true
rosdep update
```

## 1.3 Permissions (serial + camera)
```bash
sudo usermod -a -G dialout $USER
sudo usermod -a -G video $USER
```
Log out/in (or reboot).

## 1.4 Recommended DDS (robot + laptop)
```bash
sudo apt update
sudo apt install -y ros-jazzy-rmw-cyclonedds-cpp
```

---

# 2) Robot PC — Install sensor and camera dependencies

## 2.1 LiDAR (RPLIDAR A1) driver
We use `sllidar_ros2` in this stack.

### Option A (recommended): build `sllidar_ros2` in the same workspace
If this repo vendors it into `~/ros2_ws/src`, you’re done. If not:
```bash
cd ~/ros2_ws/src
git clone https://github.com/Slamtec/sllidar_ros2.git
```

> If your LiDAR ever shows `SL_RESULT_OPERATION_TIMEOUT`, see Troubleshooting §9.

## 2.2 Front camera (OAK‑D‑Pro): `depthai_ros_driver`
Install via apt if available on your setup:
```bash
sudo apt update
sudo apt install -y ros-jazzy-depthai-ros-driver
```
If apt is not available/desired, build from source (follow depthai_ros_driver docs).

### 2.2.1 OAK permissions (udev rules)
If you see “Insufficient permissions … X_LINK_UNBOOTED”:
- Install depthai udev rules (method depends on the driver packaging)
- Replug camera or reboot

## 2.3 Rear camera (USB): `v4l2_camera`
```bash
sudo apt update
sudo apt install -y ros-jazzy-v4l2-camera v4l-utils
```

Verify devices:
```bash
v4l2-ctl --list-devices
ls -l /dev/video*
```

---

# 3) Robot PC — Install ARIA (Pioneer base control)

This project uses ARIA to send velocity commands to the Pioneer base.

## 3.1 Install/build ARIA
If ARIA is not installed, build/install it (your lab may already have it).
Typical outcome expected by our build:
- headers under `/usr/local/Aria/include`
- library under `/usr/local/Aria/lib`

Confirm:
```bash
ls /usr/local/Aria/include/Aria.h
ls /usr/local/Aria/lib | grep -i Aria || true
```

---

# 4) Get this repo + build the robot workspace

## 4.1 Workspace
```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
git clone https://github.com/kushtoby/Pioneer-3-DX-with-ROS-2.git pioneer3
```

## 4.2 Install ROS deps + build
```bash
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash
```

---

# 5) Standard environment (robot + laptop)

Add to `~/.bashrc` on BOTH machines:
```bash
# ROS 2 Jazzy
source /opt/ros/jazzy/setup.bash

# Workspace overlay
if [ -f ~/ros2_ws/install/setup.bash ]; then
  source ~/ros2_ws/install/setup.bash
fi

# Multi-machine ROS2 (recommended)
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
```

Apply:
```bash
source ~/.bashrc
ros2 daemon stop
ros2 daemon start
```

---

# 6) Robot bringup (manual)

The **robot** runs a single launch file (headless) that starts:
- base controller
- LiDAR
- OAK front camera
- rear USB camera

On robot:
```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 launch pioneer3 pioneer3_robot.launch.py
```

---

# 7) Auto-start on boot (robot)

## 7.1 Start script
```bash
mkdir -p ~/bin
nano ~/bin/start_pioneer_robot.sh
```

Paste:
```bash
#!/usr/bin/env bash
set -e

source /opt/ros/jazzy/setup.bash
source /home/easel/ros2_ws/install/setup.bash

export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET

exec ros2 launch pioneer3 pioneer3_robot.launch.py
```

```bash
chmod +x ~/bin/start_pioneer_robot.sh
```

## 7.2 systemd service
```bash
sudo nano /etc/systemd/system/pioneer_robot.service
```

Paste:
```ini
[Unit]
Description=Pioneer Robot Bringup + Sensors (headless)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=easel
WorkingDirectory=/home/easel/ros2_ws
ExecStart=/home/easel/bin/start_pioneer_robot.sh
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable pioneer_robot.service
sudo systemctl start pioneer_robot.service
```

Check:
```bash
systemctl status pioneer_robot.service --no-pager
journalctl -u pioneer_robot.service -n 120 --no-pager -l
```

---

# 8) Laptop operator workflow (dashboard)

## 8.1 Verify robot topics are visible (laptop)
```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET

ros2 daemon stop
ros2 daemon start

ros2 topic list | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw)"
ros2 topic hz /scan --window 50
```

## 8.2 Run the dashboard (laptop)
```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash

# If you get a Qt platform plugin error (Wayland), force XCB:
export QT_QPA_PLATFORM=xcb

ros2 run pioneer_dashboard_app pioneer_dashboard_app
```

Verify subscriptions:
```bash
ros2 node info /pioneer_dashboard_app | sed -n '1,90p'
```

Dashboard defaults:
- Front: `/oak/rgb/image_raw`
- Rear: `/rear/image_raw`
- Scan: `/scan`
- cmd_vel: `/cmd_vel`

---

# 9) Torch / Aux trigger

Service provided by base controller:
- `/pioneer_base/torch_pulse` (`std_srvs/Trigger`)

Test from laptop:
```bash
ros2 service list | grep torch
ros2 service call /pioneer_base/torch_pulse std_srvs/srv/Trigger "{}"
```

---

# 10) Laptop-first Git workflow (recommended)

## 10.1 Edit on laptop → push
```bash
cd ~/ros2_ws/src/pioneer3
git status
git add -A
git commit -m "Describe your change"
git push
```

## 10.2 Robot pulls + rebuild + restart service
```bash
ssh -t easel@<ROBOT_IP> '
  source /opt/ros/jazzy/setup.bash &&
  cd ~/ros2_ws/src/pioneer3 && git pull &&
  cd ~/ros2_ws &&
  colcon build --symlink-install --packages-select pioneer3 &&
  sudo systemctl restart pioneer_robot.service &&
  systemctl status pioneer_robot.service --no-pager
'
```

---

# 11) Troubleshooting (field-focused)

## 11.1 “Dashboard works but no LiDAR in the widget”
1) Confirm `/scan` is publishing (laptop):
```bash
ros2 topic hz /scan --window 50
```
2) Confirm dashboard subscribes:
```bash
ros2 node info /pioneer_dashboard_app | sed -n '1,120p'
```
3) Confirm QoS match:
```bash
ros2 topic info /scan -v | head -n 90
```

## 11.2 LiDAR timeout / intermittent scan (SL_RESULT_OPERATION_TIMEOUT)
A very common cause on Ubuntu is **ModemManager** grabbing `/dev/ttyUSB0`.
On robot:
```bash
sudo systemctl stop ModemManager || true
sudo systemctl disable ModemManager || true
sudo systemctl restart pioneer_robot.service
```
Then verify:
```bash
ros2 topic hz /scan --window 50
```

## 11.3 Rear camera missing
Robot:
```bash
v4l2-ctl --list-devices
ls -l /dev/video*
```
Laptop:
```bash
ros2 topic list | grep rear
```

## 11.4 Duplicate node-name warnings
Ensure you do not run multiple bringups (service + manual) at the same time.

---

# 12) Next milestone: Joystick control

Next development: joystick/gamepad control integrated into the dashboard workflow.
---

# Appendix 2 — ARIA install paths (choose one)

There are two common ways ARIA shows up on a Pioneer system. Use **one**.

## A2.1 Option A: “System ARIA” already installed (common in labs)
This project expects ARIA to resolve to:

- Header: `/usr/local/Aria/include/Aria.h`
- Library: `/usr/local/Aria/lib/libAria.so` (or similar)

Verify:
```bash
ls -l /usr/local/Aria/include/Aria.h
ls -l /usr/local/Aria/lib | grep -i aria
```
If these exist, you can build the workspace as-is.

## A2.2 Option B: Build ARIA/AriaCoda from source
Some environments use the AriaCoda distribution (or equivalent). A typical pattern is:

```bash
cd ~
git clone <YOUR_LAB_ARIA_REPO_OR_VENDOR_URL> AriaCoda
cd AriaCoda
make -j"$(nproc)"
sudo make install
sudo ldconfig
```
Then verify the same expected paths (`/usr/local/Aria/...`) exist.

> **Note**: Different lab distributions may install under `/usr/local/include/Aria` and `/usr/local/lib`.
> If so, update your CMake include/link paths accordingly (or add symlinks).

---

# Appendix 3 — Known-good topic names and QoS (quick checklist)

When the robot stack is healthy, you should see these topics from the laptop:

## A3.1 Required topics (names)
- LiDAR scan: `/scan` (`sensor_msgs/msg/LaserScan`)
- Front RGB: `/oak/rgb/image_raw` (`sensor_msgs/msg/Image`)
- Rear RGB: `/rear/image_raw` (`sensor_msgs/msg/Image`)
- Velocity command: `/cmd_vel` (`geometry_msgs/msg/Twist`)
- Torch service: `/pioneer_base/torch_pulse` (`std_srvs/srv/Trigger`)

List + types:
```bash
ros2 topic list -t | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw|/cmd_vel)"
ros2 service list | egrep "(torch_pulse)"
```

## A3.2 QoS expectations (important)
- `/scan` publisher: **RELIABLE**
- Dashboard subscriber to `/scan`: **RELIABLE**
- Image topics may be RELIABLE or BEST_EFFORT depending on driver; the dashboard is designed to work with both.

Check `/scan` QoS:
```bash
ros2 topic info /scan -v | head -n 90
```
You want to see:
- Publisher `sllidar_node` reliability = RELIABLE
- Subscriber `pioneer_dashboard_app` reliability = RELIABLE

## A3.3 Health check commands (fast)
```bash
ros2 topic hz /scan --window 50
ros2 node info /pioneer_dashboard_app | sed -n '1,90p'
```

---

# Appendix 4 — Day-of-experiment quick start (minimal steps)

This is the “don’t think, just run” checklist.

## A4.1 Robot (power-on)
1. Power on the Pioneer 3-DX.
2. Wait ~1–2 minutes for Linux + `systemd` bringup to complete.
3. (Optional) Confirm bringup is running:
```bash
ssh -t easel@<ROBOT_IP> 'systemctl status pioneer_robot.service --no-pager'
```

## A4.2 Laptop (operator)
Open a terminal on the same network and run:

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
ros2 daemon stop
ros2 daemon start

# Verify the 3 key feeds
ros2 topic list | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw)"
ros2 topic hz /scan --window 50
```

If the three topics exist and `/scan` shows a stable Hz, run the dashboard:

```bash
source ~/ros2_ws/install/setup.bash
export QT_QPA_PLATFORM=xcb
ros2 run pioneer_dashboard_app pioneer_dashboard_app
```

## A4.3 If something is missing
- If `/scan` is missing or unstable: reboot robot or disable ModemManager (Troubleshooting §11.2)
- If cameras are missing: replug USB cameras and restart `pioneer_robot.service`

Restart service (robot):
```bash
ssh -t easel@<ROBOT_IP> 'sudo systemctl restart pioneer_robot.service'
```

---
