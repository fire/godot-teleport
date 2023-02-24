# Teleport VR

Teleport VR is an open, native network protocol for virtual and augmented reality.
This repository contains the reference Client/Server software and SDK for Teleport VR.
Comments, bug reports and pull requests are welcome.

## Getting the source

Clone the repository with submodules:

    git clone --recurse-submodules git@github.com:simul/Teleport.git

or if you have already cloned the main repo,

    git submodule update --init --recursive

## Prerequisites

1. Visual Studio 2019 or later (with Visual C++ tools for CMake)
4. NVIDIA CUDA Toolkit 11 https://developer.nvidia.com/cuda-downloads?target_os=Windows&target_arch=x86_64.
5. NVIDIA Video Codec SDK https://developer.nvidia.com/nvidia-video-codec-sdk
6. Recent CMake. Edit local.properties to contain cmake.dir=C\:\\Program Files\\CMake
7. ninja.exe: put it in C:\Program Files\CMake\bin
9. Nasm, to build OpenSSL. Get it from https://www.nasm.us/.
10. OpenXR, for the client. Get it from https://github.com/KhronosGroup/OpenXR-SDK.

## Building the PC Client

1. Build pthread.2019.sln in "\thirdparty\srt\submodules\pthread-win32\windows\VS2019" in Release x64.
	* You may retarget the projects to a more recent version of the build tools.
2. In firstparty/Platform, run Setup.py to build required libraries fmt and glfw.
3. Using CMakeGUI:
    * Set source code location to (Teleport Folder) and build the binaries at (Teleport Folder)/build_pc_client
    * Configure for x64 platform with default native compiler
    * In the Advanced CMake config settings, search for CXX_FLAGS and ensure that the configurations use the /MT and /MTd runtimes.
    * Uncheck 'BUILD_SHARED_LIBS', and 'USE_DYNAMIC_RUNTIME'.
    * Uncheck 'LIBAV_BUILD_SHARED_LIBS', and 'LIBAV_USE_DYNAMIC_RUNTIME'.
    * Uncheck 'ENABLE_ENCRYPTION' option from srt.
    * Set CMAKE_CUDA_COMPILER, LIBAV_CUDA_DIR and LIBAV_CUDA_SAMPLES_DIR to your installed Cuda version
4. Configure, generate, open and build the Visual Studio project *in Release Configuration first*.

## Firewall setup
1. Go to Windows Security->Firewall & Network->Advanced Settings.
2. Choose Inbound Rules->New Rule->Port->UDP.
3. Enter the Discovery Port and create the rule.
Repeat 2-3 for the the Service Port.

## Running

1. Connect your Android device to your local WiFi network (for a local server) or the internet (for a remote server).
2. On the server machine, make sure Unity or UE4 editor is not blocked by the Windows firewall.
3. Find the IP address of your server, either a local IP or a global IP or domain name.
4. Run the game in UE or Unity editor and then launch the client application on your Android or PCVR device.

### Default network ports

| Protocol | Port  | Description |
| ---------|-------|-------------|
| UDP      | 10500 | Session control & player input
| UDP      | 10501 | Video stream
| UDP      | 10600 | Local network server discovery

## Troubleshooting
1. If you can receive packets from the headset, but can't transmit to it, it may have an IP address conflict. Check no other device has the same IP.
