#include "UseOpenXR.h"
#include <vector>
#if IS_D3D12
#else
#include <d3d11.h>
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "fmt/core.h"
#include "Platform/CrossPlatform/Quaterniond.h"
#include "Platform/CrossPlatform/AxesStandard.h"

using namespace std;
using namespace simul;
using namespace teleport;

struct XrGraphicsBindingPlatform
{
	XrStructureType             type;
	const void* XR_MAY_ALIAS    next;
	crossplatform::RenderPlatform* renderPlatform;
} ;

struct XrSwapchainImagePlatform
{
	XrStructureType      type;
	void* XR_MAY_ALIAS    next;
	crossplatform::Texture* texture;
} ;

struct swapchain_surfdata_t
{
	crossplatform::Texture* depth_view;
	crossplatform::Texture* target_view;
};
struct swapchain_t
{
	XrSwapchain handle;
	int32_t     width=0;
	int32_t     height=0;
	uint32_t	last_img_id=0;
	vector<XrSwapchainImageD3D11KHR> surface_images;
	vector<swapchain_surfdata_t>     surface_data;
};

void swapchain_destroy(swapchain_t& swapchain)
{
	for (uint32_t i = 0; i < swapchain.surface_data.size(); i++)
	{
		delete swapchain.surface_data[i].depth_view;
		delete swapchain.surface_data[i].target_view;
	}
}
struct input_state_t
{
	XrActionSet actionSet;
	XrAction    poseAction;
	XrAction    selectAction;
	XrAction    showMenuAction;
	XrAction	triggerAction;
	XrPath   handSubactionPath[2];
	XrSpace  handSpace[2];
	XrPosef  handPose[2];
	float	trigger[2];
	XrBool32 renderHand[2];
	XrBool32 handSelect[2];
	XrBool32 handMenu[2];
};

const XrPosef  xr_pose_identity = { {0,0,0,1}, {0,0,0} };
XrInstance     xr_instance = {};
XrSession      xr_session = {};
XrSessionState xr_session_state = XR_SESSION_STATE_UNKNOWN;
bool           xr_running = false;
XrSpace        xr_app_space = {};
XrSpace        xr_head_space = {};
XrSystemId     xr_system_id = XR_NULL_SYSTEM_ID;
input_state_t  xr_input = { };
XrEnvironmentBlendMode   xr_blend = {};
XrDebugUtilsMessengerEXT xr_debug = {};
vector<XrView>                  xr_views;
vector<XrViewConfigurationView> xr_config_views;
vector<swapchain_t>             xr_swapchains;

// Function pointers for some OpenXR extension methods we'll use.
PFN_xrGetD3D11GraphicsRequirementsKHR ext_xrGetD3D11GraphicsRequirementsKHR = nullptr;
PFN_xrCreateDebugUtilsMessengerEXT    ext_xrCreateDebugUtilsMessengerEXT = nullptr;
PFN_xrDestroyDebugUtilsMessengerEXT   ext_xrDestroyDebugUtilsMessengerEXT = nullptr;

struct app_transform_buffer_t
{
	float world[16];
	float viewproj[16];
};

XrFormFactor            app_config_form = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
XrViewConfigurationType app_config_view = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

swapchain_surfdata_t CreateSurfaceData(crossplatform::RenderPlatform *renderPlatform,XrBaseInStructure& swapchain_img,int64_t d3d_swapchain_fmt)
{
	swapchain_surfdata_t result = {};

	// Get information about the swapchain image that OpenXR made for us!
	XrSwapchainImageD3D11KHR& d3d_swapchain_img = (XrSwapchainImageD3D11KHR&)swapchain_img;
	result.target_view				= renderPlatform->CreateTexture("swapchain target");
	result.target_view->InitFromExternalTexture2D(renderPlatform, d3d_swapchain_img.texture, nullptr,0,0,simul::crossplatform::UNKNOWN,true);
	result.depth_view				= renderPlatform->CreateTexture("swapchain depth");
	simul::crossplatform::TextureCreate textureCreate = {};
	textureCreate.numOfSamples		= 1;
	textureCreate.mips				= 1;
	textureCreate.w					= result.target_view->width;
	textureCreate.l					= result.target_view->length;
	textureCreate.arraysize			= 1;
	textureCreate.f					= simul::crossplatform::PixelFormat::D_32_FLOAT;
	textureCreate.setDepthStencil	= true;
	textureCreate.computable		= false;
	result.depth_view->EnsureTexture(renderPlatform, &textureCreate);

	return result;
}
static bool CheckXrResult(XrResult res)
{
	return (res == XR_SUCCESS);
}

bool UseOpenXR::Init(crossplatform::RenderPlatform *r,const char* app_name)
{
	// OpenXR will fail to initialize if we ask for an extension that OpenXR
	// can't provide! So we need to check our all extensions before 
	// initializing OpenXR with them. Note that even if the extension is 
	// present, it's still possible you may not be able to use it. For 
	// example: the hand tracking extension may be present, but the hand
	// sensor might not be plugged in or turned on. There are often 
	// additional checks that should be made before using certain features!
	vector<const char*> use_extensions;
	const char* ask_extensions[] = {
		XR_KHR_D3D11_ENABLE_EXTENSION_NAME, // Use Direct3D11 for rendering
		XR_EXT_DEBUG_UTILS_EXTENSION_NAME,  // Debug utils for extra info
	};
	renderPlatform = r;
	// We'll get a list of extensions that OpenXR provides using this 
	// enumerate pattern. OpenXR often uses a two-call enumeration pattern 
	// where the first call will tell you how much memory to allocate, and
	// the second call will provide you with the actual data!
	uint32_t ext_count = 0;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
	vector<XrExtensionProperties> xr_exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_exts.data());

	std::cout<<"OpenXR extensions available:\n";
	for (size_t i = 0; i < xr_exts.size(); i++)
	{
		std::cout<<fmt::format("- {}\n", xr_exts[i].extensionName).c_str();

		// Check if we're asking for this extensions, and add it to our use 
		// list!
		for (int32_t ask = 0; ask < _countof(ask_extensions); ask++) {
			if (strcmp(ask_extensions[ask], xr_exts[i].extensionName) == 0) {
				use_extensions.push_back(ask_extensions[ask]);
				break;
			}
		}
	}
	// If a required extension isn't present, you want to ditch out here!
	// It's possible something like your rendering API might not be provided
	// by the active runtime. APIs like OpenGL don't have universal support.
	if (!std::any_of(use_extensions.begin(), use_extensions.end(),
		[](const char* ext)
			{
				return strcmp(ext, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
			}))
		return false;

	// Initialize OpenXR with the extensions we've found!
	XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.enabledExtensionCount =(uint32_t) use_extensions.size();
	createInfo.enabledExtensionNames = use_extensions.data();
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	strcpy_s(createInfo.applicationInfo.applicationName, app_name);
	xrCreateInstance(&createInfo, &xr_instance);

	// Check if OpenXR is on this system, if this is null here, the user 
	// needs to install an OpenXR runtime and ensure it's active!
	if (xr_instance == nullptr)
		return false;

	// Load extension methods that we'll need for this application! There's a
	// couple ways to do this, and this is a fairly manual one. Chek out this
	// file for another way to do it:
	// https://github.com/maluoi/StereoKit/blob/master/StereoKitC/systems/platform/openxr_extensions.h
	xrGetInstanceProcAddr(xr_instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)(&ext_xrCreateDebugUtilsMessengerEXT));
	xrGetInstanceProcAddr(xr_instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)(&ext_xrDestroyDebugUtilsMessengerEXT));
	xrGetInstanceProcAddr(xr_instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)(&ext_xrGetD3D11GraphicsRequirementsKHR));

	// Set up a really verbose debug log! Great for dev, but turn this off or
	// down for final builds. WMR doesn't produce much output here, but it
	// may be more useful for other runtimes?
	// Here's some extra information about the message types and severities:
	// https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#debug-message-categorization
#if TELEPORT_INTERNAL
	XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	debug_info.messageTypes =
		XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
	debug_info.messageSeverities =
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debug_info.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data) {
		// Print the debug message we got! There's a bunch more info we could
		// add here too, but this is a pretty good start, and you can always
		// add a breakpoint this line!
		std::cout<<fmt::format("{}: {}\n", msg->functionName, msg->message).c_str()<<std::endl;

		// Output to debug window
		std::cout<<fmt::format( "{}: {}", msg->functionName, msg->message).c_str() << std::endl;

		// Returning XR_TRUE here will force the calling function to fail
		return (XrBool32)XR_FALSE;
	};
	// Start up the debug utils!
	if (ext_xrCreateDebugUtilsMessengerEXT)
		ext_xrCreateDebugUtilsMessengerEXT(xr_instance, &debug_info, &xr_debug);
#endif
	// Request a form factor from the device (HMD, Handheld, etc.)
	XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = app_config_form;
	if (!CheckXrResult(xrGetSystem(xr_instance, &systemInfo, &xr_system_id)))
	{
		std::cerr << fmt::format("Failed to Get XR System\n").c_str() << std::endl;
		return false;
	}

	// Check what blend mode is valid for this device (opaque vs transparent displays)
	// We'll just take the first one available!
	uint32_t blend_count = 0;
	xrEnumerateEnvironmentBlendModes(xr_instance, xr_system_id, app_config_view, 1, &blend_count, &xr_blend);

	// OpenXR wants to ensure apps are using the correct graphics card, so this MUST be called 
	// before xrCreateSession. This is crucial on devices that have multiple graphics cards, 
	// like laptops with integrated graphics chips in addition to dedicated graphics cards.
	XrGraphicsRequirementsD3D11KHR requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
	ext_xrGetD3D11GraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);

	// A session represents this application's desire to display things! This is where we hook up our graphics API.
	// This does not start the session, for that, you'll need a call to xrBeginSession, which we do in openxr_poll_events
	XrGraphicsBindingD3D11KHR binding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
	binding.device = renderPlatform->AsD3D11Device();
	XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &binding;
	sessionInfo.systemId = xr_system_id;
	if (!CheckXrResult(xrCreateSession(xr_instance, &sessionInfo, &xr_session)))
	{
		std::cerr<<fmt::format("Failed to create XR Session\n").c_str() << std::endl;
		return false;
	}

	// Unable to start a session, may not have an MR device attached or ready
	if (xr_session == nullptr)
		return false;

	// OpenXR uses a couple different types of reference frames for positioning content, we need to choose one for
	// displaying our content! STAGE would be relative to the center of your guardian system's bounds, and LOCAL
	// would be relative to your device's starting location. HoloLens doesn't have a STAGE, so we'll use LOCAL.
	XrReferenceSpaceCreateInfo ref_space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	ref_space.poseInReferenceSpace = xr_pose_identity;
	ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	xrCreateReferenceSpace(xr_session, &ref_space, &xr_app_space);

	ref_space.poseInReferenceSpace = xr_pose_identity;
	ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	xrCreateReferenceSpace(xr_session, &ref_space, &xr_head_space);
	
	// Now we need to find all the viewpoints we need to take care of! For a stereo headset, this should be 2.
	// Similarly, for an AR phone, we'll need 1, and a VR cave could have 6, or even 12!
	uint32_t view_count = 0;
	xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, app_config_view, 0, &view_count, nullptr);
	xr_config_views.resize(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	xr_views.resize(view_count, { XR_TYPE_VIEW });
	xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, app_config_view, view_count, &view_count, xr_config_views.data());
	int64_t swapchain_format = 0;
	// Find out what format to use:
	uint32_t formatCount = 0;
	XrResult res = xrEnumerateSwapchainFormats(
		xr_session,
		0,
		&formatCount,
		nullptr);
	if (!formatCount)
		return false;
	std::vector<int64_t> formats(formatCount);
	res = xrEnumerateSwapchainFormats(
		xr_session,
		formatCount,
		&formatCount,
		formats.data());
	if (std::find(formats.begin(), formats.end(), swapchain_format) == formats.end())
		swapchain_format = formats[0];

	for (uint32_t i = 0; i < view_count; i++)
	{
		// Create a swapchain for this viewpoint! A swapchain is a set of texture buffers used for displaying to screen,
		// typically this is a backbuffer and a front buffer, one for rendering data to, and one for displaying on-screen.
		// A note about swapchain image format here! OpenXR doesn't create a concrete image format for the texture, like 
		// DXGI_FORMAT_R8G8B8A8_UNORM. Instead, it switches to the TYPELESS variant of the provided texture format, like 
		// DXGI_FORMAT_R8G8B8A8_TYPELESS. When creating an ID3D11RenderTargetView for the swapchain texture, we must specify
		// a concrete type like DXGI_FORMAT_R8G8B8A8_UNORM, as attempting to create a TYPELESS view will throw errors, so 
		// we do need to store the format separately and remember it later.
		XrViewConfigurationView& view = xr_config_views[i];
		XrSwapchainCreateInfo    swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		XrSwapchain              handle;
		swapchain_info.arraySize = 1;
		swapchain_info.mipCount		= 1;
		swapchain_info.faceCount	= 1;
		swapchain_info.format		= swapchain_format;
		swapchain_info.width		= view.recommendedImageRectWidth;
		swapchain_info.height		= view.recommendedImageRectHeight;
		swapchain_info.sampleCount	= view.recommendedSwapchainSampleCount;
		swapchain_info.usageFlags	= XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		xrCreateSwapchain(xr_session, &swapchain_info, &handle);

		// Find out how many textures were generated for the swapchain
		uint32_t surface_count = 0;
		xrEnumerateSwapchainImages(handle, 0, &surface_count, nullptr);

		// We'll want to track our own information about the swapchain, so we can draw stuff onto it! We'll also create
		// a depth buffer for each generated texture here as well with make_surfacedata.
		swapchain_t swapchain = {};
		swapchain.width = swapchain_info.width;
		swapchain.height = swapchain_info.height;
		swapchain.handle = handle;
		swapchain.surface_images.resize(surface_count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
		swapchain.surface_data.resize(surface_count);
		xrEnumerateSwapchainImages(swapchain.handle, surface_count, &surface_count, (XrSwapchainImageBaseHeader*)swapchain.surface_images.data());
		for (uint32_t i = 0; i < surface_count; i++)
		{
			swapchain.surface_data[i] = CreateSurfaceData(renderPlatform,(XrBaseInStructure&)swapchain.surface_images[i],swapchain_format);
		}
		xr_swapchains.push_back(swapchain);
	}

	haveXRDevice = true;
	return true;
}


void UseOpenXR::MakeActions()
{
	XrActionSetCreateInfo actionset_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy_s(actionset_info.actionSetName, "teleport_client");
	strcpy_s(actionset_info.localizedActionSetName, "TeleportClient");
	xrCreateActionSet(xr_instance, &actionset_info, &xr_input.actionSet);
	xrStringToPath(xr_instance, "/user/hand/left", &xr_input.handSubactionPath[0]);
	xrStringToPath(xr_instance, "/user/hand/right", &xr_input.handSubactionPath[1]);

	// Create an action to track the position and orientation of the hands! This is
	// the controller location, or the center of the palms for actual hands.
	XrActionCreateInfo action_info = { XR_TYPE_ACTION_CREATE_INFO };
	action_info.countSubactionPaths = _countof(xr_input.handSubactionPath);
	action_info.subactionPaths = xr_input.handSubactionPath;
	action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy_s(action_info.actionName, "hand_pose");
	strcpy_s(action_info.localizedActionName, "Hand Pose");
	xrCreateAction(xr_input.actionSet, &action_info, &xr_input.poseAction);

	// Create an action for listening to the select action! This is primary trigger
	// on controllers, and an airtap on HoloLens
	action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy_s(action_info.actionName, "select");
	strcpy_s(action_info.localizedActionName, "Select");
	xrCreateAction(xr_input.actionSet, &action_info, &xr_input.selectAction);

	// Action for trigger press
	action_info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy_s(action_info.actionName, "trigger");
	strcpy_s(action_info.localizedActionName, "trigger");
	xrCreateAction(xr_input.actionSet, &action_info, &xr_input.triggerAction);

	// Create an action for listening to the "show menu" action.
	action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy_s(action_info.actionName, "menu");
	strcpy_s(action_info.localizedActionName, "Menu");
	xrCreateAction(xr_input.actionSet, &action_info, &xr_input.showMenuAction);

	// Bind the actions we just created to specific locations on the Khronos simple_controller
	// definition! These are labeled as 'suggested' because they may be overridden by the runtime
	// preferences. For example, if the runtime allows you to remap buttons, or provides input
	// accessibility settings.
	XrPath profile_path;
	XrPath pose_path[2];
	XrPath select_path[2];
	XrPath menu_path[2];
	XrPath trigger_path[2];
	XrPath joystick_path_x[2];
	XrPath joystick_path_y[2];
	xrStringToPath(xr_instance, "/user/hand/left/input/grip/pose", &pose_path[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/grip/pose", &pose_path[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/trigger/click", &select_path[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/trigger/click", &select_path[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/trigger/value", &trigger_path[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/trigger/value", &trigger_path[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/b/click", &menu_path[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/b/click", &menu_path[1]);

	xrStringToPath(xr_instance, "/user/hand/left/input/thumbstick/x", &joystick_path_x[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/thumbstick/x",&joystick_path_x[1]);
	
	xrStringToPath(xr_instance, "/interaction_profiles/valve/index_controller", &profile_path);
		///interaction_profiles/khr/simple_controller", &profile_path);
	XrActionSuggestedBinding bindings[] = {
		{ xr_input.poseAction,   pose_path[0]   },
		{ xr_input.poseAction,   pose_path[1]   },
		{ xr_input.selectAction, select_path[0] },
		{ xr_input.selectAction, select_path[1] },
		{ xr_input.showMenuAction, menu_path[0] },
		{ xr_input.showMenuAction, menu_path[1] },
		{ xr_input.triggerAction, trigger_path[0] },
		{ xr_input.triggerAction, trigger_path[1] }, };
	XrInteractionProfileSuggestedBinding suggested_binds = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested_binds.interactionProfile = profile_path;
	suggested_binds.suggestedBindings = &bindings[0];
	suggested_binds.countSuggestedBindings = _countof(bindings);
	XrResult result=xrSuggestInteractionProfileBindings(xr_instance, &suggested_binds);
	if (!XR_UNQUALIFIED_SUCCESS(result))
	{
		std::cerr << "xrSuggestInteractionProfileBindings failed " << result << std::endl;
	}
	// Create frames of reference for the pose actions
	for (int32_t i = 0; i < 2; i++)
	{
		XrActionSpaceCreateInfo action_space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		action_space_info.action = xr_input.poseAction;
		action_space_info.poseInActionSpace = xr_pose_identity;
		action_space_info.subactionPath = xr_input.handSubactionPath[i];
		xrCreateActionSpace(xr_session, &action_space_info, &xr_input.handSpace[i]);
	}

	// Attach the action set we just made to the session
	XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach_info.countActionSets = 1;
	attach_info.actionSets = &xr_input.actionSet;
	xrAttachSessionActionSets(xr_session, &attach_info);
}


void UseOpenXR::PollActions()
{
	if (xr_session_state != XR_SESSION_STATE_FOCUSED)
		return;

	// Update our action set with up-to-date input data!
	XrActiveActionSet action_set = { };
	action_set.actionSet = xr_input.actionSet;
	action_set.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &action_set;

	xrSyncActions(xr_session, &sync_info);

	// Now we'll get the current states of our actions, and store them for later use
	for (uint32_t hand = 0; hand < 2; hand++)
	{
		while (hand >= controllerStates.size())
			controllerStates.push_back(teleport::client::ControllerState());
		controllerStates[hand].analogueEvents.clear();
		controllerStates[hand].binaryEvents.clear();
		controllerStates[hand].motionEvents.clear();
		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		get_info.subactionPath = xr_input.handSubactionPath[hand];

		XrActionStatePose pose_state = { XR_TYPE_ACTION_STATE_POSE };
		get_info.action = xr_input.poseAction;
		xrGetActionStatePose(xr_session, &get_info, &pose_state);
		xr_input.renderHand[hand] = pose_state.isActive;

		// Events come with a timestamp
		XrActionStateBoolean select_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		get_info.action = xr_input.selectAction;
		xrGetActionStateBoolean(xr_session, &get_info, &select_state);
		xr_input.handSelect[hand] = select_state.currentState && select_state.changedSinceLastSync;

		get_info.action = xr_input.showMenuAction;
		xrGetActionStateBoolean(xr_session, &get_info, &select_state);
		xr_input.handMenu[hand] = select_state.currentState && select_state.changedSinceLastSync;
		// If we have a select event, update the hand pose to match the event's timestamp
		if (xr_input.handSelect[hand])
		{
			XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
			XrResult        res = xrLocateSpace(xr_input.handSpace[hand], xr_app_space, select_state.lastChangeTime, &space_location);
			if (XR_UNQUALIFIED_SUCCESS(res) &&
				(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
				(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
			{
				xr_input.handPose[hand] = space_location.pose;
				if(hand>= controllerPoses.size())
					controllerPoses.resize(hand+1);
				controllerPoses[hand].position = crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3*)&space_location.pose.position));
				controllerPoses[hand].orientation = crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec4*)&space_location.pose.orientation));
			}
		}
		XrActionStateFloat trigger_state = { XR_TYPE_ACTION_STATE_FLOAT };
		get_info.action = xr_input.triggerAction;
		xrGetActionStateFloat(xr_session, &get_info, &trigger_state);
		xr_input.trigger[hand] = trigger_state.currentState;
		if (controllerStates[hand].triggerBack != xr_input.trigger[hand])
		{
			controllerStates[hand].triggerBack = xr_input.trigger[hand];
			controllerStates[hand].addAnalogueEvent(avs::InputId::TRIGGER01, controllerStates[hand].triggerBack);
		}
		if (xr_input.handMenu[hand])
		{
			menuButtonHandler();
		}
	}
}

void UseOpenXR::openxr_poll_predicted(XrTime predicted_time)
{
	if (xr_session_state != XR_SESSION_STATE_FOCUSED)
		return;

	// Update hand position based on the predicted time of when the frame will be rendered! This 
	// should result in a more accurate location, and reduce perceived lag.
	for (size_t i = 0; i < 2; i++)
	{
		if (!xr_input.renderHand[i])
			continue;
		XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
		XrResult        res = xrLocateSpace(xr_input.handSpace[i], xr_app_space, predicted_time, &space_location);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
		{
			xr_input.handPose[i] = space_location.pose;
			if (i >= controllerPoses.size())
				controllerPoses.resize(i + 1);
			controllerPoses[i].position = crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3*)&space_location.pose.position));
			controllerPoses[i].orientation = crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec4*)&space_location.pose.orientation));
		}
	}
}

void app_update_predicted()
{
	// Update the location of the hand cubes. This is done after the inputs have been updated to 
	// use the predicted location, but during the render code, so we have the most up-to-date location.
	//if (app_cubes.size() < 2)
	//	app_cubes.resize(2, xr_pose_identity);
	//for (uint32_t i = 0; i < 2; i++) {
	//	app_cubes[i] = xr_input.renderHand[i] ? xr_input.handPose[i] : xr_pose_identity;
	//}
}

 mat4  AffineTransformation(vec4 q,vec3 p)
{
	 simul::crossplatform::Quaternion<float> rotation = (const float*)&q;
	 vec3 Translation = (const float*)&p;
	 vec4 VTranslation = { Translation.x,Translation.y,Translation.z,1.0f };
	 mat4 M;
	 simul::crossplatform::QuaternionToMatrix(M, rotation);
	 vec4 row3 = M.M[3];
	 row3 = row3 + VTranslation;
	 M.M[3][0] = row3.x;
	 M.M[3][1] = row3.y;
	 M.M[3][2] = row3.z;
	 M.M[3][3] = row3.w;
	 return M;
}

mat4 MatrixPerspectiveOffCenterRH
(
	float ViewLeft,
	float ViewRight,
	float ViewBottom,
	float ViewTop,
	float NearZ,
	float FarZ
)
{
	float TwoNearZ = NearZ + NearZ;
	float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
	float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
	float fRange = FarZ / (NearZ - FarZ);

	mat4 M;
	M.M[0][0] = TwoNearZ * ReciprocalWidth;
	M.M[0][1] = 0.0f;
	M.M[0][2] = 0.0f;
	M.M[0][3] = 0.0f;

	M.M[1][0] = 0.0f;
	M.M[1][1] = TwoNearZ * ReciprocalHeight;
	M.M[1][2] = 0.0f;
	M.M[1][3] = 0.0f;

	M.M[2][0] = (ViewLeft + ViewRight) * ReciprocalWidth;
	M.M[2][1] = (ViewTop + ViewBottom) * ReciprocalHeight;
	M.M[2][2] = 0.f;// NearZ / (FarZ - NearZ); //fRange;
	M.M[2][3] = -1.0f;

	M.M[3][0] = 0.0f;
	M.M[3][1] = 0.0f;
	M.M[3][2] = NearZ;// FarZ* NearZ / (FarZ - NearZ); //fRange * NearZ;
	M.M[3][3] = 0.0f;
	return M;
}
mat4 xr_projection(XrFovf fov, float clip_near, float clip_far)
{
	const float left = clip_near * tanf(fov.angleLeft);
	const float right = clip_near * tanf(fov.angleRight);
	const float down = clip_near * tanf(fov.angleDown);
	const float up = clip_near * tanf(fov.angleUp);

	return MatrixPerspectiveOffCenterRH(left, right, down, up, clip_near, clip_far);
}

void UseOpenXR::RenderLayer(simul::crossplatform::GraphicsDeviceContext& deviceContext,XrCompositionLayerProjectionView& view
	, swapchain_surfdata_t& surface, simul::crossplatform::RenderDelegate& renderDelegate,vec3 origin)
{
	// Set up camera matrices based on OpenXR's predicted viewpoint information
	mat4 proj = xr_projection(view.fov, 0.1f, 200.0f);
	crossplatform::Quaternionf rot = crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const crossplatform::Quaternionf*)&view.pose.orientation));
	vec3 pos=crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL,crossplatform::AxesStandard::Engineering,*((const vec3 *)&view.pose.position));
	//mat4 invview = AffineTransformation(rot, pos);
	pos += origin;
	deviceContext.viewStruct.proj = *((const simul::math::Matrix4x4*)&proj); 


	simul::geometry::SimulOrientation globalOrientation;
	// global pos/orientation:
	globalOrientation.SetPosition((const float*)&pos);

	simul::math::Quaternion q0(3.1415926536f / 2.0f, simul::math::Vector3(-1.f, 0.0f, 0.0f));
	simul::math::Quaternion q1 = (const float*)&rot;

	auto q_rel = q1 / q0;
	globalOrientation.SetOrientation(q_rel);
	
	deviceContext.viewStruct.view = globalOrientation.GetInverseMatrix().RowPointer(0);

	//deviceContext.viewStruct.invView = *((const simul::math::Matrix4x4*)&invview);
	//deviceContext.viewStruct.invView.Inverse(deviceContext.viewStruct.view);
	//deviceContext.viewStruct.view.Transpose();
	//simul::crossplatform::MakeViewProjMatrix(deviceContext.viewStruct.viewProj, deviceContext.viewStruct.view, deviceContext.viewStruct.proj);

	deviceContext.viewStruct.Init();

	// Set up where on the render target we want to draw, the view has a 
	XrRect2Di& rect = view.subImage.imageRect;
	crossplatform::Viewport viewport{ (int)rect.offset.x, (int)rect.offset.y, (int)rect.extent.width, (int)rect.extent.height };
	renderPlatform->SetViewports(deviceContext,1,&viewport);

	// Wipe our swapchain color and depth target clean, and then set them up for rendering!
	static float clear[] = { 0.2f, 0.3f, 0.5f, 1 };
	renderPlatform->ActivateRenderTargets(deviceContext,1, &surface.target_view, surface.depth_view);
	renderPlatform->Clear(deviceContext, clear);
	surface.depth_view->ClearDepthStencil(deviceContext, 0.0f, 0);

	// And now that we're set up, pass on the rest of our rendering to the application
	renderDelegate(deviceContext);
	renderPlatform->DeactivateRenderTargets(deviceContext);
}

crossplatform::Texture* UseOpenXR::GetRenderTexture(int index)
{
	if (index < 0 || index >= xr_swapchains.size())
		return nullptr;
	auto sw = xr_swapchains[index];
	if (sw.last_img_id < 0 || sw.last_img_id >= sw.surface_data.size())
		return nullptr;
	return sw.surface_data[sw.last_img_id].target_view;
}

bool UseOpenXR::RenderLayer(simul::crossplatform::GraphicsDeviceContext& deviceContext, XrTime predictedTime
	, vector<XrCompositionLayerProjectionView>& views, XrCompositionLayerProjection& layer
	, simul::crossplatform::RenderDelegate& renderDelegate,vec3 origin)
{
	// Find the state and location of each viewpoint at the predicted time
	uint32_t         view_count = 0;
	XrViewState      view_state = { XR_TYPE_VIEW_STATE };
	XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
	locate_info.viewConfigurationType = app_config_view;
	locate_info.displayTime = predictedTime;
	locate_info.space = xr_app_space;
	xrLocateViews(xr_session, &locate_info, &view_state, (uint32_t)xr_views.size(), &view_count, xr_views.data());
	views.resize(view_count);
	static int64_t frame = 0;
	frame++;
	// And now we'll iterate through each viewpoint, and render it!
	for (uint32_t i = 0; i < view_count; i++)
	{
		// We need to ask which swapchain image to use for rendering! Which one will we get?
		// Who knows! It's up to the runtime to decide.
		uint32_t                    img_id;
		XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		xrAcquireSwapchainImage(xr_swapchains[i].handle, &acquire_info, &img_id);
		xr_swapchains[i].last_img_id = img_id;
		// Wait until the image is available to render to. The compositor could still be
		// reading from it.
		XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wait_info.timeout = XR_INFINITE_DURATION;
		xrWaitSwapchainImage(xr_swapchains[i].handle, &wait_info);

		// Set up our rendering information for the viewpoint we're using right now!
		views[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		views[i].pose = xr_views[i].pose;
		views[i].fov = xr_views[i].fov;
		views[i].subImage.swapchain = xr_swapchains[i].handle;
		views[i].subImage.imageRect.offset = { 0, 0 };
		views[i].subImage.imageRect.extent = { xr_swapchains[i].width, xr_swapchains[i].height };

		deviceContext.setDefaultRenderTargets(nullptr,nullptr, 0, 0, xr_swapchains[i].width, xr_swapchains[i].height
			,&xr_swapchains[i].surface_data[img_id].target_view,1, xr_swapchains[i].surface_data[img_id].depth_view);
		deviceContext.frame_number = frame;
		deviceContext.renderPlatform = renderPlatform;

		deviceContext.viewStruct.view_id = i;
		deviceContext.viewStruct.depthTextureStyle = crossplatform::PROJECTION;
		// Call the rendering callback with our view and swapchain info
		RenderLayer(deviceContext,views[i], xr_swapchains[i].surface_data[img_id],renderDelegate,origin);

		// And tell OpenXR we're done with rendering to this one!
		XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		xrReleaseSwapchainImage(xr_swapchains[i].handle, &release_info);
	}

	layer.space = xr_app_space;
	layer.viewCount = (uint32_t)views.size();
	layer.views = views.data();
	return true;
}

void UseOpenXR::PollEvents(bool& exit)
{
	exit = false;

	XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };

	while (xrPollEvent(xr_instance, &event_buffer) == XR_SUCCESS)
	{
		switch (event_buffer.type)
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			XrEventDataSessionStateChanged* changed = (XrEventDataSessionStateChanged*)&event_buffer;
			xr_session_state = changed->state;

			// Session state change is where we can begin and end sessions, as well as find quit messages!
			switch (xr_session_state)
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
				begin_info.primaryViewConfigurationType = app_config_view;
				xrBeginSession(xr_session, &begin_info);
				xr_running = true;
			}
			break;
			case XR_SESSION_STATE_STOPPING:
			{
				xr_running = false;
				xrEndSession(xr_session);
			}
			break;
			case XR_SESSION_STATE_EXITING:
				exit = true;
				break;
			case XR_SESSION_STATE_LOSS_PENDING:
				exit = true; 
				break;
			}
		} break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: exit = true; return;
		}
		event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
	}
}

bool UseOpenXR::HaveXRDevice() const
{
	return UseOpenXR::haveXRDevice;
}

const avs::Pose& UseOpenXR::GetHeadPose() const
{
	return headPose;
}

const avs::Pose& UseOpenXR::GetControllerPose(int index) const
{
	if (index >= 0 && controllerPoses.size())
		return controllerPoses[index];
	else
		return avs::Pose();
}

const teleport::client::ControllerState& UseOpenXR::GetControllerState(int index) const
{
	static teleport::client::ControllerState emptyState;
	if (index >= 0 && controllerStates.size())
		return controllerStates[index];
	else
		return emptyState;
}

void UseOpenXR::RenderFrame(simul::crossplatform::GraphicsDeviceContext	&deviceContext,simul::crossplatform::RenderDelegate &renderDelegate,vec3 origin)
{
	// Block until the previous frame is finished displaying, and is ready for another one.
	// Also returns a prediction of when the next frame will be displayed, for use with predicting
	// locations of controllers, viewpoints, etc.
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	xrWaitFrame(xr_session, nullptr, &frame_state);
	// Must be called before any rendering is done! This can return some interesting flags, like 
	// XR_SESSION_VISIBILITY_UNAVAILABLE, which means we could skip rendering this frame and call
	// xrEndFrame right away.
	xrBeginFrame(xr_session, nullptr);

	// Execute any code that's dependant on the predicted time, such as updating the location of
	// controller models.
	openxr_poll_predicted(frame_state.predictedDisplayTime);
	app_update_predicted();

	XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
	XrResult        res = xrLocateSpace(xr_head_space, xr_app_space, frame_state.predictedDisplayTime, &space_location);
	if (XR_UNQUALIFIED_SUCCESS(res) &&
		(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
		(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
	{
		headPose.position = crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3*)&space_location.pose.position));
 		headPose.orientation = crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec4*)&space_location.pose.orientation));
	}
	// If the session is active, lets render our layer in the compositor!
	XrCompositionLayerBaseHeader* layer = nullptr;
	XrCompositionLayerProjection             layer_proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	vector<XrCompositionLayerProjectionView> views;
	bool session_active = xr_session_state == XR_SESSION_STATE_VISIBLE || xr_session_state == XR_SESSION_STATE_FOCUSED;
	if (session_active && RenderLayer(deviceContext,frame_state.predictedDisplayTime, views, layer_proj,renderDelegate, origin))
	{
		layer = (XrCompositionLayerBaseHeader*)&layer_proj;
	}

	// We're finished with rendering our layer, so send it off for display!
	XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
	end_info.displayTime = frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = xr_blend;
	end_info.layerCount = layer == nullptr ? 0 : 1;
	end_info.layers = &layer;
	xrEndFrame(xr_session, &end_info);
}

void UseOpenXR::Shutdown()
{
	haveXRDevice = false;
	// We used a graphics API to initialize the swapchain data, so we'll
	// give it a chance to release anythig here!
	for (int32_t i = 0; i < xr_swapchains.size(); i++)
	{
		xrDestroySwapchain(xr_swapchains[i].handle);
		swapchain_destroy(xr_swapchains[i]);
	}
	xr_swapchains.clear();

	// Release all the other OpenXR resources that we've created!
	// What gets allocated, must get deallocated!
	if (xr_input.actionSet != XR_NULL_HANDLE)
	{
		if (xr_input.handSpace[0] != XR_NULL_HANDLE)
			xrDestroySpace(xr_input.handSpace[0]);
		if (xr_input.handSpace[1] != XR_NULL_HANDLE)
			xrDestroySpace(xr_input.handSpace[1]);
		xrDestroyActionSet(xr_input.actionSet);
	}
	if (xr_app_space != XR_NULL_HANDLE)
		xrDestroySpace(xr_app_space);
	if (xr_session != XR_NULL_HANDLE)
		xrDestroySession(xr_session);
	if (xr_debug != XR_NULL_HANDLE)
		ext_xrDestroyDebugUtilsMessengerEXT(xr_debug);
	if (xr_instance != XR_NULL_HANDLE)
		xrDestroyInstance(xr_instance);
}
