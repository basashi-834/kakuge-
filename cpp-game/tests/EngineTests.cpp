// tests/EngineTests.cpp
// Engine-agnostic regression check for the core fight loop - the C++
// equivalent of winforms-game/tests/HeadlessLogicTest.ps1, exercising the
// exact same 25 checks. Compiles and runs as a plain native binary on
// Linux (g++) since the engine/ headers have zero Windows dependencies -
// no mingw/Wine needed for this part.
//
// Run with: ./build/EngineTests
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <random>
#include "../engine/Constants.h"
#include "../engine/Boxes.h"
#include "../engine/MoveData.h"
#include "../engine/MoveExecutor.h"
#include "../engine/CharacterStats.h"
#include "../engine/InputSystem.h"
#include "../engine/StateMachine.h"
#include "../engine/SuperGauge.h"
#include "../engine/Fighter.h"
#include "../engine/Projectile.h"
#include "../engine/CPUAI.h"
#include "../engine/BattleSystem.h"
#include "../engine/DataManager.h"

using namespace kakuge;
namespace fs = std::filesystem;

static int Passed = 0, Failed = 0;
static void Check(const std::string& label, bool cond) {
    if (cond) { Passed++; std::cout << "[OK]   " << label << "\n"; }
    else { Failed++; std::cout << "[FAIL] " << label << "\n"; }
}

static void TestGuard(DataManager& dm, const std::string& label, const std::string& moveId,
                       int crouchGuard /* -1=null(no input), 0=false(standing block), 1=true(crouch) */,
                       bool expectBlocked) {
    Fighter attacker, defender;
    attacker.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
    defender.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
    attacker.PositionX = -40; defender.PositionX = 40;
    attacker.Facing = Constants::FacingRight;
    defender.Facing = Constants::FacingLeft;
    attacker.Opponent = &defender; defender.Opponent = &attacker;

    if (crouchGuard == -1) {
        defender.SM.ChangeState(CharState::Idle, "");
        defender.InputBuf.RecordFrame(1, 5, {});
    } else if (crouchGuard == 1) {
        defender.SM.ChangeState(CharState::Crouch, "");
        defender.IsCrouchingGuard = true;
        defender.InputBuf.RecordFrame(1, 1, {}); // digit 1 = down+back, facing left -> right+down held
    } else {
        defender.SM.ChangeState(CharState::Block, "");
        defender.InputBuf.RecordFrame(1, 4, {}); // digit 4 = back only
    }

    const MoveData* move = attacker.GetMove(moveId);
    HitResult result = defender.ReceiveHit(*move, attacker);
    Check(label + " (blocked=" + (result.blocked ? "true" : "false") + ")", result.blocked == expectBlocked);
}

int main() {
    fs::path exeDir = fs::canonical("/proc/self/exe").parent_path();
    fs::path root = exeDir.parent_path(); // build/ -> cpp-game/
    fs::path dataDir = root / "data";
    if (!fs::is_directory(dataDir)) dataDir = fs::path(__FILE__).parent_path().parent_path() / "data";

    fs::path tempUserDir = fs::temp_directory_path() / ("KakugeTest_" + std::to_string(std::random_device{}()));
    DataManager dm(dataDir, tempUserDir);
    dm.ReloadAll();

    std::cout << "=== DataManager load ===\n";
    Check("loaded character 'ryu'", dm.GetCharacter("ryu") != nullptr);
    const auto* ryuMoves = dm.GetMoveset("ryu");
    Check("loaded 23 ryu moves", ryuMoves != nullptr && ryuMoves->size() == 23);

    // -----------------------------------------------------------------
    // 1) Full simulated match
    // -----------------------------------------------------------------
    std::cout << "\n=== Full match simulation (P1 idle vs CPU) ===\n";
    BattleSystem bs;
    bs.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);

    double dt = 1.0 / 60.0;
    RawInput neutral;
    bool sawHitboxActive = false, sawHpDrop = false;
    int maxFrames = 12000, frame = 0;
    while (bs.MatchActive && frame < maxFrames) {
        bs.Update(dt, neutral);
        if (!bs.Player2.ActiveHitboxRects.empty()) sawHitboxActive = true;
        if (bs.Player1.CurrentHP < bs.Player1.Stats.MaxHP) sawHpDrop = true;
        frame++;
    }
    Check("loop ended before frame cap (" + std::to_string(frame) + " frames)", frame < maxFrames);
    Check("hitbox became active at least once", sawHitboxActive);
    Check("player1 HP dropped at least once", sawHpDrop);
    Check("match ended via KO or timeout", !bs.MatchActive);
    Check("a winner or draw was recorded", bs.IsDraw || bs.Winner != nullptr);
    std::cout << "  final: P1hp=" << bs.Player1.CurrentHP << " P2hp=" << bs.Player2.CurrentHP
               << " draw=" << bs.IsDraw << " winner=" << (bs.Winner ? bs.Winner->Stats.Name : "none") << "\n";

    // -----------------------------------------------------------------
    // 2) Guard rule table
    // -----------------------------------------------------------------
    std::cout << "\n=== Guard rule table ===\n";
    TestGuard(dm, "High vs standing block -> BLOCKED", "standing_medium", 0, true);
    TestGuard(dm, "High vs crouching block -> BLOCKED", "standing_medium", 1, true);
    TestGuard(dm, "Low vs crouching block -> BLOCKED", "crouch_light", 1, true);
    TestGuard(dm, "Low vs standing block -> HIT", "crouch_light", 0, false);
    TestGuard(dm, "Overhead vs standing block -> BLOCKED", "jump_attack", 0, true);
    TestGuard(dm, "Overhead vs crouching block -> HIT", "jump_attack", 1, false);
    TestGuard(dm, "Throw vs standing block -> HIT (unblockable)", "standing_throw", 0, false);
    TestGuard(dm, "Throw vs crouching block -> HIT (unblockable)", "standing_throw", 1, false);
    TestGuard(dm, "High vs no guard input -> HIT", "standing_medium", -1, false);

    // -----------------------------------------------------------------
    // 3) Command parser + cancel window
    // -----------------------------------------------------------------
    std::cout << "\n=== Command parser + cancel window ===\n";
    InputBuffer buf;
    int f = 0;
    for (int d : {2, 2, 3, 3, 6}) { f++; buf.RecordFrame(f, d, {}); }
    f++; buf.RecordFrame(f, 6, {"Special"});
    Check("236+Special recognized", CommandParser::Matches(buf, "236", "Special", Constants::CommandWindow));
    Check("214 NOT recognized from a 236 buffer", !CommandParser::Matches(buf, "214", "Special", Constants::CommandWindow));

    InputBuffer buf2;
    f = 0;
    for (int d : {2, 3, 6}) { f++; buf2.RecordFrame(f, d, {}); }
    for (int i = 0; i < 20; i++) { f++; buf2.RecordFrame(f, 5, {}); }
    f++; buf2.RecordFrame(f, 5, {"Special"});
    Check("236+Special NOT recognized when button comes too late", !CommandParser::Matches(buf2, "236", "Special", Constants::CommandWindow));

    Fighter fighter;
    fighter.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
    fighter.Opponent = &fighter;
    RawInput neutral2;
    RawInput lightInput; lightInput.Buttons.LP = true;
    fighter.FrameStep(dt, lightInput);
    Check("LP press starts standing_light", fighter.SM.CurrentMove == "standing_light");

    for (int i = 0; i < 4; i++) fighter.FrameStep(dt, neutral2);
    Check("still in standing_light at cancel window open, frame=" + std::to_string(fighter.SM.CurrentFrame),
          fighter.SM.CurrentMove == "standing_light" && fighter.SM.CurrentFrame == 4);

    RawInput heavyInput; heavyInput.Buttons.HP = true;
    fighter.FrameStep(dt, heavyInput);
    Check("Heavy cancels standing_light into standing_heavy inside the window",
          fighter.SM.CurrentMove == "standing_heavy" && fighter.SM.CurrentFrame == 0);

    // -----------------------------------------------------------------
    // 4) DataManager save/reload persistence
    // -----------------------------------------------------------------
    std::cout << "\n=== DataManager save/reload persistence ===\n";
    CharacterStats statsCopy = *dm.GetCharacter("ryu");
    int originalHp = statsCopy.MaxHP;
    statsCopy.MaxHP = originalHp + 250;
    statsCopy.Name = "RYU-TEST";
    dm.SaveCharacter(statsCopy);

    MoveData moveCopy = *dm.GetMove("ryu", "standing_light");
    int originalDamage = moveCopy.Damage;
    moveCopy.Damage = originalDamage + 17;
    dm.SaveMove("ryu", moveCopy);

    DataManager dm2(dataDir, tempUserDir);
    dm2.ReloadAll();
    Check("reloaded maxHP matches the edit", dm2.GetCharacter("ryu")->MaxHP == originalHp + 250);
    Check("reloaded name matches the edit", dm2.GetCharacter("ryu")->Name == "RYU-TEST");
    Check("reloaded move damage matches the edit", dm2.GetMove("ryu", "standing_light")->Damage == originalDamage + 17);

    // -----------------------------------------------------------------
    // 5) New: in-game character creation (CreateCharacter)
    // -----------------------------------------------------------------
    std::cout << "\n=== In-game character creation ===\n";
    bool created = dm.CreateCharacter("ken", "KEN", "ryu");
    Check("CreateCharacter succeeded", created);
    Check("new character is immediately gettable", dm.GetCharacter("ken") != nullptr);
    const auto* kenMoves = dm.GetMoveset("ken");
    Check("new character cloned the template's moveset", kenMoves != nullptr && kenMoves->size() == 23);
    bool dupRejected = !dm.CreateCharacter("ken", "KEN2", "ryu");
    Check("duplicate id is rejected", dupRejected);

    DataManager dm3(dataDir, tempUserDir);
    dm3.ReloadAll();
    Check("new character persists across reload", dm3.GetCharacter("ken") != nullptr);

    // -----------------------------------------------------------------
    // 6) Collision spec (GameSpec in engine/Constants.h): pushbox per
    //    stance, 3-part hurtboxes, facing flip, throw range, wall rule,
    //    per-frame overrides, integer snapping, first-pass frame data.
    // -----------------------------------------------------------------
    std::cout << "\n=== Collision spec (GameSpec) ===\n";
    Check("start positions project to canvas x=116/268 (world -76/+76)",
          StageConstants::Player1StartX == -76.0 && StageConstants::Player2StartX == 76.0 &&
          StageConstants::PlayerStartDistance == 152.0);

    {
        Fighter f;
        f.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        f.Opponent = &f;
        f.PositionX = 0; f.PositionY = 0; f.Facing = Constants::FacingRight;

        RectBox stand = f.PushboxRect();
        Check("standing pushbox is 30x72 on the feet line (x -15..15, y -72..0)",
              stand.Width == 30 && stand.Height == 72 && stand.Left() == -15 && stand.Right() == 15 &&
              stand.Top() == -72 && stand.Bottom() == 0);

        f.SM.ChangeState(CharState::Crouch, "");
        RectBox crouch = f.PushboxRect();
        Check("crouching pushbox is 32x48 on the feet line",
              crouch.Width == 32 && crouch.Height == 48 && crouch.Bottom() == 0 && crouch.Left() == -16);

        f.SM.ChangeState(CharState::Jump, "");
        f.PositionY = -40;
        RectBox air = f.PushboxRect();
        Check("air pushbox is 28x52 and rides the body (torso-centered), not the ground",
              air.Width == 28 && air.Height == 52 && air.CenterY == -40 + Fighter::AirPushboxCenterY);
        f.SM.ChangeState(CharState::Idle, "");
        f.PositionY = 0;

        std::vector<RectBox> hurt = f.HurtboxRects();
        double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
        for (const auto& r : hurt) { left = std::min(left, r.Left()); right = std::max(right, r.Right()); top = std::min(top, r.Top()); bottom = std::max(bottom, r.Bottom()); }
        Check("standing hurtbox has 3 parts (head/torso/legs)", hurt.size() == 3);
        Check("standing hurtbox outer extent is 30 wide x 88 tall, feet at 0 (spec: 28-34 x 86-88)",
              (right - left) == 30 && (bottom - top) == 88 && bottom == 0);

        f.SM.ChangeState(CharState::Crouch, "");
        std::vector<RectBox> churt = f.HurtboxRects();
        double ctop = 1e9;
        for (const auto& r : churt) ctop = std::min(ctop, r.Top());
        Check("crouching hurtbox total height is within 50-58", churt.size() == 3 && -ctop >= 50 && -ctop <= 58);
        f.SM.ChangeState(CharState::Idle, "");

        f.PositionX = 10.4;
        Check("collision rects snap the fractional position to whole pixels", f.PushboxRect().CenterX == 10.0 && f.HurtboxRects()[0].CenterX == 10.0);
    }

    {
        const MoveData* lp = dm.GetMove("ryu", "standing_light");
        const MoveData* hp = dm.GetMove("ryu", "standing_heavy");
        const MoveData* lk = dm.GetMove("ryu", "standing_light_kick");
        const MoveData* hk = dm.GetMove("ryu", "standing_heavy_kick");
        const MoveData* clk = dm.GetMove("ryu", "crouch_light");
        Check("first-pass frame data: LP 4/3/7, HP 7/3/13, LK 5/3/8, HK 9/4/16",
              lp->Startup == 4 && lp->Active == 3 && lp->Recovery == 7 && lp->TotalFrame == 14 &&
              hp->Startup == 7 && hp->Active == 3 && hp->Recovery == 13 && hp->TotalFrame == 23 &&
              lk->Startup == 5 && lk->Active == 3 && lk->Recovery == 8 && lk->TotalFrame == 16 &&
              hk->Startup == 9 && hk->Active == 4 && hk->Recovery == 16 && hk->TotalFrame == 29);

        // Facing right from x=100: LP box is x 118..134, y -66..-56 (reach 34).
        auto rightBoxes = MoveExecutor::GetActiveHitboxRects(*lp, lp->Startup, Constants::FacingRight, 100, 0);
        auto leftBoxes = MoveExecutor::GetActiveHitboxRects(*lp, lp->Startup, Constants::FacingLeft, 100, 0);
        Check("LP hitbox: 16x10, x +18..+34, y -66..-56 when facing right",
              rightBoxes.size() == 1 && rightBoxes[0].Width == 16 && rightBoxes[0].Height == 10 &&
              rightBoxes[0].Left() == 118 && rightBoxes[0].Right() == 134 && rightBoxes[0].Top() == -66 && rightBoxes[0].Bottom() == -56);
        Check("LP hitbox mirrors to x -34..-18 when facing left (same data, flipped)",
              leftBoxes.size() == 1 && leftBoxes[0].Left() == 66 && leftBoxes[0].Right() == 82);
        Check("no hitbox exists outside the Active window (startup frame)",
              MoveExecutor::GetActiveHitboxRects(*lp, 0, Constants::FacingRight, 100, 0).empty());
        auto hkBoxes = MoveExecutor::GetActiveHitboxRects(*hk, hk->Startup, Constants::FacingRight, 0, 0);
        Check("HK hitbox: 24x14 reaching to +48 (longest normal)", hkBoxes.size() == 1 && hkBoxes[0].Width == 24 && hkBoxes[0].Height == 14 && hkBoxes[0].Right() == 48);
        auto clkBoxes = MoveExecutor::GetActiveHitboxRects(*clk, clk->Startup, Constants::FacingRight, 0, 0);
        Check("crouch LK hitbox is a low: 20x8 at y -12..-4", clkBoxes.size() == 1 && clkBoxes[0].Top() == -12 && clkBoxes[0].Bottom() == -4);

        // Per-frame override: hitbox on startup frames 0-1, then the
        // normal Active window; a hurtbox override shrinks to one part.
        MoveData custom;
        custom.Startup = 3; custom.Active = 2; custom.Recovery = 2; custom.TotalFrame = 7;
        custom.Hitboxes.push_back({20, -20, 8, 8});
        FrameBoxSet fb;
        fb.startFrame = 0; fb.endFrame = 1;
        fb.hasHitboxes = true; fb.hitboxes.push_back({10, -10, 6, 6});
        fb.hasHurtboxes = true; fb.hurtboxes.push_back({"torso", RectBox{0, -40, 20, 80}});
        fb.hasPushbox = true; fb.pushbox = RectBox{0, -20, 10, 40};
        custom.FrameBoxes.push_back(fb);
        auto ovr = MoveExecutor::GetActiveHitboxRects(custom, 0, Constants::FacingRight, 0, 0);
        auto normal = MoveExecutor::GetActiveHitboxRects(custom, 3, Constants::FacingRight, 0, 0);
        Check("frameBoxes hitbox override applies on its frames even during startup",
              MoveExecutor::HasLiveHitboxes(custom, 0) && ovr.size() == 1 && ovr[0].Width == 6 && !MoveExecutor::HasLiveHitboxes(custom, 2));
        Check("frames without an override fall back to the move's Active-window hitboxes", normal.size() == 1 && normal[0].Width == 8);
        Fighter f2;
        f2.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        f2.Opponent = &f2;
        f2.CurrentMoveData = &custom;
        f2.SM.ChangeState(CharState::Attack, "custom");
        Check("frameBoxes hurtbox/pushbox overrides replace the stance boxes on their frames",
              f2.HurtboxRects().size() == 1 && f2.HurtboxRects()[0].Height == 80 && f2.PushboxRect().Width == 10);
    }

    {
        BattleSystem bsThrow;
        bsThrow.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        const MoveData* throwMove = bsThrow.Player1.GetMove("standing_throw");
        auto armThrow = [&](double defX, double defY) {
            bsThrow.ResetHP();
            // Fully reset the attacker between attempts: ChangeState is a
            // no-op for the same state+move, so a leftover Attack state
            // would keep the previous attempt's frame counter.
            bsThrow.Player1.SM.ChangeState(CharState::Idle, "");
            bsThrow.Player1.CurrentMoveData = nullptr;
            bsThrow.Player1.ActiveHitboxRects.clear();
            bsThrow.Player1.PositionX = 0; bsThrow.Player1.PositionY = 0; bsThrow.Player1.Facing = Constants::FacingRight;
            bsThrow.Player2.PositionX = defX; bsThrow.Player2.PositionY = defY; bsThrow.Player2.Facing = Constants::FacingLeft;
            bsThrow.Player2.SM.ChangeState(defY < 0 ? CharState::Jump : CharState::Idle, "");
            bsThrow.Player1.StartMove(*throwMove);
            for (int i = 0; i < throwMove->Startup; i++) bsThrow.Player1.SM.Tick();
            bsThrow.Player1.ProgressMove();
            bsThrow.ResolveCombat(bsThrow.Player1, bsThrow.Player2);
            return bsThrow.Player2.SM.CurrentState == CharState::Throw;
        };
        Check("throw connects at center distance 20 (<= NORMAL_THROW_RANGE 28)", armThrow(20, 0));
        Check("throw whiffs at center distance 40 (> 28) even though the old hitbox would have reached", !armThrow(40, 0));
        Check("throw whiffs on an airborne opponent at any distance", !armThrow(20, -30));
        Check("throw resolves on distance, not hitbox-vs-hurtbox", throwMove->ThrowRange == 28.0 && throwMove->GuardType == Constants::GuardThrow);
    }

    {
        BattleSystem bsPush;
        bsPush.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        bsPush.Player1.PositionX = 0; bsPush.Player2.PositionX = 20; // 30-wide boxes -> 10 overlap
        bsPush.ResolvePushboxes();
        Check("pushbox overlap of 10 splits half/half (-5 / +5)", bsPush.Player1.PositionX == -5 && bsPush.Player2.PositionX == 25);

        bsPush.Player1.PositionX = BattleSystem::StageMinX; bsPush.Player2.PositionX = BattleSystem::StageMinX + 20;
        bsPush.ResolvePushboxes();
        Check("cornered fighter doesn't move; the other absorbs the full overlap",
              bsPush.Player1.PositionX == BattleSystem::StageMinX && bsPush.Player2.PositionX == BattleSystem::StageMinX + 30);
    }

    std::error_code ec;
    fs::remove_all(tempUserDir, ec);

    std::cout << "\n=== RESULT: " << Passed << " passed, " << Failed << " failed ===\n";
    return Failed > 0 ? 1 : 0;
}
