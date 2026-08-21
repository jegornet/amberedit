#include "app/navigator.hpp"

// Navigator is entirely inline in the header: the class is small. This
// translation unit exists so that it stays in amberedit_core's source list and
// keeps compiling even with no consumers.

namespace amberedit::app {
namespace {

// Compile-time check that the stack starts at the area list.
static_assert(static_cast<int>(ScreenId::AreaList) == 0,
              "AreaList must remain the root screen");

}  // namespace
}  // namespace amberedit::app
