// Local tooling compatibility shim. Force-included only for the g++ syntax
// check. libstdc++ (unlike clang/MSVC STL) does not expose the unqualified
// global `nullptr_t` that the upstream Windhawk headers use; this adds it.
#pragma once

#include <cstddef>
using std::nullptr_t;
