# Pioneer 3‑DX with ROS 2 Jazzy — Build, Bringup, and Operator Dashboard (Live Experiments)

This is an end‑to‑end, **field‑ready** build & operations manual for running a Pioneer 3‑DX robot in **live experiments** (not tied to a class).  
It covers: base control, LiDAR, front + rear cameras, a standalone operator dashboard (camera + LiDAR + teleop + torch), and headless auto‑start on boot.

---

## What you will achieve

After completing this manual, you can:

- Boot the robot and have it **auto‑start** sensors + base control (headless).
- From a laptop on the same network, run a **standalone operator dashboard** that:
  - Displays **front camera** as the main view
  - Displays **rear camera** in **picture‑in‑picture (PiP)**
  - Displays a **mini LiDAR** view (LaserScan)
  - Provides **teleop controls** (deadman + speed sliders + direction buttons + STOP)
  - Triggers a **torch/aux button** via a ROS service call
- Use a **laptop‑first workflow**: edit → push → robot pulls → rebuild → restart service.

---

## System architecture at a glance

### Core ROS pieces

- **Base controller:** `pioneer3/base_controller` (C++ + ARIA)
  - Subscribes: `/cmd_vel`
  - Provides: `/pioneer_base/torch_pulse` (`std_srvs/Trigger`)
  - Connects via: `-robotPort /dev/ttyS0` (typical onboard Pioneer serial)

- **LiDAR:** `sllidar_ros2` → publishes `/scan` (`sensor_msgs/LaserScan`)

- **Front camera:** `depthai_ros_driver` (OAK‑D‑Pro) → publishes `/oak/rgb/image_raw`

- **Rear camera:** `v4l2_camera` (USB webcam) → publishes `/rear/image_raw`

- **Operator dashboard:** `pioneer_dashboard_app` (Qt5 Widgets + ROS 2 + OpenCV)
  - Subscribes: `/oak/rgb/image_raw`, `/rear/image_raw`, `/scan`
  - Publishes: `/cmd_vel`
  - Calls: `/pioneer_base/torch_pulse`

---

# Part A — Build & Setup (Robot PC)

## A1. Hardware checklist

- Pioneer 3‑DX with onboard PC (Ubuntu 24.04)
- RPLIDAR A1 (USB)
- OAK‑D‑Pro (USB)
- Rear USB webcam (e.g., Logitech C920)
- Optional: monitor + keyboard + mouse for initial setup
- Laptop on same network for operation

## A2. OS + ROS

- Ubuntu 24.04
- ROS 2 Jazzy Desktop

Sanity check:
```bash
source /opt/ros/jazzy/setup.bash
ros2 --help
```

## A3. Workspace

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws
```

## A4. Permissions (serial + camera)

```bash
sudo usermod -a -G dialout $USER
sudo usermod -a -G video $USER
```
Log out/in (or reboot) after adding groups.

## A5. Base dependencies

```bash
sudo apt update
sudo apt install -y git make g++ \
  python3-colcon-common-extensions python3-rosdep python3-vcstool
```

Initialize rosdep (once):
```bash
sudo rosdep init || true
rosdep update
```

## A6. Clone the repo

```bash
cd ~/ros2_ws/src
git clone https://github.com/kushtoby/Pioneer-3-DX-with-ROS-2.git pioneer3
```

## A7. Build

Install deps:
```bash
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
```

Build:
```bash
colcon build --symlink-install
source install/setup.bash
```

---

# Part B — Networking for real operation (Robot + Laptop)

Recommended environment variables on **both** robot and laptop:

```bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
```

After changes:
```bash
ros2 daemon stop
ros2 daemon start
```

Add to `~/.bashrc` on robot + laptop for convenience:
```bash
# ROS 2 Jazzy
source /opt/ros/jazzy/setup.bash

# Workspace overlays (if present)
if [ -f ~/ros2_ws/install/setup.bash ]; then
  source ~/ros2_ws/install/setup.bash
fi

# Multi-machine ROS 2 (recommended)
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
```

---

# Part C — Launching the robot stack

We run the robot in **headless mode** (drivers + sensors) and operate from the laptop.

## C1. Robot launch files (what they do)

- `pioneer3_robot.launch.py`  
  Brings up: base + LiDAR + OAK + rear camera (and any required containers).  
- `pioneer3_sensors.launch.py`  
  Sensor subset (if you need it separately).  
- `pioneer3_bringup.launch.py`  
  Legacy bringup (if present).

## C2. Manual bringup (robot)

```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 launch pioneer3 pioneer3_robot.launch.py
```

---

# Part D — Auto‑start bringup on boot (Robot PC)

This makes the robot “ready” after power‑on with no login steps.

## D1. Start script

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

Make executable:
```bash
chmod +x ~/bin/start_pioneer_robot.sh
```

## D2. systemd service

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

Enable + start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable pioneer_robot.service
sudo systemctl start pioneer_robot.service
```

Status/logs:
```bash
systemctl status pioneer_robot.service --no-pager
journalctl -u pioneer_robot.service -n 120 --no-pager -l
```

---

# Part E — Laptop operator workflow (Dashboard)

## E1. Confirm robot topics exist (laptop)

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

## E2. Run dashboard (laptop)

```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
export QT_QPA_PLATFORM=xcb
ros2 run pioneer_dashboard_app pioneer_dashboard_app
```

Dashboard defaults (as shipped):
- Front: `/oak/rgb/image_raw`
- Rear: `/rear/image_raw`
- Scan: `/scan`
- cmd_vel: `/cmd_vel`

Verify subscriptions:
```bash
ros2 node info /pioneer_dashboard_app | sed -n '1,90p'
```

---

# Part F — Torch / Aux Trigger

The base controller provides:
- `/pioneer_base/torch_pulse` (`std_srvs/Trigger`)

Test from laptop:
```bash
ros2 service list | grep torch
ros2 service call /pioneer_base/torch_pulse std_srvs/srv/Trigger "{}"
```

---

# Part G — Laptop‑first Git workflow

## G1. Edit on laptop → push

```bash
cd ~/ros2_ws/src/pioneer3
git status
git add -A
git commit -m "Describe your change"
git push
```

## G2. Robot pulls + rebuild + restart service

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

# Part H — Troubleshooting (field‑focused)

## H1. Dashboard shows cameras but no LiDAR
1) Ensure `/scan` is publishing:
```bash
ros2 topic hz /scan --window 50
```
2) Ensure dashboard subscribes:
```bash
ros2 node info /pioneer_dashboard_app | sed -n '1,120p'
```
3) Ensure QoS matches (dashboard subscriber should be RELIABLE):
```bash
ros2 topic info /scan -v | head -n 90
```

## H2. LiDAR timeouts / intermittent `/scan`
Common Ubuntu fix on robot:
```bash
sudo systemctl stop ModemManager || true
sudo systemctl disable ModemManager || true
sudo systemctl restart pioneer_robot.service
```
Then re‑check:
```bash
ros2 topic hz /scan --window 50
```

## H3. Duplicate node name warnings
If you see warnings about nodes sharing the same name, ensure you didn’t launch multiple bringups (service + manual) at the same time.

## H4. Rear camera not publishing
Robot:
```bash
v4l2-ctl --list-devices
ls -l /dev/video*
```
Laptop:
```bash
ros2 topic list | grep rear
```

## H5. Qt platform plugin (Wayland) errors
Force XCB:
```bash
export QT_QPA_PLATFORM=xcb
```

---

# Appendix 1 — Optional RViz Panel Plugin

An optional RViz2 plugin panel exists: `pioneer_dashboard_rviz::DashboardPanel`.

Notes:
- Build plugins on the same machine that runs RViz.
- Match RViz Qt version (Qt5 vs Qt6). Check:
  ```bash
  ldd $(which rviz2) | grep -E "Qt5|Qt6" | head -n 20
  ```

---

# Next milestone: Joystick control

Next development item is joystick/gamepad control integration into the dashboard workflow.
