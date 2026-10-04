/**
 * @file system_motion.cpp
 * @brief Whether the reader has asked Windows to stop animating things.
 */

#include "app/system_motion.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace lens::app {

SystemMotion::SystemMotion()
{
    // pvParam takes the animation state, not the return value: the call's own BOOL says only
    // whether the read worked. See the header for the interface this is.
    BOOL animationsEnabled = FALSE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0))
        reduced_ = animationsEnabled == FALSE;
}

bool SystemMotion::reduced() const
{
    return reduced_;
}

}
