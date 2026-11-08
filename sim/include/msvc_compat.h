#pragma once

// LovyanGFX contains GCC-only attributes in headers that MSVC still parses
// (including drivers that are unused by the SDL backend). Strip the attribute
// syntax; layout-sensitive attributes are irrelevant for this preview.
#ifndef __attribute__
#define __attribute__(x)
#endif
