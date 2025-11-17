This is example of opencl/gpu tracker for orange pi 5. (12 fps).
Before building project be sure you have opencl installed and mali driver works!

cd /usr/lib
sudo wget https://github.com/JeffyCN/mirrors/raw/libmali/lib/aarch64-linux-gnu/libmali-valhall-g610-g6p0-x11-wayland-gbm.so -O libmali-g610.so

cd /lib/firmware
sudo wget https://github.com/JeffyCN/mirrors/raw/libmali/firmware/g610/mali_csffw.bin

sudo apt update
sudo apt install mesa-opencl-icd -y
sudo mkdir -p /etc/OpenCL/vendors

echo "/usr/lib/libmali-g610.so" | sudo tee /etc/OpenCL/vendors/mali.icd

#install opencl:
sudo apt install ocl-icd-opencl-dev -y
sudo apt install libxcb-dri2-0 libxcb-dri3-0 libwayland-client0 libwayland-server0 libx11-xcb1 -y
sudo apt install clinfo -y

#Check clinfo:
clinfo

Remove all files in /build

cmake ..
make

#Run
./visual_tracker

Be sure you have non-gui linux and mouse connected to usb
