Since everyone else and their mom is releasing MGS4 patches, here's another set of them.

Allows game to make use of MSAA on geometry & transparent textures, along with a possibly-placebo rawinput mouse hook.

Not likely to be updated, mainly releasing in hope other mods might adopt things here.

# Patches

### hooks_msaa.cpp

Enables MSAA on any full-resolution render targets, also includes shader-patching code to enable multisampling on cutout textures (fences etc)

MGS4 is forward-rendered, so MSAA can apply to it (when Fox Engine was announced they made a big deal about that switching to deferred rendering)

Two things had to be patched before it did anything. The game never sets BGFX_STATE_MSAA so D3D was still rasterizing one sample per pixel, you'd get multisampled targets resolving from identical samples.

bgfx also refuses to make a readable multisampled depth buffer, partly because D3D can't resolve depth formats, partly because its format probe asks about D24S8 when shaders actually read depth through an R24_UNORM_X8_TYPELESS view. Leaving depth write-only instead breaks anything that reads depth, so DoF ended up blurring the whole screen.

MSAA alone seemed to make some difference, geometry edges have far less aliasing artifacts with it, but really geometry aliasing wasn't the main issue in the game, most of the aliasing is from transparent textures like fences, that MSAA couldn't affect.

Tried a few attempts at enabling ATOC on them, but doesn't seem it's viable, the games HDR lighting seems to overwrite most of the data ATOC would use.

Instead I tried patching the shaders that draw the fences, switching their inputs to per-sample interpolation. That makes D3D run the shader once per MSAA sample instead of once per pixel, so the alpha test happens per sample and the cutout edges get antialiased the same as geometry. It's only a 4-bit field in an existing declaration so the shader stays the same length, only the container hash needs redoing.

### hooks_shaders.cpp

Dumps every shader the renderer makes, and can swap them out from a folder next to the dll. Hooks Context::createShader instead of the games own loader so it catches bgfx's built-in shaders too, not just the ones out of the .vfp files.

The cutout patching above happens here as well.

### hooks_mouse.cpp

Patches game to allow GLFW to make use of raw-input, and some minor patches to game-code that should allow smaller mouse movements to be registered.

The raw-input code is all there in GLFW already, the game just never turns it on, so mouse input comes in through WM_MOUSEMOVE with whatever pointer accel Windows applied to it.

The vanilla game felt a bit like mouse was emulating a control stick to me, and in the code it kind of is, but camera code does seem to grab the mouse delta directly at least.

The small-movement thing is real though, the camera scales the delta by 2/3 and then truncates to an int, so a frame that only moved a pixel may get thrown away entirely. Now it keeps the remainder for the next frame.

Really I'm not sure how much of a difference these hooks make though, needs more testing.

### hooks_resolution.cpp

Some hooks to allow custom resolutions, mainly to allow DSR to work, might work for ultrawide resolutions too but those aren't tested.

Removes the games 16:9 and 5760x2160 resolution clamps, allowing the render size to be set directly.

Crosshair reticle fixed thanks to code from https://github.com/drbermejor/mgs4Ultra120, which I found after writing these and running into the same issue.

### hooks_shadows.cpp

Allows overriding shadow map size and sample count. The game parses those out of Shadow...@<index>.<field> entries in its config, so the hook just rewrites the values after they're parsed.

# Code

Hooks are all self-contained units, creating a new hook just involves adding a new class deriving from `Hook`, no need to edit 3 different files to validate it / apply it / log it from separate areas, that can all be handled in the single class instead.

INI settings are similarly self-contained, the top of each hook.cpp file declares the INI settings it wants to read, the declaration will handle registering the settings so it's read from INI with its value logged.

spdlog used for logging, safetyhook used for hooking, ModUtils used for memory scanning and patching.
