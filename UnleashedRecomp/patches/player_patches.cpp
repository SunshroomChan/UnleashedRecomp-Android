#include <api/SWA.h>
#include <ui/game_window.h>
#include <user/config.h>
#include <os/logger.h>
#include <app.h>
#include <sdl_events.h>

#include <algorithm>
#include <array>

static uint32_t g_lastEnemyScore;
static uint32_t g_lastTrickScore;
static float g_lastDarkGaiaEnergy;
static bool g_isUnleashCancelled;
static std::array<uint32_t, 2> g_ringPlayerContexts{};
static std::array<uint32_t, 2> g_evilSonicContexts{};

static bool IsInfiniteRingsEnabled()
{
    return Config::InfiniteRings || Config::InfiniteRingEnergy;
}

static void TrackRingPlayerContext(uint32_t context)
{
    if (!context)
        return;

    for (auto known : g_ringPlayerContexts)
    {
        if (known == context)
            return;
    }

    for (auto& known : g_ringPlayerContexts)
    {
        if (!known)
        {
            known = context;
            return;
        }
    }

    // A stage can destroy and recreate a player object. Replacing the oldest
    // slot keeps the list bounded without retaining an unbounded set of guest
    // pointers across level transitions.
    g_ringPlayerContexts[0] = g_ringPlayerContexts[1];
    g_ringPlayerContexts[1] = context;
}

static void TrackEvilSonicContext(uint32_t context)
{
    if (!context)
        return;

    for (auto known : g_evilSonicContexts)
    {
        if (known == context)
            return;
    }

    for (auto& known : g_evilSonicContexts)
    {
        if (!known)
        {
            known = context;
            return;
        }
    }

    g_evilSonicContexts[0] = g_evilSonicContexts[1];
    g_evilSonicContexts[1] = context;
}

static void ForgetEvilSonicContext(uint32_t context)
{
    for (auto& known : g_evilSonicContexts)
    {
        if (known == context)
            known = 0;
    }
}

/* Hook function for when checkpoints are activated
   to preserve the current checkpoint score. */
PPC_FUNC_IMPL(__imp__sub_82624308);
PPC_FUNC(sub_82624308)
{
    __imp__sub_82624308(ctx, base);

    if (!Config::SaveScoreAtCheckpoints)
        return;

    if (auto pGameDocument = SWA::CGameDocument::GetInstance())
    {
        g_lastEnemyScore = pGameDocument->m_pMember->m_ScoreInfo.EnemyScore;
        g_lastTrickScore = pGameDocument->m_pMember->m_ScoreInfo.TrickScore;

        LOGFN("Score: {}", g_lastEnemyScore + g_lastTrickScore);
    }
}

/* Hook function that resets the score
   and restore the last checkpoint score. */
PPC_FUNC_IMPL(__imp__sub_8245F048);
PPC_FUNC(sub_8245F048)
{
    __imp__sub_8245F048(ctx, base);

    if (!Config::SaveScoreAtCheckpoints)
        return;

    if (auto pGameDocument = SWA::CGameDocument::GetInstance())
    {
        LOGFN("Score: {}", g_lastEnemyScore + g_lastTrickScore);

        pGameDocument->m_pMember->m_ScoreInfo.EnemyScore = g_lastEnemyScore;
        pGameDocument->m_pMember->m_ScoreInfo.TrickScore = g_lastTrickScore;
    }
}

void ResetScoreOnRestartMidAsmHook()
{
    g_lastEnemyScore = 0;
    g_lastTrickScore = 0;
}

/* Ring expenditure hook.
   The game routes every ring loss (damage, boost costs and scripted drains)
   through this routine. Passing zero to the original keeps the normal ring
   update and HUD notification path intact while preventing the count from
   decreasing when the code is enabled. */
PPC_FUNC_IMPL(__imp__sub_82318AA0);
PPC_FUNC(sub_82318AA0)
{
    TrackRingPlayerContext(ctx.r3.u32);

    if (IsInfiniteRingsEnabled())
        ctx.r4.u32 = 0;

    __imp__sub_82318AA0(ctx, base);
}

/* Damage/drop path. Unlike the small expenditure helper above, this routine
   clamps and subtracts the number of rings dropped after taking damage. It is
   used by both the daytime Sonic and nighttime Werehog player contexts. */
PPC_FUNC_IMPL(__imp__sub_8231FAE8);
PPC_FUNC(sub_8231FAE8)
{
    TrackRingPlayerContext(ctx.r3.u32);

    if (IsInfiniteRingsEnabled())
        ctx.r4.u32 = 0;

    __imp__sub_8231FAE8(ctx, base);
}

/* Ring setter guard. A few scripted damage/reset paths call the small setter
   directly instead of going through one of the subtraction helpers above.
   Keep legitimate gains and initialisation intact, but never accept a lower
   value while Infinite Rings is enabled. This is shared by the daytime Sonic
   and nighttime Werehog player contexts. */
PPC_FUNC_IMPL(__imp__sub_82316B68);
PPC_FUNC(sub_82316B68)
{
    TrackRingPlayerContext(ctx.r3.u32);

    if (IsInfiniteRingsEnabled())
    {
        const auto currentRings = PPC_LOAD_U32(ctx.r3.u32 + 1336);
        if (ctx.r4.u32 < currentRings)
            ctx.r4.u32 = currentRings;
    }

    __imp__sub_82316B68(ctx, base);
}

// Player ring initialisation. This runs when either daytime Sonic or the
// Werehog context is created, before the first ring is collected or lost, so
// the per-tick cheat pass can cover both forms from their first frame.
PPC_FUNC_IMPL(__imp__sub_8244EEF8);
PPC_FUNC(sub_8244EEF8)
{
    TrackRingPlayerContext(ctx.r3.u32);
    __imp__sub_8244EEF8(ctx, base);
}

namespace PlayerPatches
{
    void Update()
    {
        if (!IsInfiniteRingsEnabled() && !Config::InfiniteJump && !Config::InfiniteUnleash)
            return;

        // Keep the ring counter at a high value after the original game tick.
        // This covers direct stores used by scripted damage and checkpoint
        // code, which do not call either subtraction helper.
        if (IsInfiniteRingsEnabled())
        {
            for (auto context : g_ringPlayerContexts)
            {
                if (context)
                    PPC_STORE_U32(context + 1336, 999);
            }
        }

        // Keep the Werehog's Dark Gaia/Unleash gauge full from the first
        // frame and after every game tick.  These pointers are tracked only
        // for CEvilSonicContext instances, so daytime Sonic is unaffected.
        if (Config::InfiniteUnleash)
        {
            for (auto context : g_evilSonicContexts)
            {
                if (context)
                {
                    auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(context);
                    if (pEvilSonicContext)
                        pEvilSonicContext->m_DarkGaiaEnergy = 100.0f;
                }
            }
        }

        if (!Config::InfiniteJump)
            return;

        // The jump state consumes a tapped A input. Re-arm that edge while the
        // player is holding A so both the Sonic and Werehog state machines can
        // request another jump. We only do this after a player context has
        // been observed, preventing title/menu navigation from auto-repeating.
        if (!g_ringPlayerContexts[0] && !g_ringPlayerContexts[1])
            return;

        auto input = SWA::CInputState::GetInstance();
        if (!input)
            return;

        auto& pad = const_cast<SWA::SPadState&>(input->GetPadState());
        const uint32_t down = pad.DownState;
        if (down & SWA::eKeyState_A)
            pad.TappedState = static_cast<uint32_t>(pad.TappedState) | SWA::eKeyState_A;
    }
}

// Dark Gaia energy change hook.
PPC_FUNC_IMPL(__imp__sub_823AF7A8);
PPC_FUNC(sub_823AF7A8)
{
    auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(ctx.r3.u32);

    if (!pEvilSonicContext)
    {
        __imp__sub_823AF7A8(ctx, base);
        return;
    }

    TrackEvilSonicContext(ctx.r3.u32);

    g_lastDarkGaiaEnergy = pEvilSonicContext->m_DarkGaiaEnergy;

    // A negative delta is energy drain.  Ignore it while Infinite Unleash is
    // active; the update pass below also restores the full gauge after any
    // direct stores performed by scripts or the HUD.
    if (Config::InfiniteUnleash && ctx.f1.f64 < 0.0)
        ctx.f1.f64 = 0.0;

    // Don't drain energy if out of control.
    if (Config::FixUnleashOutOfControlDrain && pEvilSonicContext->m_OutOfControlCount && ctx.f1.f64 < 0.0)
        return;

    __imp__sub_823AF7A8(ctx, base);

    if (Config::InfiniteUnleash)
    {
        // Restore immediately as well as in PlayerPatches::Update so code
        // that reads the gauge later in this same tick sees it as full.
        pEvilSonicContext->m_DarkGaiaEnergy = 100.0f;
        return;
    }

    if (!Config::AllowCancellingUnleash || Config::InfiniteUnleash)
        return;

    auto pInputState = SWA::CInputState::GetInstance();

    // Don't allow cancelling Unleash if the intro anim is still playing.
    if (!pInputState || pEvilSonicContext->m_AnimationID == 39)
        return;

    if (pInputState->GetPadState().IsTapped(SWA::eKeyState_RightBumper))
    {
        pEvilSonicContext->m_DarkGaiaEnergy = 0.0f;
        g_isUnleashCancelled = true;
    }
}

void PostUnleashMidAsmHook(PPCRegister& r30)
{
    if (!g_isUnleashCancelled)
        return;

    if (auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(r30.u32))
        pEvilSonicContext->m_DarkGaiaEnergy = std::max(0.0f, g_lastDarkGaiaEnergy - 35.0f);

    g_isUnleashCancelled = false;
}

// SWA::Player::CEvilSonicContext
PPC_FUNC_IMPL(__imp__sub_823B49D8);
PPC_FUNC(sub_823B49D8)
{
    __imp__sub_823B49D8(ctx, base);

    // Capture the Werehog context even before its first ring event so the
    // jump helper is available immediately after entering a night stage.
    TrackRingPlayerContext(ctx.r3.u32);
    TrackEvilSonicContext(ctx.r3.u32);
    if (Config::InfiniteUnleash)
    {
        if (auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(ctx.r3.u32))
            pEvilSonicContext->m_DarkGaiaEnergy = 100.0f;
    }
    App::s_isWerehog = true;

    SDL_User_EvilSonic(true);
}

// ~SWA::Player::CEvilSonicContext
PPC_FUNC_IMPL(__imp__sub_823B4590);
PPC_FUNC(sub_823B4590)
{
    __imp__sub_823B4590(ctx, base);

    ForgetEvilSonicContext(ctx.r3.u32);

    App::s_isWerehog = false;

    SDL_User_EvilSonic(false);
}
