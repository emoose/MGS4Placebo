Since everyone and their mom is releasing MGS4 patches, here's another set of them.

Allows game to make use of MSAA on geometry & transparent textures, along with a possibly-placebo rawinput mouse hook, plus shadow resolution / game resolution overrides.

Not that likely to be updated, mainly releasing in hope other mods might adopt things here.

Test builds can be found at https://github.com/emoose/MGS4Placebo/releases - let me know if you notice any issues with it!

# Patches

### hooks_msaa.cpp

Enables MSAA on any full-resolution render targets, also includes shader-patching code to enable multisampling on cutout textures (fences etc)

Since MGS4 is forward-rendered MSAA can be applied onto it (when Fox Engine was announced they made a big deal about switching over to deferred rendering)

For this we patch full-res RTs to use bgfx's MSAA flags, and also set BGFX_STATE_MSAA so D3D would make use of it.

However bgfx also refuses to attach a readable multisampled depth buffer, giving an error and bailing instead - but since shaders only read depth through R24_UNORM_X8_TYPELESS it /should/ be fine to use. (leaving depth write-only just broke anything that tried reading depth, eg. DoF would just blur the whole screen)

To get around it we patch out two checks in bgfx, one that won't attach multisampled depth to a framebuffer unless it's marked write-only, and one that gives up creating the texture at all when bgfx checks whether D24S8 supports MSAA sampling. (seems bgfx checks D24_UNORM_S8_UINT instead, which can never back an SRV anyway, so it would always say no)

Shaders reading depth still declare a plain Texture2D though, which can't address a multisampled surface, so the values come back scrambled.

D3D11 hands back something usable regardless, D3D12 just shows bands over anything depth-based (smoke, DoF, fog).

Only one shader actually reads the depth buffer, the one linearizing it into the full-size R32_FLOAT that everything else samples, so a single replacement fixes all of it.

---

With MSAA alone it made some difference to the scene, geometry edges have far less aliasing artifacts with it, but really geometry aliasing wasn't the main issue, most of the aliasing is from transparent textures like fences that MSAA couldn't affect.

Tried a few attempts at enabling ATOC on them, but doesn't seem it's viable, the games HDR lighting seems to overwrite most of the data ATOC would use.

Instead tried patching the shaders that draw the fences & other transparent cut-outs, switching their inputs to use per-sample interpolation. That makes D3D run the shader once per MSAA sample instead of once per pixel, so the alpha test happens per sample and the edges get antialiased the same as geometry (it's only a 4-bit field in an existing declaration so just a single byte patch per shader, along with a checksum fixup)

MSAA probably isn't that useful for those on PCs that can just run the game supersampled, but could help portables and less-powerful computers.

**This has only been lightly tested in some of the first areas of the game**, already had to solve one issue with DoF, so wouldn't be surprised if later parts have their own issues too.

### hooks_shaders.cpp

Dumps every shader the renderer makes, and can swap them out from a folder next to the dll. Hooks Context::createShader instead of the games own loader so it catches bgfx's built-in shaders too, not just the ones out of the .vfp files.

The MSAA cutout patching and linearizing-shader replacement mentioned above happens here as well.

### hooks_mouse.cpp

Patches game to let GLFW make use of raw-input, and some minor patches to game-code that should allow smaller mouse movements to be registered.

The raw-input code is all there in GLFW already, the game just never turns it on, so mouse input comes in through WM_MOUSEMOVE with whatever pointer accel Windows applied to it.

At first vanilla game felt a little bit like mouse was emulating an analog stick to me, and in the code it kind of is, but camera code does seem to grab the mouse delta directly at least.

The small-movement thing is real though, the camera scales the delta by 2/3 and then truncates to an int, so a frame that only moved a pixel may get thrown away entirely. Now it keeps the remainder for the next frame.

Really I'm not sure how much of a difference these hooks make though, likely needs more testing.

### hooks_resolution.cpp

Some hooks to allow custom resolutions, mainly to allow DSR to work, might work for ultrawide resolutions too but those aren't tested.

Removes the games 16:9 and 5760x2160 resolution clamps, allowing the render size to be set directly.

Crosshair reticle fixed thanks to patch from https://github.com/drbermejor/mgs4Ultra120, which I found after writing these and running into the same issue.

### hooks_shadows.cpp

Allows overriding shadow map size and sample count. The game parses those out of Shadow entries in its .ecf config, so the hook just rewrites the values after they're parsed.

# Code

Hooks are all self-contained units, creating a new hook just involves adding a new class deriving from `Hook`, no need to edit 3 different files to validate it / apply it / log it from separate areas, that can all be handled in the single class instead.

INI settings are similarly self-contained, the top of each hook.cpp file declares the INI settings it wants to read, the declaration will handle registering the settings so it's read from INI with its value logged.

(this style might seem odd but has been used in [OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks) and a couple other projects pretty well, made juggling 70+ separate hooks/modifications inside a single project a lot easier)

spdlog used for logging, safetyhook used for hooking, ModUtils used for memory scanning and patching, ini-cpp for INI reading/writing.
