#pragma once

namespace PlayerPatches
{
    // Called from the application tick so player-only cheats can keep their
    // state in sync even when the game writes it directly.
    void Update();
}
