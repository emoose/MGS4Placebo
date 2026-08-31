#include "hook_mgr.hpp"
#include "plugin.hpp"

#include <cmath>

namespace Settings
{
	Setting<bool> MouseFixEnable("MouseFix", "Enable", true);

	Setting<bool> MouseRawInput("MouseFix", "RawInput", true,
		"Reads mouse movement straight from the device instead of from cursor "
		"positions, removing Windows pointer acceleration and the desktop "
		"pointer-speed setting from camera control.");

	Setting<float> MouseSensitivityX("MouseFix", "CameraSensitivityX", 1.0f,
		"Horizontal camera speed, as a multiplier over the stock speed.",
		Settings::Range<float>{ 0.05f, 10.0f });

	Setting<float> MouseSensitivityY("MouseFix", "CameraSensitivityY", 1.0f,
		"Vertical camera speed, as a multiplier over the stock speed.",
		Settings::Range<float>{ 0.05f, 10.0f });
};

// The game drives the camera from a virtual DualShock pad built out of the
// keyboard/mouse bindings in config\mgs4.input.ini, but the camera controller
// itself skips that pad and reads mouse movement directly whenever the active
// input device is keyboard/mouse. These hooks work on that direct path.
namespace
{
	// glfwSetInputMode, already bound to the game window.
	using glfwSetInputMode_fn = void(__fastcall*)(uint32_t mode, uint32_t value);

	constexpr uint32_t GLFW_RAW_MOUSE_MOTION = 0x00033005;
	constexpr uint32_t GLFW_TRUE = 1;

	// Fixed multiplier the camera applies to the pixel delta. Kept so that a
	// sensitivity of 1.0 leaves the camera feeling as it shipped.
	constexpr float kStockMouseScale = 2.0f / 3.0f;
}

class MouseRawInputHook : public Hook
{
	inline static SafetyHookMid MouseInit_hook = {};

	// Runs inside the mouse subsystem init, just after it puts the cursor into
	// GLFW_CURSOR_DISABLED. GLFW only registers a raw input device while the
	// cursor is disabled, so the order matters.
	static void MouseInit_dest(safetyhook::Context&)
	{
		auto setInputMode = Module::fn_ptr<glfwSetInputMode_fn>(0x660F70);
		setInputMode(GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}

public:
	std::string_view description() override
	{
		return "MouseRawInputHook";
	}

	bool validate() override
	{
		return Settings::MouseFixEnable && Settings::MouseRawInput;
	}

	bool apply() override
	{
		// 0x74F757, the instruction after the glfwSetInputMode(GLFW_CURSOR, ...)
		// call in the mouse subsystem init.
		//   xorps xmm0, xmm0
		//   lea   rdx, [rsp+38h]
		if (!Module::code_matches(0x74F757, { 0x0F, 0x57, 0xC0, 0x48, 0x8D, 0x54, 0x24, 0x38 }))
			return false;

		MouseInit_hook = safetyhook::create_mid(
			Module::exe_ptr(0x74F757), MouseInit_dest);

		return bool(MouseInit_hook);
	}

	static MouseRawInputHook instance;
};
MouseRawInputHook MouseRawInputHook::instance;

class MouseCameraHook : public Hook
{
	inline static SafetyHookMid CameraScale_hooks[2] = {};

	// Fraction of a step left over from the previous frame, one pair per camera
	// object. Without it the camera converts the scaled delta to an integer by
	// truncating, so any frame that moves the mouse less than one whole step
	// contributes nothing at all: at high framerates or low sensitivity that
	// silently swallows most slow aiming movement. Carrying the remainder makes
	// the camera cover the exact distance the mouse travelled instead.
	inline static uintptr_t ResidualOwner = 0;
	inline static float ResidualX = 0.0f;
	inline static float ResidualY = 0.0f;

	// Both call sites read the raw pixel delta from the same two stack slots,
	// scale it, and store the result from eax/ecx into those slots. The hook
	// sits on the first store, where the slots still hold the raw delta, and
	// substitutes its own result before the store takes it.
	static void CameraScale_dest(safetyhook::Context& ctx)
	{
		const int32_t rawX = *reinterpret_cast<int32_t*>(ctx.rbp + 0xB0);
		const int32_t rawY = *reinterpret_cast<int32_t*>(ctx.rbp + 0xB8);

		// rbx holds the camera object. A different camera starts from zero
		// rather than inheriting a remainder built up by the last one.
		if (ctx.rbx != ResidualOwner)
		{
			ResidualOwner = ctx.rbx;
			ResidualX = 0.0f;
			ResidualY = 0.0f;
		}

		const float scaledX = float(rawX) * kStockMouseScale * Settings::MouseSensitivityX + ResidualX;
		const float scaledY = float(rawY) * kStockMouseScale * Settings::MouseSensitivityY + ResidualY;

		const float stepX = std::floor(scaledX);
		const float stepY = std::floor(scaledY);

		ResidualX = scaledX - stepX;
		ResidualY = scaledY - stepY;

		ctx.rax = uint32_t(int32_t(stepX));
		ctx.rcx = uint32_t(int32_t(stepY));
	}

public:
	std::string_view description() override
	{
		return "MouseCameraHook";
	}

	bool validate() override
	{
		return Settings::MouseFixEnable;
	}

	bool apply() override
	{
		// The two mouse branches of the camera controller update, one per camera
		// mode. Both are the "mov [rbp+0B0h], eax" that stores the scaled X.
		constexpr uintptr_t sites[2] = { 0x6533E0, 0x65356C };

		for (size_t i = 0; i < 2; i++)
		{
			//   mov [rbp+0B0h], eax
			//   mov [rbp+0B8h], ecx
			if (!Module::code_matches(sites[i], { 0x89, 0x85, 0xB0, 0x00, 0x00, 0x00,
			                             0x89, 0x8D, 0xB8, 0x00, 0x00, 0x00 }))
				return false;

			CameraScale_hooks[i] = safetyhook::create_mid(
				Module::exe_ptr(sites[i]), CameraScale_dest);

			if (!CameraScale_hooks[i])
				return false;
		}

		return true;
	}

	static MouseCameraHook instance;
};
MouseCameraHook MouseCameraHook::instance;

