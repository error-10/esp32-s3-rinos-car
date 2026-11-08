// Minimal LovyanGFX v1 single translation unit for the SDL2 preview.
// The upstream lgfx_v1.cpp includes every panel/touch driver, which MSVC cannot
// parse (GCC __attribute__ extensions and non-constant array bounds in unused
// drivers). This file includes only the core, framebuffer base and SDL backend.

#define LGFX_V1_IMPLEMENTATION

#include "lgfx/v1/LGFXBase.inl"
#include "lgfx/v1/LGFX_Sprite.inl"
#include "lgfx/v1/LGFX_Button.inl"
#include "lgfx/v1/lgfx_fonts.inl"

#include "lgfx/v1/misc/DividedFrameBuffer.inl"
#include "lgfx/v1/misc/SpriteBuffer.inl"
#include "lgfx/v1/misc/common_function.inl"
#include "lgfx/v1/misc/pixelcopy.inl"

#include "lgfx/v1/panel/Panel_Device.inl"
#include "lgfx/v1/panel/Panel_FrameBufferBase.inl"

#include "lgfx/v1/platforms/sdl/Panel_sdl.inl"
#include "lgfx/v1/platforms/sdl/common.inl"

#undef LGFX_V1_IMPLEMENTATION
