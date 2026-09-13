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
static std::array<uint32_t, 4> g_playerContexts{};

static constexpr uint32_t PLAYER_RING_COUNT_OFFSET = 1336;
static constexpr uint32_t PLAYER_RING_ENERGY_OFFSET = 1340;
static constexpr uint32_t INFINITE_RING_COUNT = 999;

static void StoreGuestU32(uint32_t address, uint32_t value)
{
    if (auto pValue = reinterpret_cast<be<uint32_t>*>(g_memory.Translate(address)))
        *pValue = value;
}

static uint32_t LoadGuestU32(uint32_t address)
{
    if (auto pValue = reinterpret_cast<const be<uint32_t>*>(g_memory.Translate(address)))
        return *pValue;

    return 0;
}

static bool IsInfiniteJumpTap()
{
    if (!Config::InfiniteJump)
        return false;

    auto input = SWA::CInputState::GetInstance();
    return input && input->GetPadState().IsTapped(SWA::eKeyState_A);
}

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

static void TrackPlayerContext(uint32_t context)
{
    if (!context)
        return;

    for (auto known : g_playerContexts)
    {
        if (known == context)
            return;
    }

    for (auto& known : g_playerContexts)
    {
        if (!known)
        {
            known = context;
            return;
        }
    }

    g_playerContexts[0] = g_playerContexts[1];
    g_playerContexts[1] = g_playerContexts[2];
    g_playerContexts[2] = g_playerContexts[3];
    g_playerContexts[3] = context;
}

static void ForgetPlayerContext(uint32_t context)
{
    for (auto& known : g_playerContexts)
    {
        if (known == context)
            known = 0;
    }
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

    TrackPlayerContext(playerContext);

    if (Config::InfiniteRings)
        PPC_STORE_U32(playerContext + PLAYER_RING_COUNT_OFFSET, INFINITE_RING_COUNT);

    if (Config::InfiniteRingEnergy)
        FillRingEnergy(ctx, base, playerContext);
}

namespace PlayerPatches
{
    void Update()
    {
        if (!Config::InfiniteUnleash && !Config::InfiniteRings)
            return;

        // Keep the Werehog's Dark Gaia/Unleash gauge full from the first
        // frame and after every game tick. Ring writes below use the shared
        // player context so both character forms stay in sync.
        for (auto context : g_evilSonicContexts)
        {
            if (context)
            {
                auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(context);
                if (pEvilSonicContext)
                {
                    if (Config::InfiniteUnleash)
                        pEvilSonicContext->m_DarkGaiaEnergy = 100.0f;
                    if (Config::InfiniteRings)
                        StoreGuestU32(context + PLAYER_RING_COUNT_OFFSET, INFINITE_RING_COUNT);
                }
            }
        }

        if (Config::InfiniteRings)
        {
            // The base player context is shared by daytime Sonic and the
            // Werehog. Keep the backing counter full as well as the public
            // getter/setter hooks so the HUD cannot remain at 000 after a
            // character transition.
            for (auto context : g_playerContexts)
            {
                if (context && g_memory.Translate(context))
                    StoreGuestU32(context + PLAYER_RING_COUNT_OFFSET, INFINITE_RING_COUNT);
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
    TrackPlayerContext(ctx.r3.u32);
    if (Config::InfiniteRings)
        PPC_STORE_U32(ctx.r3.u32 + PLAYER_RING_COUNT_OFFSET, INFINITE_RING_COUNT);
    if (Config::InfiniteUnleash)
    {
        if (auto pEvilSonicContext = (SWA::Player::CEvilSonicContext*)g_memory.Translate(ctx.r3.u32))
            pEvilSonicContext->m_DarkGaiaEnergy = 100.0f;
    }
    App::s_isWerehog = true;

    SDL_User_EvilSonic(true);
}

// The shared player context owns the normal Sonic and Werehog state. Tracking
// it at construction/destruction lets the ring refresh stay valid across a
// stage transition without retaining freed guest pointers.
PPC_FUNC_IMPL(__imp__sub_8230D620);
PPC_FUNC(sub_8230D620)
{
    __imp__sub_8230D620(ctx, base);
    TrackPlayerContext(ctx.r3.u32);
}

PPC_FUNC_IMPL(__imp__sub_82319A78);
PPC_FUNC(sub_82319A78)
{
    ForgetPlayerContext(ctx.r3.u32);
    ForgetEvilSonicContext(ctx.r3.u32);
    __imp__sub_82319A78(ctx, base);
}

// CStateJump and CStateJumpSecond both apply the game's own jump impulse in
// their entry routine. Re-entering the active state on an A tap gives a true
// mid-air jump and keeps all of the character-specific animation/physics
// handling intact. It also avoids holding A to alter gravity.
PPC_FUNC_IMPL(__imp__sub_823F3500);
PPC_FUNC_IMPL(__imp__sub_823F3C48);
PPC_FUNC(sub_823F3C48)
{
    if (IsInfiniteJumpTap())
    {
        PPCContext jumpContext = ctx;
        __imp__sub_823F3500(jumpContext, base);
    }

    __imp__sub_823F3C48(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_823F6550);
PPC_FUNC_IMPL(__imp__sub_823F6698);
PPC_FUNC(sub_823F6698)
{
    if (IsInfiniteJumpTap())
    {
        PPCContext jumpContext = ctx;
        __imp__sub_823F6550(jumpContext, base);
    }

    __imp__sub_823F6698(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_82DFB728);
PPC_FUNC_IMPL(__imp__sub_82E67A88);
PPC_FUNC_IMPL(__imp__sub_822C0890);

static bool RequestWerehogJumpSecond(PPCContext& sourceContext, uint8_t* base)
{
    // The Werehog state table stores Evil_JumpSecond at 0x832783A0. Use the
    // same descriptor -> state request path as the original game instead of
    // invoking the JumpSecond entry routine while the state machine is still
    // in Evil_Fall.
    static constexpr uint32_t EVIL_JUMP_SECOND_DESCRIPTOR = 0x832783A0;
    static constexpr uint32_t TEMP_STACK_SIZE = 0x40;
    static constexpr uint32_t DESCRIPTOR_TEMP_OFFSET = 0x10;
    static constexpr uint32_t RESULT_TEMP_OFFSET = 0x20;

    const uint32_t stateContext = sourceContext.r3.u32;
    const uint32_t stateDescriptor = LoadGuestU32(EVIL_JUMP_SECOND_DESCRIPTOR);
    if (!stateContext || !stateDescriptor)
        return false;

    PPCContext transitionContext = sourceContext;
    const uint32_t tempStack = (transitionContext.r1.u32 - TEMP_STACK_SIZE) & ~0xFu;
    const uint32_t descriptorTemp = tempStack + DESCRIPTOR_TEMP_OFFSET;
    const uint32_t resultTemp = tempStack + RESULT_TEMP_OFFSET;

    transitionContext.r1.u32 = tempStack;
    StoreGuestU32(resultTemp, 0);
    StoreGuestU32(resultTemp + 4, 0);

    transitionContext.r3.u32 = descriptorTemp;
    transitionContext.r4.u32 = stateDescriptor;
    __imp__sub_82DFB728(transitionContext, base);

    transitionContext.r5.u32 = transitionContext.r3.u32;
    transitionContext.r4.u32 = stateContext;
    transitionContext.r3.u32 = resultTemp;
    transitionContext.r6.u32 = 0;
    transitionContext.r8.u32 = 0;
    transitionContext.f1.f64 = 0.0;
    __imp__sub_82E67A88(transitionContext, base);

    // The native callers release the shared request object stored at +4.
    if (const uint32_t requestObject = LoadGuestU32(resultTemp + 4))
    {
        transitionContext.r3.u32 = requestObject;
        __imp__sub_822C0890(transitionContext, base);
    }

    return true;
}

// Werehog transitions into Evil_Fall after the upward part of a jump. A new
// A tap must request a real Evil_JumpSecond state transition. Calling only the
// jump initializer from this hook leaves the state machine in Evil_Fall, which
// is what caused the floating/stuck landing after repeated mid-air jumps.
PPC_FUNC_IMPL(__imp__sub_823F6A78);
PPC_FUNC(sub_823F6A78)
{
    if (IsInfiniteJumpTap() && RequestWerehogJumpSecond(ctx, base))
        return;

    __imp__sub_823F6A78(ctx, base);
}

static bool RequestSonicJumpBall(PPCContext& sourceContext, uint8_t* base, uint32_t stateContext)
{
    // The speed-context state-name table used by the surrounding daytime
    // routines stores JumpBall at 0x8326B9A0 (Stand/Walk/JumpShort/JumpBall).
    // Request the state through the game's state machine so JumpBall's normal
    // entry code applies a fresh jump impulse instead of directly invoking an
    // update routine while Sonic is still in the previous airborne state.
    static constexpr uint32_t SONIC_JUMP_BALL_DESCRIPTOR = 0x8326B9A0;
    static constexpr uint32_t TEMP_STACK_SIZE = 0x40;
    static constexpr uint32_t DESCRIPTOR_TEMP_OFFSET = 0x10;
    static constexpr uint32_t RESULT_TEMP_OFFSET = 0x20;

    const uint32_t stateDescriptor = LoadGuestU32(SONIC_JUMP_BALL_DESCRIPTOR);
    if (!stateContext || !stateDescriptor)
        return false;

    PPCContext transitionContext = sourceContext;
    const uint32_t tempStack = (transitionContext.r1.u32 - TEMP_STACK_SIZE) & ~0xFu;
    const uint32_t descriptorTemp = tempStack + DESCRIPTOR_TEMP_OFFSET;
    const uint32_t resultTemp = tempStack + RESULT_TEMP_OFFSET;

    transitionContext.r1.u32 = tempStack;
    StoreGuestU32(resultTemp, 0);
    StoreGuestU32(resultTemp + 4, 0);

    transitionContext.r3.u32 = descriptorTemp;
    transitionContext.r4.u32 = stateDescriptor;
    __imp__sub_82DFB728(transitionContext, base);

    // Native JumpBall callers pass the object returned in r3 by
    // sub_82DFB728 as r5 to the transition request. Passing descriptorTemp
    // here silently produces an invalid request, which is why daytime Sonic
    // previously showed no mid-air jump even though this hook was reached.
    transitionContext.r5.u32 = transitionContext.r3.u32;
    transitionContext.r4.u32 = stateContext;
    transitionContext.r3.u32 = resultTemp;
    transitionContext.r6.u32 = 0;
    transitionContext.r8.u32 = 0;
    transitionContext.f1.f64 = 0.0;
    __imp__sub_82E67A88(transitionContext, base);

    if (const uint32_t requestObject = LoadGuestU32(resultTemp + 4))
    {
        transitionContext.r3.u32 = requestObject;
        __imp__sub_822C0890(transitionContext, base);
    }

    return true;
}

static bool TryRequestSonicMidairJump(PPCContext& ctx, uint8_t* base, uint32_t stateContext)
{
    return !App::s_isWerehog && IsInfiniteJumpTap() &&
        RequestSonicJumpBall(ctx, base, stateContext);
}

// Daytime Sonic does not use one common airborne update routine. Different
// jump/fall substates enter separate state handlers, and several of those
// handlers contain their own native JumpBall transition. Hook the handlers
// that already use JumpBall and feed the exact same state object (entry r3)
// into the transition request. This makes a fresh A tap work while rising or
// falling instead of only in the one substate that reaches sub_82395940.
PPC_FUNC_IMPL(__imp__sub_82392FF0);
PPC_FUNC(sub_82392FF0)
{
    if (TryRequestSonicMidairJump(ctx, base, ctx.r3.u32))
        return;

    __imp__sub_82392FF0(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_82394F30);
PPC_FUNC(sub_82394F30)
{
    if (TryRequestSonicMidairJump(ctx, base, ctx.r3.u32))
        return;

    __imp__sub_82394F30(ctx, base);
}

// sub_823386E0 also has a native JumpBall request. Keep it as a direct
// fallback for paths that reach this handler without going through
// sub_82395940 first.
PPC_FUNC_IMPL(__imp__sub_823386E0);
PPC_FUNC(sub_823386E0)
{
    if (TryRequestSonicMidairJump(ctx, base, ctx.r3.u32))
        return;

    __imp__sub_823386E0(ctx, base);
}

// This wrapper sits above one more daytime virtual-state branch. Catch the
// tap before the branch can report the frame handled and skip sub_823386E0.
PPC_FUNC_IMPL(__imp__sub_82395940);
PPC_FUNC(sub_82395940)
{
    if (TryRequestSonicMidairJump(ctx, base, ctx.r3.u32))
        return;

    __imp__sub_82395940(ctx, base);
}

// ~SWA::Player::CEvilSonicContext
PPC_FUNC_IMPL(__imp__sub_823B4590);
PPC_FUNC(sub_823B4590)
{
    const auto context = ctx.r3.u32;
    __imp__sub_823B4590(ctx, base);

    ForgetEvilSonicContext(context);

    App::s_isWerehog = false;

    SDL_User_EvilSonic(false);
}
