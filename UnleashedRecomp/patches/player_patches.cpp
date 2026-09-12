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
static std::array<uint32_t, 2> g_evilSonicContexts{};

static constexpr uint32_t PLAYER_RING_COUNT_OFFSET = 1336;
static constexpr uint32_t PLAYER_RING_ENERGY_OFFSET = 1340;
static constexpr uint32_t INFINITE_RING_COUNT = 999;

PPC_FUNC_IMPL(__imp__sub_82318DF0);

static float GetMaximumRingEnergy(PPCContext& ctx, uint8_t* base, uint32_t playerContext)
{
    // The maximum boost/Ring Energy value depends on Sonic's current level.
    // Query the game's own calculation on a context copy so the hook does not
    // leak its temporary register changes back into the caller.
    PPCContext queryContext = ctx;
    queryContext.r3.u32 = playerContext;
    __imp__sub_82318DF0(queryContext, base);
    return static_cast<float>(queryContext.f1.f64);
}

static void FillRingEnergy(PPCContext& ctx, uint8_t* base, uint32_t playerContext)
{
    if (!playerContext)
        return;

    PPCRegister energy{};
    energy.f32 = GetMaximumRingEnergy(ctx, base, playerContext);
    PPC_STORE_U32(playerContext + PLAYER_RING_ENERGY_OFFSET, energy.u32);
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
    if (Config::InfiniteRings)
        ctx.r4.u32 = 0;

    __imp__sub_82318AA0(ctx, base);
}

/* Damage/drop path. Unlike the small expenditure helper above, this routine
   clamps and subtracts the number of rings dropped after taking damage. It is
   used by both the daytime Sonic and nighttime Werehog player contexts. */
PPC_FUNC_IMPL(__imp__sub_8231FAE8);
PPC_FUNC(sub_8231FAE8)
{
    if (Config::InfiniteRings)
        ctx.r4.u32 = 0;

    __imp__sub_8231FAE8(ctx, base);
}

/* Ring getter/setter hooks. Keeping this entirely on the game's active call
   path avoids retaining player pointers after a stage transition. */
PPC_FUNC_IMPL(__imp__sub_82316B60);
PPC_FUNC(sub_82316B60)
{
    __imp__sub_82316B60(ctx, base);

    if (Config::InfiniteRings)
        ctx.r3.u32 = INFINITE_RING_COUNT;
}

PPC_FUNC_IMPL(__imp__sub_82316B68);
PPC_FUNC(sub_82316B68)
{
    if (Config::InfiniteRings)
        ctx.r4.u32 = INFINITE_RING_COUNT;

    __imp__sub_82316B68(ctx, base);
}

/* Ring Energy getter/setter and modifier hooks. Ring Energy is a float at a
   different field from the ring counter, and its maximum changes with the
   player's level. */
PPC_FUNC_IMPL(__imp__sub_82316B70);
PPC_FUNC(sub_82316B70)
{
    const auto playerContext = ctx.r3.u32;
    __imp__sub_82316B70(ctx, base);

    if (Config::InfiniteRingEnergy)
        ctx.f1.f64 = GetMaximumRingEnergy(ctx, base, playerContext);
}

PPC_FUNC_IMPL(__imp__sub_82316C70);
PPC_FUNC(sub_82316C70)
{
    if (Config::InfiniteRingEnergy)
        ctx.f1.f64 = GetMaximumRingEnergy(ctx, base, ctx.r3.u32);

    __imp__sub_82316C70(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_8231C590);
PPC_FUNC(sub_8231C590)
{
    const auto playerContext = ctx.r3.u32;
    __imp__sub_8231C590(ctx, base);

    if (Config::InfiniteRingEnergy)
        FillRingEnergy(ctx, base, playerContext);
}

PPC_FUNC_IMPL(__imp__sub_8231C628);
PPC_FUNC(sub_8231C628)
{
    const auto playerContext = ctx.r3.u32;
    __imp__sub_8231C628(ctx, base);

    if (Config::InfiniteRingEnergy)
        FillRingEnergy(ctx, base, playerContext);
}

// Player ring/energy initialisation. The context is valid for the duration of
// this call, so both cheats can start full without a persistent guest pointer.
PPC_FUNC_IMPL(__imp__sub_8244EEF8);
PPC_FUNC(sub_8244EEF8)
{
    const auto playerContext = ctx.r3.u32;
    __imp__sub_8244EEF8(ctx, base);

    if (Config::InfiniteRings)
        PPC_STORE_U32(playerContext + PLAYER_RING_COUNT_OFFSET, INFINITE_RING_COUNT);

    if (Config::InfiniteRingEnergy)
        FillRingEnergy(ctx, base, playerContext);
}

namespace PlayerPatches
{
    void Update()
    {
        if (!Config::InfiniteUnleash)
            return;

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
