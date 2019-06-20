# RemotePlay

## Getting the source

Clone the repository with submodules:

    git clone --recurse-submodules https://github.com/simul/RemotePlay.git

## Prerequisites

1. Visual Studio 2017 (with Visual C++ tools for CMake)
2. Android Studio
3. Unreal Engine 4.22 incorporating the patch to move SceneCaptureSource from USceneCaptureComponent2D to USceneCaptureComponent
4. NVIDIA CUDA Toolkit 9 with patch.
5. NVIDIA Video Codec SDK
6. Recent CMake, and get ninja.exe and put it in C:\Program Files\CMake\bin
7. edit local.properties to contain cmake.dir=C\:\\Program Files\\CMake
	
## Building UE4 plugin

1. Using CMakeGUI, create a Visual Studio 2017 x64 build in the Libraries/libavstream subdirectory of plugins/UnrealDemo/Plugins/RemotePlay. In the Advanced CMake config settings, search for CXX_FLAGS and ensure that the configurations use the /MD and /MDd options: Unreal uses the dynamic runtimes so this is needed for compatibility.
2. Create the Cmake libavstream project and add it to the solution at plugins/UnrealDemo/UnrealDemo.sln. Make sure that the release build is configured to compile in Development Editor solution config.
3. Build libavstream, this creates libavstream.lib inplugins\UnrealDemo\Plugins\RemotePlay\Libraries\libavstream\lib\(CONFIG)
4. Build `UnrealDemo` UE4 project in `Development Editor` configuration.
5. Go to Edit->Editor Preferences, General->Performance and disable "Use Less CPU When in Background"
6. Put r.ShaderDevelopmentMode=1 in your ConsoleVariables.ini
7. (OPTIONAL) Package the project for `Windows 64-bit` platform. This is recommended for best performance during testing.

## Building the PC Client

## Building GearVR client application

1. Follow [Oculus Mobile SDK software setup guide](https://developer.oculus.com/documentation/mobilesdk/latest/concepts/mobile-studio-setup-android/).
2. [Generate an osig file](https://dashboard.oculus.com/tools/osig-generator/) for your device and place it in `client/VrProjects/Native/RemotePlayClient/assets` directory.
3. Connect your Android device to your PC and make sure that ADB debugging is enabled and the PC is authorized.
4. (old method) Go to `client/VrProjects/Native/RemotePlayClient/Projects/Android` and run `build.bat` to build and deploy.
5. New method: In Android Studio open RemotePlay/build.gradle.

## Running

1. Connect your Android device to the same WiFi network your development PC is on.
2. Make sure UE4 editor is not blocked by the Windows firewall.
3. Make sure the GearVR controller is paired with the Android device.
4. Run the game in the editor and then launch `RemotePlayClient` application on your Android device.

It may take up to a few seconds for GearVR controller to be recognized.

For best performance when testing with UE4 demo project run the packaged game in windowed mode, in 1024x768 resolution, with Low quality settings.

### Controls

| Control | Action |
|--|--|
| Swipe on trackpad up/down | Move forwards/backwards |
| Swipe on trackpad left/right | Strafe left/right |
| Click on trackpad | Jump |
| Trigger | Fire |

### Default network ports

| Protocol | Port  | Description |
| ---------|-------|-------------|
| UDP      | 10500 | Session control & player input
| UDP      | 10501 | Video stream
| UDP      | 10600 | Local network server discovery
