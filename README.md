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

## 0.2 Network
- Robot and laptop on same Wi‑Fi/Ethernet subnet
- We use:
  - `ROS_DOMAIN_ID=7`
  - `rmw_cyclonedds_cpp`
  - subnet discovery

## 0.3 Workspace layout
The repo lives at `~/ros2_ws/src/pioneer3/` and contains all packages as subdirectories. The dashboard app is at `pioneer3/pioneer_dashboard_app/`. There should be **no** standalone `pioneer_dashboard_app/` directly under `src/` — if one exists from an earlier setup, delete it:
```bash
rm -rf ~/ros2_ws/src/pioneer_dashboard_app
```

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
If this repo vendors it into `~/ros2_ws/src`, you're done. If not:
```bash
cd ~/ros2_ws/src
git clone https://github.com/Slamtec/sllidar_ros2.git
```

> If your LiDAR ever shows `SL_RESULT_OPERATION_TIMEOUT`, see Troubleshooting §11.2.

## 2.2 Front camera (OAK‑D‑Pro): `depthai_ros_driver`
Install via apt if available on your setup:
```bash
sudo apt update
sudo apt install -y ros-jazzy-depthai-ros-driver
```
If apt is not available/desired, build from source (follow depthai_ros_driver docs).

### 2.2.1 OAK permissions (udev rules)
If you see "Insufficient permissions … X_LINK_UNBOOTED":
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

This project uses ARIA to send velocity commands to the Pioneer base and to control digital output pins.

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

## 6.1 Front camera low-latency option (recommended for live operation)

By default the OAK driver publishes **raw** and **compressed** streams. Over the network, the **compressed** stream is usually smoother.

### Option A (simplest): use the compressed topic in the dashboard
Set the dashboard front topic to:
- `/oak/rgb/image_raw/compressed`

### Option B (best compromise): republish compressed → raw on a local "_local" topic
This keeps the dashboard code path as `sensor_msgs/Image`, but uses the compressed feed under the hood.

Run on the **robot** (or as part of your bringup):
```bash
# Creates: /oak/rgb/image_raw_local (sensor_msgs/Image)
# Subscribes from: /oak/rgb/image_raw/compressed
ros2 run image_transport republish \
  --ros-args \
  -p in_transport:=compressed \
  -p out_transport:=raw \
  --remap in:=/oak/rgb/image_raw \
  --remap out:=/oak/rgb/image_raw_local \
  -r __node:=oak_image_republisher
```

Then set the dashboard front topic to:
- `/oak/rgb/image_raw_local`

**Important:** don't run multiple republishers at once. If you see duplicate node-name warnings for `/image_republisher`, stop them and restart the robot bringup:
```bash
pkill -f "image_transport republish" || true
pkill -f oak_image_republisher || true
sudo systemctl restart pioneer_robot.service
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

## 8.2 Build the dashboard (laptop only)
The dashboard package lives inside the `pioneer3` repo subdirectory. Use this command to build it — standard `colcon build` without `--base-paths` will not find it:
```bash
cd ~/ros2_ws
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```

Add this as an alias in `~/.bashrc` for convenience:
```bash
alias build_dashboard='cd ~/ros2_ws && colcon build --symlink-install --packages-select pioneer_dashboard_app --base-paths src/pioneer3/pioneer_dashboard_app'
```

## 8.3 Run the dashboard (laptop)
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
- Front: `/oak/rgb/image_raw/compressed`  *(recommended)*
  - Alternative: `/oak/rgb/image_raw_local` (if using §6.1 Option B)
  - Alternative: `/oak/rgb/image_raw` (raw)
- Rear: `/rear/image_raw`
- Scan: `/scan`
- cmd_vel: `/cmd_vel`

---

# 9) Torch / cue light toggle

The torch is a SEAMAGIC rechargeable work light wired through a 5V relay module. The relay input is driven by Pioneer digital output pin **OD6** (User I/O connector pin 14). The relay is powered from pin 18 (Vcc, 5V) and GND from pin 20.

**Important:** AUX power must be enabled on the Pioneer User Control Panel (press AUX1 until the red LED lights) for the 5V line on the IDC connector to be live. This is required for the relay to actuate.

## 9.1 How the toggle works
The light has 3 modes cycled by its internal button: High → Low → Strobe → Off. The dashboard toggle works as follows:

- **TORCH ON** (green button): sends a single 300ms pulse to OD6 → relay closes briefly → light advances to High mode
- **TORCH OFF** (red button): sends 3 pulses 500ms apart → cycles Low → Strobe → Off

Before trials, manually cycle the light to Off and leave it there. The dashboard ON button will always bring it to High on the first press.

## 9.2 ROS services
Two services are provided by the base controller:

| Service | Type | Effect |
|---|---|---|
| `/pioneer_base/torch_pulse` | `std_srvs/srv/Trigger` | Single pulse — turns light on (High mode) |
| `/pioneer_base/torch_off` | `std_srvs/srv/Trigger` | 3-pulse sequence — cycles to Off |

Test from laptop:
```bash
ros2 service list | grep torch
ros2 service call /pioneer_base/torch_pulse std_srvs/srv/Trigger "{}"
ros2 service call /pioneer_base/torch_off std_srvs/srv/Trigger "{}"
```

## 9.3 Tunable parameters
The pulse timing can be adjusted at runtime without rebuilding:
```bash
ros2 param set /pioneer_base torch_pulse_ms 300   # pulse width in ms (default 300)
ros2 param set /pioneer_base torch_gap_ms 500     # gap between off-sequence pulses in ms (default 500)
```

If the light lands on the wrong mode after TORCH OFF, increase `torch_gap_ms` in 100ms increments until it reliably lands on Off.

## 9.4 DIGOUT wiring reference
From the Pioneer 3 manual, User I/O connector (20-pin latching IDC):

| Pin | Signal | Use |
|---|---|---|
| 14 | OD6 | Relay IN signal |
| 18 | Vcc (5V) | Relay VCC |
| 20 | GND | Relay GND |

The ARCOS DIGOUT command (#30) takes a two-byte argument: **high byte = bit mask (which pins to change), low byte = new values**. Both bytes must be set correctly or ARCOS ignores the command silently.

---

# 10) Laptop-first Git workflow (recommended)

## 10.1 Edit on laptop → push
```bash
cd ~/ros2_ws/src/pioneer3
git pull                        # always pull before editing
git status
git add -A
git commit -m "Describe your change"
git push
```

## 10.2 Robot pulls + rebuild pioneer3 + restart service
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

## 10.3 Rebuild dashboard on laptop only
```bash
cd ~/ros2_ws
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```

No robot-side rebuild or restart needed for dashboard-only changes.

---

# 11) Troubleshooting (field-focused)

## 11.1 "Dashboard works but no LiDAR in the widget"
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

## 11.5 Front camera "lags" but topics look healthy
This usually means you're viewing **raw** frames over the network, or the OAK pipeline is doing extra work.

1) Check the compressed rate (laptop):
```bash
timeout 8 ros2 topic hz /oak/rgb/image_raw/compressed --window 50
```

2) Prefer the compressed workflow:
- Use `/oak/rgb/image_raw/compressed` directly in the dashboard **or**
- Use §6.1 Option B to republish compressed → `/oak/rgb/image_raw_local`

3) If you still need more headroom:
- Reduce OAK resolution / FPS in `config/oak_rgb_only.yaml`
- Keep the dashboard "latest-only" buffering + render tick enabled (drops old frames instead of building latency)

## 11.6 Torch button does nothing (dashboard stays grey)
The `service_is_ready()` check was removed from the torch button — if the button is completely unresponsive, click **Apply / Reconnect** in the Topics box to recreate the service clients, then try again.

## 11.7 Torch relay clicks but light doesn't respond
1) Confirm AUX1 LED is lit on the Pioneer User Control Panel. The 5V line on the IDC connector (pin 18) is AUX-switched — without AUX power the relay has no coil voltage.
2) If AUX is on and the relay clicks but the light doesn't change mode, the pulse timing may need adjustment. Increase `torch_gap_ms`:
```bash
ros2 param set /pioneer_base torch_gap_ms 700
```
3) If the relay doesn't click at all, measure pin 14 to pin 20 with a multimeter while calling `torch_pulse`. Should pulse 0V → 5V → 0V. If it stays high permanently, the service restart loop may not have fully restarted — run `sudo systemctl restart pioneer_robot.service`.

## 11.8 colcon can't find pioneer_dashboard_app
The dashboard is a subdirectory inside `pioneer3`, not a top-level package. Always build it with the explicit `--base-paths` flag:
```bash
colcon build --symlink-install \
  --packages-select pioneer_dashboard_app \
  --base-paths src/pioneer3/pioneer_dashboard_app
```
If you see `ignoring unknown package 'pioneer_dashboard_app'` without this flag, that is expected — use the alias from §8.2 instead.

---

# Appendix 2 — ARIA install paths (choose one)

There are two common ways ARIA shows up on a Pioneer system. Use **one**.

## A2.1 Option A: "System ARIA" already installed (common in labs)
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
- Torch on service: `/pioneer_base/torch_pulse` (`std_srvs/srv/Trigger`)
- Torch off service: `/pioneer_base/torch_off` (`std_srvs/srv/Trigger`)

List + types:
```bash
ros2 topic list -t | egrep "(/scan|/oak/rgb/image_raw|/rear/image_raw|/cmd_vel)"
ros2 service list | egrep "(torch_pulse|torch_off)"
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
ros2 service list | grep torch
```

---

# Appendix 4 — Day-of-experiment quick start (minimal steps)

This is the "don't think, just run" checklist.

## A4.1 Robot (power-on)
1. Power on the Pioneer 3-DX.
2. Press **AUX1** on the User Control Panel until the red LED lights — this enables 5V for the relay.
3. Wait ~1–2 minutes for Linux + `systemd` bringup to complete.
4. (Optional) Confirm bringup is running:
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

## A4.3 Torch pre-trial check
Before participants arrive:
1. Manually cycle the light to **Off** using its physical button.
2. Click **TORCH ON** in the dashboard — light should go to High (800 lm).
3. Click **TORCH OFF** — light should cycle back to Off.
4. If it lands on the wrong mode, run:
```bash
ros2 param set /pioneer_base torch_gap_ms 700
```
and repeat the test.

## A4.4 If something is missing
- If `/scan` is missing or unstable: reboot robot or disable ModemManager (Troubleshooting §11.2)
- If cameras are missing: replug USB cameras and restart `pioneer_robot.service`
- If torch doesn't respond: check AUX1 LED on User Control Panel (§11.7)

Restart service (robot):
```bash
ssh -t easel@<ROBOT_IP> 'sudo systemctl restart pioneer_robot.service'
```

---
