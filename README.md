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
- Physical sensor mast (monopod + stack light + cue light + machined aluminum plates)
- Torch/cue light toggle via Pioneer DIGOUT (OD6) + relay, controlled from dashboard
- Per-trial LiDAR rosbag recording with automatic transfer to laptop
- PS4 DualShock 4 joystick support integrated into dashboard

---

# 0) Hardware + network assumptions

## 0.1 Hardware
- Pioneer 3‑DX with onboard PC
- RPLIDAR A1 (USB) — mounted at top of mast on machined aluminum plate
- OAK‑D‑Pro (USB) — front camera, mounted on mast via CAMVATE 75mm VESA plate
- Rear USB webcam (Logitech Brio 101) — mounted on mast rear clamp
- IFOOTAGE Round Base Monopod A300 — primary sensor mast bolted to robot top deck
- LUBAN 3-layer stack light (no buzzer) — mounted below LiDAR on custom aluminum plate
- SmallRig RM01 mini LED cue light — mounted below front camera, aimed at exit path
- Insignia 4-port powered USB hub — mounted on robot base for RPLIDAR + rear camera
- 5V 1-channel relay module (optocoupler isolated) — wired to Pioneer User I/O OD6 (pin 14)
- SEAMAGIC rechargeable work light — wired through relay for torch toggle
- Laptop on same network for operation
- PS4 DualShock 4 controller (USB or Bluetooth) — plugged into laptop

## 0.2 Network
- Robot and laptop on same Wi‑Fi/Ethernet subnet
- Robot IP: `192.168.1.31` | Laptop IP: `192.168.1.8`
- We use:
  - `ROS_DOMAIN_ID=7`
  - `rmw_cyclonedds_cpp`
  - subnet discovery

## 0.3 Workspace layout
The repo lives at `~/ros2_ws/src/pioneer3/` and contains all packages as subdirectories:
- `pioneer3/` — top-level ROS package (base controller, launch files, config)
- `pioneer3/pioneer_dashboard_app/` — standalone Qt dashboard (runs on laptop)
- `pioneer3/pioneer_dashboard_rviz/` — RViz plugin variant of the dashboard

There should be **no** standalone `pioneer_dashboard_app/` directly under `src/`. If one exists from an earlier setup, delete it:
```bash
rm -rf ~/ros2_ws/src/pioneer_dashboard_app
```

---

# 1) Robot PC — Fresh install (Ubuntu 24.04 + ROS 2 Jazzy)

## 1.1 Install ROS 2 Jazzy
```bash
sudo apt update
sudo apt install -y ros-jazzy-desktop
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
```bash
cd ~/ros2_ws/src
git clone https://github.com/Slamtec/sllidar_ros2.git
```

## 2.2 Front camera (OAK‑D‑Pro): `depthai_ros_driver`
```bash
sudo apt update
sudo apt install -y ros-jazzy-depthai-ros-driver
```

### 2.2.1 OAK permissions (udev rules)
If you see "Insufficient permissions … X_LINK_UNBOOTED":
- Install depthai udev rules (method depends on the driver packaging)
- Replug camera or reboot

## 2.3 Rear camera (USB): `v4l2_camera`
```bash
sudo apt update
sudo apt install -y ros-jazzy-v4l2-camera v4l-utils
v4l2-ctl --list-devices
ls -l /dev/video*
```

---

# 3) Robot PC — Install ARIA (Pioneer base control)

## 3.1 Install/build ARIA
Expected paths:
- Header: `/usr/local/Aria/include/Aria.h`
- Library: `/usr/local/Aria/lib/libAria.so`

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

# Multi-machine ROS 2
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

Laptop convenience alias (add to `~/.bashrc`):
```bash
alias build_dashboard='cd ~/ros2_ws && colcon build --symlink-install --packages-select pioneer_dashboard_app --base-paths src/pioneer3/pioneer_dashboard_app'
```

---

# 6) Robot bringup (manual)

```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 launch pioneer3 pioneer3_robot.launch.py
```

## 6.1 Front camera low-latency option

The dashboard defaults to `/oak/rgb/image_raw/compressed` which is decoded directly by Qt — this is the recommended setting. No republisher needed.

If you want to use the republished raw topic instead:
```bash
ros2 run image_transport republish \
  --ros-args \
  -p in_transport:=compressed \
  -p out_transport:=raw \
  --remap in:=/oak/rgb/image_raw \
  --remap out:=/oak/rgb/image_raw_local \
  -r __node:=oak_image_republisher
```
Then set the dashboard front topic to `/oak/rgb/image_raw_local`.

---

# 7) Auto-start on boot (robot)

## 7.1 Start script
```bash
mkdir -p ~/bin
cat > ~/bin/start_pioneer_robot.sh << 'EOF'
#!/usr/bin/env bash
set -e
source /opt/ros/jazzy/setup.bash
source /home/easel/ros2_ws/install/setup.bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
exec ros2 launch pioneer3 pioneer3_robot.launch.py
EOF
chmod +x ~/bin/start_pioneer_robot.sh
```

## 7.2 systemd service
```bash
sudo tee /etc/systemd/system/pioneer_robot.service << 'EOF'
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
EOF

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

## 8.1 Verify robot topics are visible
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

## 8.2 Build the dashboard (laptop only)

The dashboard package lives inside the `pioneer3` repo subdirectory. Standard `colcon build` without `--base-paths` will **not** find it. Always use:

```bash
cd ~/ros2_ws
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```

Or use the alias from §5:
```bash
build_dashboard
```

**Do not** use `--packages-select pioneer3 pioneer_dashboard_app` on the laptop — `pioneer3` requires `pioneer_msgs` and ARIA which are robot-only dependencies and will fail to build on the laptop.

## 8.3 Run the dashboard (laptop)

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
ros2 daemon stop
ros2 daemon start
ros2 topic list | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw)"
source ~/ros2_ws/install/setup.bash
export QT_QPA_PLATFORM=xcb
ros2 run pioneer_dashboard_app pioneer_dashboard_app
```

Dashboard defaults:
- Front: `/oak/rgb/image_raw/compressed` *(default — decoded directly by Qt, low latency)*
- Rear: `/rear/image_raw`
- Scan: `/scan`
- cmd_vel: `/cmd_vel`

**Smart topic routing:** the dashboard automatically detects whether the front topic ends in `/compressed` and subscribes as `CompressedImage` or `Image` accordingly. Switch at runtime by editing the topic field and clicking **Apply / Reconnect**.

---

# 9) Joystick control (PS4 DualShock 4)

The dashboard has integrated PS4 joystick support. The joystick is **optional** — if `/dev/input/js0` is not present the dashboard runs in GUI-only mode. A status label between the Topics box and the Teleop box shows connection state.

**The deadman checkbox must still be enabled in the GUI before the joystick can drive the robot.**

## 9.1 PS4 button/axis mapping

| Input | Index | Action |
|---|---|---|
| Left stick Y | Axis 1 | Forward / Back |
| Left stick X | Axis 0 | Turn left / right |
| L1 | Button 4 | Linear speed − 0.05 m/s |
| R1 | Button 5 | Linear speed + 0.05 m/s |
| L2 | Button 6 | Angular speed − 0.05 rad/s |
| R2 | Button 7 | Angular speed + 0.05 rad/s |
| Cross (✕) | Button 0 | Torch toggle (same as GUI button) |
| Circle (○) | Button 1 | Stop robot |
| Square (□) | Button 2 | Start / Stop trial (same as GUI button) |
| Triangle (△) | Button 3 | Apply / Reconnect topics |

Slider changes from L1/R1/L2/R2 are reflected visually in the GUI sliders. Directional buttons visually depress when the stick is pushed.

## 9.2 Verify joystick is detected
```bash
ls /dev/input/js0
jstest /dev/input/js0    # install with: sudo apt install joystick
```

---

# 10) Torch / cue light toggle

The torch is a SEAMAGIC rechargeable work light wired through a 5V relay module driven by Pioneer digital output pin **OD6** (User I/O connector pin 14).

**Important:** AUX power must be enabled on the Pioneer User Control Panel (press AUX1 until the red LED lights) for the 5V line on the IDC connector to be live.

## 10.1 How the toggle works
The light cycles: High → Low → Strobe → Off.
- **TORCH ON** (green button / joystick ✕): single 300ms pulse → light goes to High
- **TORCH OFF** (red button / joystick ✕): 3 pulses 500ms apart → cycles to Off

Before trials, manually cycle the light to Off. The ON button will always bring it to High on the first press.

## 10.2 ROS services

| Service | Type | Effect |
|---|---|---|
| `/pioneer_base/torch_pulse` | `std_srvs/srv/Trigger` | Single pulse — turns light on |
| `/pioneer_base/torch_off` | `std_srvs/srv/Trigger` | 3-pulse sequence — cycles to Off |

```bash
ros2 service call /pioneer_base/torch_pulse std_srvs/srv/Trigger "{}"
ros2 service call /pioneer_base/torch_off std_srvs/srv/Trigger "{}"
```

## 10.3 Tunable parameters
```bash
ros2 param set /pioneer_base torch_pulse_ms 300   # pulse width in ms
ros2 param set /pioneer_base torch_gap_ms 500     # gap between off-sequence pulses
```

## 10.4 DIGOUT wiring reference

| Pin | Signal | Use |
|---|---|---|
| 14 | OD6 | Relay IN signal |
| 18 | Vcc (5V) | Relay VCC |
| 20 | GND | Relay GND |

---

# 11) Trial recording (LiDAR rosbag)

## 11.1 How it works
- **⏺ START TRIAL** (blue button / joystick □): calls `/bag_recorder/start_recording` — starts `ros2 bag record /scan` with a timestamped name under `/home/easel/bags/`
- **⏹ STOP TRIAL** (red button / joystick □): calls `/bag_recorder/stop_recording` — flushes the bag and rsyncs to `kush@192.168.1.8:~/bags/` automatically

## 11.2 Prerequisites (one-time SSH key setup)
```bash
# On robot
ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519
ssh-copy-id kush@192.168.1.8
ssh -o BatchMode=yes kush@192.168.1.8 'echo OK'

# On laptop
sudo apt install -y openssh-server
sudo systemctl enable ssh
sudo systemctl start ssh
```

## 11.3 Verify bags on laptop
```bash
ls ~/bags/
ros2 bag info ~/bags/session_<timestamp>
```

## 11.4 Tunable parameters

| Parameter | Default | Description |
|---|---|---|
| `laptop_user` | `kush` | Username on laptop for rsync |
| `laptop_ip` | `192.168.1.8` | Laptop IP |
| `laptop_dir` | `~/bags` | Destination on laptop |
| `robot_dir` | `/home/easel/bags` | Where bags are saved on robot |
| `scan_topic` | `/scan` | Topic to record |

---

# 12) Laptop-first Git workflow

## 12.1 Edit on laptop → push
```bash
cd ~/ros2_ws/src/pioneer3
git pull                  # always pull before editing
git add -A
git commit -m "Describe your change"
git push
```

## 12.2 Robot pulls + rebuilds pioneer3 + restarts service
```bash
ssh -t easel@192.168.1.31 '
  source /opt/ros/jazzy/setup.bash &&
  cd ~/ros2_ws/src/pioneer3 && git pull &&
  cd ~/ros2_ws &&
  colcon build --symlink-install --packages-select pioneer3 &&
  sudo systemctl restart pioneer_robot.service &&
  systemctl status pioneer_robot.service --no-pager
'
```

## 12.3 Rebuild dashboard on laptop only
```bash
cd ~/ros2_ws
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```

No robot-side rebuild or restart needed for dashboard-only changes.

## 12.4 Restart service on robot only
```bash
ssh -t easel@192.168.1.31 'sudo systemctl restart pioneer_robot.service'
```

---

# 13) Troubleshooting

## 13.1 "Dashboard works but no LiDAR in the widget"
```bash
ros2 topic hz /scan --window 50
ros2 node info /pioneer_dashboard_app | sed -n '1,120p'
ros2 topic info /scan -v | head -n 90
```

## 13.2 LiDAR timeout (SL_RESULT_OPERATION_TIMEOUT)
ModemManager grabbing `/dev/ttyUSB0` is the most common cause:
```bash
sudo systemctl stop ModemManager || true
sudo systemctl disable ModemManager || true
sudo systemctl restart pioneer_robot.service
ros2 topic hz /scan --window 50
```

## 13.3 Rear camera missing
```bash
# Robot
v4l2-ctl --list-devices
ls -l /dev/video*
# Laptop
ros2 topic list | grep rear
```

## 13.4 Front camera lags
Use `/oak/rgb/image_raw/compressed` in the dashboard front topic field — this is the default and recommended setting. Raw over the network causes lag.

## 13.5 Torch button does nothing
Click **Apply / Reconnect** to recreate service clients, then try again. Also confirm AUX1 LED is lit on the Pioneer User Control Panel.

## 13.6 Torch relay clicks but light doesn't respond
```bash
ros2 param set /pioneer_base torch_gap_ms 700
```
Increase in 100ms steps until the light reliably lands on Off.

## 13.7 colcon can't find pioneer_dashboard_app
The dashboard is a subdirectory inside `pioneer3`. Always build it with `--base-paths`:
```bash
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```

## 13.8 Trial recording — bag transfer fails
```bash
# Test SSH from robot to laptop
ssh -o BatchMode=yes kush@192.168.1.8 'echo OK'
# Manual rsync if needed
rsync -avz easel@192.168.1.31:~/bags/ ~/bags/
```

## 13.9 Joystick not detected
```bash
ls /dev/input/js0
# If missing, replug controller and check:
cat /proc/bus/input/devices | grep -A 4 "js\|Joystick\|Controller"
```

---

# Appendix A — Pioneer 3-DX speed specifications

| Parameter | Value |
|---|---|
| Max continuous speed | 1.2 m/s |
| Peak speed | 1.6 m/s |
| ARIA `setTransVelMax` in base_controller | 1200 mm/s (1.2 m/s) |
| ARIA `setRotVelMax` in base_controller | 300 deg/s |
| Dashboard linear slider range | 0 – 1.20 m/s (default 0.80 m/s) |
| Dashboard angular slider range | 0 – 3.00 rad/s (default 1.20 rad/s) |

---

# Appendix B — Known-good topic names and QoS

## B.1 Required topics
- `/scan` — `sensor_msgs/msg/LaserScan`
- `/oak/rgb/image_raw` — `sensor_msgs/msg/Image`
- `/oak/rgb/image_raw/compressed` — `sensor_msgs/msg/CompressedImage` *(preferred)*
- `/rear/image_raw` — `sensor_msgs/msg/Image`
- `/cmd_vel` — `geometry_msgs/msg/Twist`
- `/pioneer_base/torch_pulse` — `std_srvs/srv/Trigger`
- `/pioneer_base/torch_off` — `std_srvs/srv/Trigger`
- `/bag_recorder/start_recording` — `std_srvs/srv/Trigger`
- `/bag_recorder/stop_recording` — `std_srvs/srv/Trigger`

```bash
ros2 topic list -t | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw|/cmd_vel)"
ros2 service list | egrep "(torch|bag_recorder)"
```

## B.2 QoS
- `/scan` publisher and dashboard subscriber: **RELIABLE**
- Image topics: RELIABLE or BEST_EFFORT depending on driver — dashboard handles both

## B.3 Health check
```bash
ros2 topic hz /scan --window 50
ros2 node info /pioneer_dashboard_app | sed -n '1,90p'
ros2 service list | grep -E "(torch|bag_recorder)"
```

---

# Appendix C — Day-of-experiment quick start

## C.1 Robot (power-on)
1. Power on the Pioneer 3-DX
2. Press **AUX1** on User Control Panel until red LED lights (enables 5V for relay)
3. Wait ~1–2 min for systemd bringup
4. Optional confirm:
```bash
ssh -t easel@192.168.1.31 'systemctl status pioneer_robot.service --no-pager'
```

## C.2 Laptop (operator) — exact sequence
```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
ros2 daemon stop
ros2 daemon start
ros2 topic list | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw)"
source ~/ros2_ws/install/setup.bash
export QT_QPA_PLATFORM=xcb
ros2 run pioneer_dashboard_app pioneer_dashboard_app
```

## C.3 Torch pre-trial check
1. Manually cycle the light to **Off** using its physical button
2. Click **TORCH ON** — light should go to High
3. Click **TORCH OFF** — light should return to Off
4. If wrong mode: `ros2 param set /pioneer_base torch_gap_ms 700`

## C.4 Running a trial
1. Click **⏺ START TRIAL** (or joystick □) — recording begins on robot
2. Run the trial
3. Click **⏹ STOP TRIAL** (or joystick □) — recording stops, bag transfers to `~/bags/`
4. Verify: `ls ~/bags/`

## C.5 If something is missing
- `/scan` missing or unstable → disable ModemManager (§13.2)
- Cameras missing → replug USB cameras and restart service
- Torch not responding → check AUX1 LED (§10)
- Bag transfer fails → check SSH key setup (§11.2)

Restart service:
```bash
ssh -t easel@192.168.1.31 'sudo systemctl restart pioneer_robot.service'
```

---

# Appendix D — ARIA install paths

## D.1 Option A: System ARIA already installed
```bash
ls -l /usr/local/Aria/include/Aria.h
ls -l /usr/local/Aria/lib | grep -i aria
```

## D.2 Option B: Build ARIA/AriaCoda from source
```bash
cd ~
git clone <YOUR_LAB_ARIA_REPO> AriaCoda
cd AriaCoda
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

> If ARIA installs under `/usr/local/include/Aria` and `/usr/local/lib` instead of `/usr/local/Aria/`, update the `include_directories` and `link_directories` in `CMakeLists.txt` accordingly.
