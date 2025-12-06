# 1. Purpose of This Manual
This document is written as a step-by-step instructional manual so that any MEE 625 student can recreate the project outcome on the Pioneer 3-DX platform.

<img width="1170" height="711" alt="image" src="https://github.com/user-attachments/assets/3ea92a4c-7c06-4d7f-bb2d-522cba070a1e" />

After following this manual, you should be able to:
- Control the Pioneer 3-DX with ROS 2 Jazzy using keyboard teleoperation.
- Run the Pioneer driver node (pioneer_aria).
- Run the LiDAR driver for the RPLIDAR A1M8 and get /scan data.
- Visualize robot odometry (/odom), TF frames, and LiDAR scans (/scan) in RViz2.
- Use Git and GitHub to pull and update the "pioneer3" ROS 2 package.

---

# 2. System Overview: High-Level Node Diagram
ROS2 Custom Package Name: pioneer3
ROS2 Node Structure:

<img width="1616" height="288" alt="image" src="https://github.com/user-attachments/assets/427b45fa-bffd-40e1-9b50-08bab7a6a370" />

where:
- pioneer_aria – low-level driver that talks to the Pioneer’s motor controller via the AriaCoda library.
- teleop_twist_keyboard – lets you send velocity commands with WASD-style keys.
- lidar_driver – publishes 2D laser scans from the RPLIDAR A1M8 on /scan.
- RViz2 + TF publishers – used to visualize robot pose and laser scans.

---

# 3. Hardware, Software, and Networking Requirements

## 3.1 Hardware
- Pioneer 3-DX robot (P3-DX) with onboard PC.
- RPLIDAR A1M8 sensor (USB interface).
- 3D-printed mount to raise the LiDAR above the robot antenna.
- Monitor, keyboard, and mouse for the robot’s onboard PC (at least for initial setup).
- Required for remote operation: a second laptop/PC on the same network.

## 3.2 Software (on the Robot's PC)
- Ubuntu 24.04.
- ROS 2 Jazzy.
- A ROS 2 workspace located at ~/ros2_ws with a src folder at ~/ros2_ws/src.
- Internet connection (wired or Wi-Fi) on the robot PC.

## 3.3 Networking Requirements (for Remote RViz via SSH)
If you want to run RViz on your own laptop instead of the robot’s monitor, the following must be true:
### 1. Same network – The Pioneer’s onboard PC and your laptop must be on the same network.
### 2. Same ROS 2 domain – ROS 2 uses a domain ID to separate systems. On both machines, set the same ID, e.g.:
```
echo 'export ROS_DOMAIN_ID=20' >> ~/.bashrc
```
```
source ~/.bashrc
```
### 3. Time sync – It is helpful (but not strictly required) if both machines have reasonably correct system clocks.

---

### 4. Assumptions and Conventions
- All commands are run on the robot’s onboard PC unless otherwise noted.
- The ROS 2 workspace is located at:
```
~/ros2_ws
```
```
~/ros2_ws/src
```
- "$USER" refers to your Linux username on the machine.
- You must have sudo privileges on the robot PC.

---

# 5. Preparing the Pioneer 3-DX Onboard PC (Ubuntu 24.04 + ROS 2 Jazzy)
By default, the Pioneer 3-DX does not come with ROS 2 installed, and the onboard PC may be running an older or non-standard Linux/ROS installation. This section outlines how to get to Ubuntu 24.04 + ROS 2 Jazzy on the robot’s onboard PC so that it can run the rest of the steps in this manual.

## 5.1 Choose Your Path to Ubuntu 24.04
You can either perform a clean installation of Ubuntu 24.04 (Option A) or upgrade from an existing Ubuntu/ROS installation (Option B). You only need to follow one of these options.

### 5.1.1 Option A – Fresh Install of Ubuntu 24.04 on the Onboard PC (Recommended)
1. On a separate computer, download the Ubuntu 24.04 Desktop ISO from the official Ubuntu website.
2. Create a bootable USB drive using a tool such as Rufus (Windows), balenaEtcher (Windows/Linux/macOS), or the Startup Disk Creator (on an existing Ubuntu machine).
3. Connect a monitor, keyboard, and mouse to the Pioneer’s onboard PC.
4. Insert the bootable USB drive into the Pioneer’s onboard PC and power it on.
5. Enter the BIOS/boot menu (typically by pressing Esc, F2, F10, F12, or Del depending on the hardware) and choose to boot from the USB drive.
6. In the Ubuntu installer, select "Install Ubuntu".
7. When prompted for installation type, choose the option that erases the existing disk andinstalls Ubuntu (unless you have a specific reason to keep the old OS).
8. Set a hostname for the robot, such as "pioneer-pc".
9. Create a user account (e.g., username "easel" or your preferred username) and choose a strong password.
10. Complete the installation and reboot when prompted.
11. After reboot, remove the USB drive and log into the new Ubuntu 24.04 system. Use this option if you want a clean, reproducible setup and do not need to preserve anything that was previously on the robot.

### 5.1.2 Option B – Upgrading from an Existing Ubuntu/ROS Installation
If the Pioneer’s onboard PC already has Ubuntu and ROS installed (for example, Ubuntu 20.04 with ROS Noetic or Ubuntu 22.04 with ROS 2 Humble), you can perform an in-place upgrade instead of wiping the disk. This is only recommended if you need to preserve existing data or configurations and are comfortable troubleshooting OS upgrades.
1. Check the current Ubuntu version:
```
lsb_release -a
```
- If you are already on Ubuntu 24.04, skip directly to Section 5.2 (Install ROS 2 Jazzy).
- If you are on Ubuntu 22.04, you can upgrade directly to 24.04.
- If you are on Ubuntu 20.04, you will typically upgrade first to 22.04, then to 24.04.
2. Back up important data and configs.
- Copy any important files (e.g., workspaces, custom scripts, configuration files) to an external drive or a network location.
- Note any special device rules (e.g., udev rules) you may want to recreate later.
3. Fully update the current system:
```
sudo apt update
```
```
sudo apt upgrade -y
```
```
sudo apt dist-upgrade -y
```
4. Run the Ubuntu release upgrade tool (from 20.04 → 22.04, or from 22.04 → 24.04): sudo do-release-upgrade
- Follow the on-screen instructions.• If it says "no new release found", ensure the system is set to track normal releases in "Software & Updates → Updates → Notify me of a new Ubuntu version".
5. Reboot when the upgrade is finished and confirm the new version:
```
lsb_release -a
```
- If you upgraded from 20.04 to 22.04, repeat the process once more from 22.04 → 24.04.
6. Remove old ROS versions (optional but recommended). For example, if you had ROS Noetic:
```
sudo apt remove 'ros-*'
```
```
sudo apt autoremove -y
```
7. Once the OS reports Ubuntu 24.04, continue with Section 5.2 to install ROS 2 Jazzy on the upgraded system.
Note: In-place upgrades can sometimes leave behind older configurations. If you encounter strange dependency or graphics issues later, a clean install (Option A) is usually easier to debug.

## 5.2 Install ROS 2 Jazzy
After Ubuntu 24.04 is installed and updated, install ROS 2 Jazzy by following the official ROS 2 instructions for Ubuntu 24.04. A typical sequence is:
1. Set up locale:
```
sudo apt update
sudo apt install locales
sudo locale-gen en_US en_US.UTF-8
sudo update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
export LANG=en_US.UTF-8
```
2. Add the ROS 2 apt repository and keys (see ROS 2 Jazzy docs for exact commands).
3. Update apt and install ROS 2 Jazzy Desktop:
```
sudo apt update
sudo apt install ros-jazzy-desktop
```
4. Install additional ROS 2 tools (useful for development):
```
sudo apt install ros-jazzy-ros-base ros-jazzy-rqt ros-jazzy-rviz2
```
5. Source ROS 2 in your shell (you will later automate this in Section 9):
```
source /opt/ros/jazzy/setup.bash
```
6. Confirm the installation by running, for example:
```
ros2 --help
```
If the command runs and prints usage information, ROS 2 Jazzy is installed correctly.

## 5.3 Update System and Drivers
Before continuing, it is good practice to fully update the system:
```
sudo apt update
sudo apt upgrade -y
```
If the Pioneer’s onboard PC requires any additional proprietary drivers (e.g., for Wi-Fi or graphics), use the "Software & Updates → Additional Drivers" tool in Ubuntu to install them. A stable network connection is particularly important for installing packages and using Git.

---

# 6. Basic Setup and Prerequisites

## 6.1 Create ROS 2 Workspace
Open a terminal and create the workspace:
```
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws
```

## 6.2 Give Serial Access to Your User (Robot and LiDAR)
To access serial devices such as /dev/ttyUSB* or /dev/ttyACM*, add your user to the dialout group:
```
sudo usermod -a -G dialout $USER
```

Log out and log back in (or reboot) for this change to take effect.

## 6.3 Install Basic Development Tools
Install Git, build tools, and ROS 2 utilities:
```
cd ~
sudo apt update
sudo apt install -y git make g++ \
python3-colcon-common-extensions python3-rosdep python3-vcstool
```

## 6.4 Initialize rosdep
Initialize rosdep (only once per machine):
```
cd ~
sudo rosdep init
rosdep update
# ignore "already exists" error if it appears
```

---

# 7. Install Pioneer Driver Stack (pioneer_ros2) and AriaCoda

## 7.1 Clone pioneer_ros2 (Drivers and Core Packages)
From the workspace source folder:
```
cd ~/ros2_ws/src
git clone https://github.com/grupo-avispa/pioneer_ros2.git
```

## 7.2 Install the AriaCoda Library
The pioneer_aria node depends on the AriaCoda (Aria) library.
Clone the repository:
```
cd ~
git clone https://github.com/grupo-avispa/AriaCoda.git
```

Build and install:
```
cd ~/AriaCoda
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

Verify installation:
```
ldconfig -p | grep AriaYou should see libAria.so listed under /usr/local/lib.
```

---

# 8. Add the "pioneer3" Project from GitHub

## 8.1 Configure Git Identity
Configure your Git identity so commits are tagged correctly:
```
cd ~
git config --global user.name "Your Name"
git config --global user.email "your_github_email@example.com"
```

## 8.2 Clone the Course Project Repository
From inside your workspace source folder:
```
cd ~/ros2_ws/src
git clone https://github.com/z1910335/MEE625_FinalProject.git pioneer3
```
This creates the ROS 2 package "pioneer3" in your workspace.

---

# 9. Set Up Your ROS 2 Environment (.bashrc)
To avoid manually sourcing ROS 2 every time you open a terminal, edit your ~/.bashrc file.

## 9.1 Edit ~/.bashrc
```
Open ~/.bashrc in a text editor:
cd ~
nano ~/.bashrc
```

## 9.2 Add the following lines at the end of the file:
```
# Source ROS 2 Jazzy
source /opt/ros/jazzy/setup.bash
# Source your workspace (if it exists)
if [ -f ~/ros2_ws/install/setup.bash ]; then
source ~/ros2_ws/install/setup.bash
```
## 9.3 Save and exit (Ctrl+X, Y, Enter) and then apply the changes:
```
source ~/.bashrc
```

---

# 10. GitHub Token for git pull and git push (If Needed)

## 10.1 Create Token
If the repository is private or you do not use SSH keys, you will need a GitHub Personal Access Token (PAT) for authentication.
On GitHub, go to:
Settings → Developer settings → Personal access tokens → Fine-grained tokens
Generate a new token with:
- Repository access: Only the project repository (e.g., z1910335/MEE625_FinalProject).
- Permissions: Contents → Read and Write.
Use this token as the password when performing git pull and git push.

## 10.2 Typical Git Workflow
Pull the latest changes before starting work:
```
cd ~/ros2_ws/src/pioneer3
git pull
```

After making changes:
```
cd ~/ros2_ws/src/pioneer3
git status
```
```
git add .
```
```
git commit -m "Short description of changes"
```
```
git push
```

When prompted for a password, paste your GitHub token.

---

# 11. Install Extra ROS 2 Packages (Teleop, LiDAR, Nav2)

## 11.1 Teleoperation Package
Install teleop_twist_keyboard:
```
sudo apt install -y ros-jazzy-teleop-twist-keyboard
```

## 11.2 LiDAR Driver
Install a LiDAR driver package (choose one depending on availability):
# Option: sllidar_ros2
```
sudo apt install -y ros-jazzy-sllidar-ros2
```
If no driver is available via apt, you can clone a driver repository into ~/ros2_ws/src, for example:
```
cd ~/ros2_ws/src
git clone https://github.com/Slamtec/sllidar_ros2.git
```
(Then rebuild the workspace in a later step.)

---

# 12. Install Dependencies with rosdep
From the workspace root, install missing dependencies for pioneer_ros2 and pioneer3:
```
cd ~/ros2_ws
rosdep install -i \
--from-path src/pioneer_ros2 src/pioneer3 \
--rosdistro jazzy -y
```

---

13. Build the Workspace with colcon
Build the relevant packages using colcon:
```
cd ~/ros2_ws
colcon build --symlink-install \
--packages-select pioneer_common pioneer_core pioneer_msgs pioneer_modules pioneer_aria pioneer3
```

If this is your first build, source the install setup script afterwards:
```
source install/setup.bash
```
If you added the sourcing lines to ~/.bashrc, new terminals will source this automatically.

--- 

# 14. Running the System
There are two ways to run the system:
- Directly on the pioneer3 (Testing & Developement - Display, Keyboard, Mouse plugged into robot)
- Remote control through SSH (Mobile robot controlled by computer on the same network)

## 14.1 Direct Startup
On the robot’s PC the first terminal window, start the launch file
```
cd ~/ros2_ws
ros2 launch pioneer3 pioneer3_bringup.launch.py
```
In a second terminal, start the keyboard control node
```
cd ~/ros2_ws
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

## 14.2 Remote Startup
In the first terminal window:
```
ssh -X easel@192.168.1.31
```
Input the password, then:
```
cd ~/ros2_ws
ros2 launch pioneer3 pioneer3_bringup.launch.py
```

In a second terminal:
```
ssh -X easel@192.168.1.31
```
Input the password, then:
```
cd ~/ros2_ws
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

---

# 15. Troubleshooting Guide

## 15.1 pioneer_aria Cannot Connect
- Ensure the robot is powered on and not in an error state.
- Confirm your user is in the dialout group (see Section 5.2).
- Check that the correct serial port is being used (e.g., /dev/ttyS0 or /dev/ttyUSB0).
- Inspect the node’s output for error messages about serial ports or permissions.

## 15.2 No /scan Data
Run:
```
ros2 topic list
```
and check if /scan is present.

If /scan is missing:
- Check LiDAR power and USB connection.
- Confirm the correct serial_port parameter in your LiDAR launch command.
- Check permissions on /dev/ttyUSB* devices.

If /scan exists but RViz shows nothing, verify topic and frame settings in RViz.

## 15.3 RViz2 Shows Nothing
- Ensure the Fixed Frame in RViz is set to a valid frame (e.g., odom or base_link).
- Add a TF display in RViz to verify that odom, base_link, and base_laser frames exist.
- Use ros2 topic echo to confirm that /odom and /scan are publishing data:
```
ros2 topic echo /odom
ros2 topic echo /scan
```

## 15.4 Build Errors with colcon
Re-run rosdep to ensure all dependencies are installed.
If necessary, clean and rebuild the workspace:
```
cd ~/ros2_ws
rm -rf build/ install/ log/
colcon build --symlink-install
```

---

# 16. Project Repository Structure (High-Level)
The project repository is organized as follows (simplified):

~/ros2_ws/src/
pioneer_ros2/ # Third-party driver stack
pioneer3/ # Course project package
CMakeLists.txt
package.xml
src/ # Nodes and utilities
launch/ # Launch files
README.md # Original project description and notes

As you extend the project, you can add new launch files to start the driver, LiDAR, and RViz together, and new source files for custom behaviors or visualizations.

---

# 17. Summary
This manual has described, in reproducible detail, how to:
- Prepare a ROS 2 workspace on the Pioneer 3-DX onboard PC.
- Install the pioneer_ros2 driver stack and the AriaCoda library.
- Clone and configure the pioneer3 course project package from GitHub.
- Install teleoperation and LiDAR driver packages.
- Build the workspace with colcon.
- Run the Pioneer driver, LiDAR, and teleoperation nodes.
- Visualize odometry and LiDAR scans in RViz2, both locally and via SSH.
A new student should be able to follow this manual from a fresh Ubuntu 24.04 + ROS 2 Jazzy installation on the Pioneer’s PC and reproduce the project outcome achieved in MEE 625.

---

# 18. References
1. pioneer_ros2 GitHub repository – Pioneer 3-DX ROS 2 driver and core packages. https://github.com/grupo-avispa/pioneer_ros2
2. AriaCoda (Aria) GitHub repository – Aria library used by Pioneer drivers. https://github.com/grupo-avispa/AriaCoda
3. ROS 2 Jazzy Documentation – Installation, colcon, rosdep, TF2, and RViz2 usage.https://docs.ros.org/en/jazzy/
4. teleop_twist_keyboard package – ROS 2 teleoperation package. https://github.com/ros2/teleop_twist_keyboard
5. RPLIDAR ROS driver – Example LiDAR driver repositories. https://github.com/Slamtec/sllidar_ros2
6. MEE625_FinalProject GitHub repository – Course project code for the pioneer3 package. https://github.com/z1910335/MEE625_FinalProject
