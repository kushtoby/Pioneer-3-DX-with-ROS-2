# MEE625_FinalProject Description
ROS2 package of the Final Project for MEE625 Robot Programming &amp; Control Fall2025.
<img width="1170" height="711" alt="image" src="https://github.com/user-attachments/assets/3ea92a4c-7c06-4d7f-bb2d-522cba070a1e" />

ROS2 Package Name: pioneer3

Robot: Pioneer 3 (P3-DX) by Mobile Robots Inc. (Located in Omron Lab)
- Has onboard computer than can run Ubuntu 24.04
- Has ROS2 Jazzy Installed
- Has Internet Connection
- Has ports that can establish connection to Monitor, Mouse, Keyboard, etc
- TODO: Remote connection while ROS2 Control Package is running

Sensor: LiDAR RPLIDAR A1M8
- 360&deg; scanning (rotation sensor)
- Will be mounted on top of the Pioneer 3
	- 3D-Printed design piece needed to raise sensor above P3's antenna 

Project Plan: Create a ROS2 Package that can control the robot
- pioneer_aria (driver) 
- Teleop Node (Manual Control)
- Sensor Node (Collect sensor data and publish it)
-  Display Node

ROS2 Node Structure:

	teleop_twist_keyboard  ──/cmd_vel──▶  pioneer_aria (driver)  ──/odom──▶  display_node
	                                                    │
	                                                    └──/tf (odom→base_link)
	lidar_driver  ─────────────/scan────────────────────────────────────────▶  display_node
	static_transform_publisher: base_link → base_laser


Stretch Goal (if time permits): 
- Implement some sort of autonomous function via additional nodes 
	- Zero compromise to functionality of manual control node structure

---

# Step 0: Prerequisites

Assumptions (No Instruction):
- Ubuntu 24.04
- ROS2 Jazzy
- ~/ros2_ws/src in your directory

## 0.1 Hardware Access (serial &amp; udev)

This is for machines that will open USB/serial ports (plug in) on the robot.

/dev/ttyUSB*//dev/ttyACM* (robot’s PC; laptop if you plug the robot or LiDAR into it).

Not needed for machines that will connect over the network (wifi).

```
sudo usermod -a -G dialout $USER   # log out/in afterwards
```

Log out and back in (or reboot) so the dialout group takes effect

## 0.2 Basic Tools
```
cd ~
sudo apt update
sudo apt install -y git make g++
# git: version control
# make: build tool used by AriaCoda
# g++: cpp compiler for building AriaCoda and cpp ros nodes
```
```
sudo apt install -y python3-colcon-common-extensions python3-rosdep python3-vcstool
```

## 0.3 Initialize rosdep (ROS2 Dependency Manager)
```
cd ~
sudo rosdep init # only once; ignore error if it already exists
rosdep update # refresh dependency database
```

---

# Step 1: Setup to Use ROS2 on the Pioneer 3 (P3-DX)

## 1.1 Clone pioneer_ros2 (Drivers &amp; Core Packages) from GitHub
```
cd ~/ros2_ws/src
git clone https://github.com/grupo-avispa/pioneer_ros2.git
```

## 1.2 Install Aria (AriaCoda Library) from GitHub:

### 1.2.1 Clone AriaCoda:
```
cd ~
git clone https://github.com/grupo-avispa/AriaCoda.git
```

### 1.2.2 Build &amp; Install AriaCoda
```
cd ~/AriaCoda
make -j"$(nproc)"   # build the library using all CPU cores
sudo make install   # install headers and libAria.so into /usr/local
sudo ldconfig       # refresh the dynamic linker cache
````

### 1.2.3 Verify
```
cd ~
ldconfig -p | grep Aria # Should see libAria.so under /usr/local/lib
```

---

# Step 2: Add this GitHub Project to Your Workspace

## 2.1 Configure Your Identity

The Name can be whatever you want to be called on GitHub.

The Email needs to be the same as the email on your GitHub account.
```
cd ~
git config --global user.name "Your Name"
git config --global user.email "your_email@example.com"
```

## 2.2 Clone the Project Repo as "pioneer3"
```
cd ~/ros2_ws/src
git clone https://github.com/z1910335/MEE625_FinalProject.git pioneer3
```

---

# Step 3 Setup the ROS2 Environment

## 3.1 Teleop &amp; LiDAR Driver
```
# Teleop and LiDAR driver (pick ONE LiDAR driver)
sudo apt install -y ros-jazzy-teleop-twist-keyboard
# If available on your system:
#   sudo apt install -y ros-jazzy-rplidar-ros
# or:
#   sudo apt install -y ros-jazzy-sllidar-ros2
# Otherwise build from source: https://github.com/Slamtec/sllidar_ros2 or https://github.com/Slamtec/rplidar_ros
```

## 3.2 Install Nav2 Utilities
```
cd ~
sudo apt update
sudo apt install -y ros-jazzy-navigation2
```

## 3.3 Install ROS2 Dependencies with rosdep
```
cd ~/ros2_ws
rosdep install -i \
  --from-path src/pioneer_ros2 src/pioneer3 \
  --rosdistro jazzy -y
```

## 3.4 Sourcing

Sourcing for every new Terminal is annoying.

Add sourcing to ```~/.bashrc.``` so ROS2 and Install are sourced Automatically.

### 3.4.1
```
cd ~
nano ~/.bashrc
```

### 3.4.2

Add the following lines at the end of the script (if not already present):
```
source /opt/ros/jazzy/setup.bash
```
```
source ~/ros2_ws/install/setup.bash
```

### 3.4.3

Exit Nano:
- **ctrl** + **x** to Exit
- Select **Y** to confirm and save changes
- Press **Enter** to return to the Terminal

### 3.4.4

Restart Terminal or ```source ~/.bashrc``` after this step to implement the changes

---

# Step 4: GitHub Token for ```git pull``` &amp; ```git push```

For private repositories, tokens are required.

## 4.1 Go to Settings &rarr; Developer Settings &rarr; Personal Access Tokens &rarr; Fine-Grained Tokens

## 4.2 Create a New Token:

- Repository Access: **Only Select Repositories**
- Choose **z1910335/MEE625_FinalProject**
- Under Repository Permissions, Click **Add Permissions**:
- Contents &rarr; Read and Write
		
## 4.3 Generate the Token and COPY IT TO SOMEWHERE SAFE TO USE LATER 

This is now the "Password" You use to ```git pull``` &amp; ```git push```
Anyone with this token can push to the repo as you!

---

# STEP 5: ```git pull``` &amp; ```git push```

Always start by pulling the latest version from GitHub BEFORE pushing your changes

## 5.1 Pull
```
cd ~/ros2_ws/src/pioneer3
git pull
```
- Enter your GitHub Username
- Paste you Token into the Terminal: **ctrl** + **Shift** + **V** then press **Enter**

## 5.2 Push
```
cd ~/ros2_ws/src/pioneer3
```
```
git status # See what has changed. Pull if your local version is not up-to-date
```
```
git add . # Stage new or changed files
```
```
git commit -m "Add-your-description-of-changes-here-in-the-quotes"
```
```
git push
```

---

STEP 6: Building the Package &amp; Running Nodes

```
cd ~/ros2_ws
colcon build --symlink-install \
  --packages-select pioneer_common pioneer_core pioneer_msgs pioneer_modules pioneer_aria pioneer3
```

IFF this is your first time building the package (install didn't exist until now: Need to source it!)
```
source install/setup.bash # or ```. install/setup.bash``` Fun Fact: . and **source** are synonymous!
```

```
ros2 run pioneer3 <node_executable>
```

TODO: Instructions using the launch file
