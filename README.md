Since everyone else and their mom is releasing MGS4 patches, here's another set of them.

Allows game to make use of MSAA on geometry & transparent textures, along with a possibly-placebo rawinput mouse hook.

Not likely to be updated, mainly releasing in hope other mods might adopt things here.

# Patches

### hooks_msaa.cpp

Enables MSAA on any full-resolution render targets, also includes shader-patching code to enable multisampling on cutout textures (fences etc)

MGS4 is forward-rendered, so MSAA can be applied to it. (when Fox Engine was announced they made a big deal about switching over to deferred rendering)

For this we just switch RTs to use bgfx's MSAA flags, and also set BGFX_STATE_MSAA so D3D would make use of it.

However bgfx also refuses to make a readable multisampled depth buffer, giving an error and bailing instead of creating it - but since shaders only read depth through R24_UNORM_X8_TYPELESS, it should be fine for a multisampled depth buffer to be used - we patch out the two checks that block it, one that won't attach multisampled depth to a framebuffer unless it's marked write-only, and one that gives up creating the texture at all after asking the driver whether D24S8 supports MSAA sampling. (leaving depth write-only without changing this would break anything that reads depth, so eg. DoF would just blur the whole screen)

MSAA alone seemed to make some difference, geometry edges have far less aliasing artifacts with it, but really geometry aliasing wasn't the main issue in the game, most of the aliasing is from transparent textures like fences that MSAA couldn't affect.

Tried a few attempts at enabling ATOC on them, but doesn't seem it's viable, the games HDR lighting seems to overwrite most of the data ATOC would use.

Instead tried patching the shaders that draw the fences & other transparent cut-outs, switching their inputs to per-sample interpolation. That makes D3D run the shader once per MSAA sample instead of once per pixel, so the alpha test happens per sample and the edges get antialiased the same as geometry. It's only a 4-bit field in an existing declaration so the shader stays the same length, only the container hash needs redoing.

MSAA likely isn't too useful for those on PCs that can just run the game supersampled, but might be a help on portables and less-powerful computers.

**This has only been lightly tested in some of the first areas of the game**, already had to solve one issue with DoF, so wouldn't be surprised if later parts have their own issues too.

### hooks_shaders.cpp

Dumps every shader the renderer makes, and can swap them out from a folder next to the dll. Hooks Context::createShader instead of the games own loader so it catches bgfx's built-in shaders too, not just the ones out of the .vfp files.

The cutout patching above happens here as well.

### hooks_mouse.cpp

Patches game to let GLFW make use of raw-input, and some minor patches to game-code that should allow smaller mouse movements to be registered.

The raw-input code is all there in GLFW already, the game just never turns it on, so mouse input comes in through WM_MOUSEMOVE with whatever pointer accel Windows applied to it.

The vanilla game felt a bit like mouse was emulating a control stick to me, and in the code it kind of is, but camera code does seem to grab the mouse delta directly at least.

The small-movement thing is real though, the camera scales the delta by 2/3 and then truncates to an int, so a frame that only moved a pixel may get thrown away entirely. Now it keeps the remainder for the next frame.

Really I'm not sure how much of a difference these hooks make though, needs more testing.

### hooks_resolution.cpp

Some hooks to allow custom resolutions, mainly to allow DSR to work, might work for ultrawide resolutions too but those aren't tested.

Removes the games 16:9 and 5760x2160 resolution clamps, allowing the render size to be set directly.

Crosshair reticle fixed thanks to patch from https://github.com/drbermejor/mgs4Ultra120, which I found after writing these and running into the same issue.

### hooks_shadows.cpp

Allows overriding shadow map size and sample count. The game parses those out of Shadow...@<index>.<field> entries in its config, so the hook just rewrites the values after they're parsed.

# Code

Hooks are all self-contained units, creating a new hook just involves adding a new class deriving from `Hook`, no need to edit 3 different files to validate it / apply it / log it from separate areas, that can all be handled in the single class instead.

INI settings are similarly self-contained, the top of each hook.cpp file declares the INI settings it wants to read, the declaration will handle registering the settings so it's read from INI with its value logged.

spdlog used for logging, safetyhook used for hooking, ModUtils used for memory scanning and patching, ini-cpp for INI reading/writing.
