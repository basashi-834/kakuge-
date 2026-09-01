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
    Check("loaded 12 ryu moves", ryuMoves != nullptr && ryuMoves->size() == 12);

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
        if (bs.Player2.ActiveHitboxValid) sawHitboxActive = true;
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
    Check("new character cloned the template's moveset", kenMoves != nullptr && kenMoves->size() == 12);
    bool dupRejected = !dm.CreateCharacter("ken", "KEN2", "ryu");
    Check("duplicate id is rejected", dupRejected);

    DataManager dm3(dataDir, tempUserDir);
    dm3.ReloadAll();
    Check("new character persists across reload", dm3.GetCharacter("ken") != nullptr);

    std::error_code ec;
    fs::remove_all(tempUserDir, ec);

    std::cout << "\n=== RESULT: " << Passed << " passed, " << Failed << " failed ===\n";
    return Failed > 0 ? 1 : 0;
}
