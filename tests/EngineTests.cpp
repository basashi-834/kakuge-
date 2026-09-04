// =====================================================================
// tests/EngineTests.cpp - エンジンの動作確認（自動テスト）
// =====================================================================
// ゲーム画面を一切出さずに、戦闘のルールだけを検証するプログラムです。
// SDL2 を使わないので、画面のない環境でも実行できます。
//
// なぜテストを書くのか
// -----------------
// 格闘ゲームのルールは細かく絡み合っています。「下段は立ちガードでは
// 防げない」「投げは距離で判定する」「持続 3 フレームの技は 1 回しか
// 当たらない」…。どこか 1 か所を直したときに、別の場所が壊れていない
// ことを毎回手で確かめるのは現実的ではありません。
// このプログラムを 1 回走らせれば、40 項目以上を数秒で確認できます。
//
// 実行のしかた
//   Windows : build\bin\EngineTests.exe
//   Linux   : ./build/bin/EngineTests
// 引数に data フォルダの場所を渡すこともできます。
// 省略した場合はいくつかの定番の場所を自動で探します。
//
// 最後に「RESULT: N passed, 0 failed」と出れば成功です。
// 失敗があると終了コードが 1 になるので、自動化にも組み込めます。
// =====================================================================
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <random>
#include <string>
#include <vector>

#include "core/TrueType.h"
#include "engine/BattleSystem.h"
#include "engine/Boxes.h"
#include "engine/CPUAI.h"
#include "engine/CharacterStats.h"
#include "engine/Constants.h"
#include "engine/DataManager.h"
#include "engine/Fighter.h"
#include "engine/InputSystem.h"
#include "engine/MoveData.h"
#include "engine/MoveExecutor.h"
#include "engine/Projectile.h"
#include "engine/StateMachine.h"
#include "engine/SuperGauge.h"

using namespace kakuge;
namespace fs = std::filesystem;

static int Passed = 0, Failed = 0;

// 1 項目ぶんの確認。条件が true なら [OK]、false なら [FAIL] を表示します。
static void Check(const std::string& label, bool cond) {
    if (cond) { Passed++; std::cout << "[OK]   " << label << "\n"; }
    else { Failed++; std::cout << "[FAIL] " << label << "\n"; }
}

// data フォルダを探す。
// 実行する場所によって位置が変わるので、ありそうな所を順に見ます。
static fs::path FindDataDir(int argc, char** argv) {
    std::error_code ec;
    if (argc > 1) {
        fs::path given(argv[1]);
        if (fs::is_directory(given, ec)) return given;
    }
    // よくある配置を順番に確認します。
    const char* candidates[] = {
        "data",             // 実行ファイルの隣（CMake がコピーした場所）
        "../data",
        "../../data",
        "../../../data",
    };
    for (const char* c : candidates) {
        fs::path p(c);
        if (fs::is_directory(p, ec)) return p;
    }
    // 最後の手段: このソースファイルの位置から辿る
    // （__FILE__ はコンパイル時に埋め込まれるソースのパスです）
    fs::path fromSource = fs::path(__FILE__).parent_path().parent_path() / "data";
    return fromSource;
}

// ガードのルール表を確認するための補助関数。
// crouchGuard: -1 = 何も入力していない / 0 = 立ちガード / 1 = しゃがみガード
static void TestGuard(DataManager& dm, const std::string& label, const std::string& moveId,
                      int crouchGuard, bool expectBlocked) {
    Fighter attacker, defender;
    attacker.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
    defender.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
    attacker.PositionX = -40; defender.PositionX = 40;
    attacker.Facing = Constants::FacingRight;
    defender.Facing = Constants::FacingLeft;
    attacker.Opponent = &defender; defender.Opponent = &attacker;

    // ガード判定は「入力履歴の最新の 1 件」を見るので、
    // その 1 件を直接作って状況を再現します。
    if (crouchGuard == -1) {
        defender.SM.ChangeState(CharState::Idle, "");
        defender.InputBuf.RecordFrame(1, 5, {}); // 5 = ニュートラル
    } else if (crouchGuard == 1) {
        defender.SM.ChangeState(CharState::Crouch, "");
        defender.IsCrouchingGuard = true;
        defender.InputBuf.RecordFrame(1, 1, {}); // 1 = 下＋後ろ
    } else {
        defender.SM.ChangeState(CharState::Block, "");
        defender.InputBuf.RecordFrame(1, 4, {}); // 4 = 後ろ
    }

    const MoveData* move = attacker.GetMove(moveId);
    HitResult result = defender.ReceiveHit(*move, attacker);
    Check(label + " (blocked=" + (result.blocked ? "true" : "false") + ")",
          result.blocked == expectBlocked);
}

int main(int argc, char** argv) {
    fs::path dataDir = FindDataDir(argc, argv);
    std::cout << "data フォルダ: " << dataDir.string() << "\n\n";

    // テスト中の保存先は一時フォルダにします。
    // 本物の保存フォルダを汚さないようにするためです。
    fs::path tempUserDir = fs::temp_directory_path() /
                           ("KakugeTest_" + std::to_string(std::random_device{}()));
    DataManager dm(dataDir, tempUserDir);
    dm.ReloadAll();

    // =================================================================
    std::cout << "=== 自前 JSON パーサ ===\n";
    // =================================================================
    {
        Json j;
        std::string err;
        bool ok = Json::Parse(
            R"({"id":"test","n":12,"f":-3.5,"b":true,"nul":null,
                "arr":[1,2,3],"obj":{"k":"v"},"esc":"a\"b\\c\nd","jp":"技名"})",
            j, &err);
        Check("基本的な JSON を解析できる", ok);
        Check("文字列を読める", j.GetString("id", "") == "test");
        Check("整数を読める", j.GetInt("n", 0) == 12);
        Check("小数（負の値）を読める", j.GetNumber("f", 0) == -3.5);
        Check("真偽値を読める", j.GetBool("b", false) == true);
        Check("null を読める", j.Find("nul") != nullptr && j.Find("nul")->IsNull());
        Check("配列を読める", j.Find("arr") && j.Find("arr")->Size() == 3 &&
                              j.Find("arr")->At(1).AsInt() == 2);
        Check("入れ子のオブジェクトを読める",
              j.Find("obj") && j.Find("obj")->GetString("k", "") == "v");
        Check("エスケープを解釈できる", j.GetString("esc", "") == "a\"b\\c\nd");
        Check("日本語（UTF-8）をそのまま読める", j.GetString("jp", "") == "技名");
        Check("存在しないキーは既定値を返す", j.GetInt("nothing", 99) == 99);

        // 書き出して読み直したとき、同じ値になるか（往復テスト）。
        // データを保存して読み込むたびに値が変わってしまわないことの確認です。
        Json out = Json::MakeObject();
        out.Set("a", Json(1000));
        out.Set("b", Json(0.5));
        out.Set("c", Json("あいう"));
        Json arr = Json::MakeArray();
        arr.Push(Json(1)); arr.Push(Json(2));
        out.Set("d", std::move(arr));
        Json back;
        Check("書き出した JSON を読み直せる（往復）",
              Json::Parse(out.Dump(4), back, nullptr) &&
              back.GetInt("a", 0) == 1000 && back.GetNumber("b", 0) == 0.5 &&
              back.GetString("c", "") == "あいう" && back.Find("d")->Size() == 2);
        Check("整数は小数点なしで書き出される",
              out.Dump(0).find("\"a\":1000") != std::string::npos);

        Json bad;
        Check("壊れた JSON はエラーになる（落ちない）",
              !Json::Parse("{\"a\":}", bad, &err) && !err.empty());
        Check("末尾の余分な文字を検出する", !Json::Parse("{} x", bad, nullptr));
    }

    // =================================================================
    std::cout << "\n=== データの読み込み ===\n";
    // =================================================================
    Check("キャラクター 'ryu' を読み込めた", dm.GetCharacter("ryu") != nullptr);
    const auto* ryuMoves = dm.GetMoveset("ryu");
    Check("ryu の技を 23 個読み込めた", ryuMoves != nullptr && ryuMoves->size() == 23);
    if (!dm.GetCharacter("ryu")) {
        std::cout << "\nデータが読めないため、以降のテストを中止します。\n"
                  << "data フォルダの場所を引数で渡してみてください。\n";
        return 1;
    }

    // =================================================================
    std::cout << "\n=== 試合をまるごと 1 回シミュレート ===\n";
    // =================================================================
    // プレイヤーは何もせず、CPU だけが動く試合を最後まで回します。
    // 「決着がつく」「CPU が攻めた」「ダメージが入る」を確認します。
    //
    // CPU の思考には乱数が入るので、種を固定して毎回同じ試合にします。
    // 固定しないと、実行のたびに違う展開になり、たまたま失敗する
    // テストになってしまいます（実際そうなっていました）。
    {
        BattleSystem bs;
        bs.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"),
                      *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        bs.CpuAI->Rng.seed(20240601u);
        double dt = 1.0 / 60.0;
        RawInput neutral;
        bool sawAttack = false, sawHpDrop = false, sawProjectile = false;
        int maxFrames = 12000, frame = 0;
        while (bs.MatchActive && frame < maxFrames) {
            bs.Update(dt, neutral);
            // 「攻撃した」の判定に本体の攻撃判定（ActiveHitboxRects）だけを
            // 見てはいけません。飛び道具は本体とは別のオブジェクトなので、
            // CPU が波動拳だけで倒しきると本体の判定は一度も出ないためです。
            if (bs.Player2.SM.CurrentState == CharState::Attack) sawAttack = true;
            if (!bs.Projectiles.empty()) sawProjectile = true;
            if (bs.Player1.CurrentHP < bs.Player1.Stats.MaxHP) sawHpDrop = true;
            frame++;
        }
        Check("上限フレームより前に決着した (" + std::to_string(frame) + " フレーム)",
              frame < maxFrames);
        Check("CPU が攻撃行動を取った", sawAttack);
        Check("飛び道具が飛んだ", sawProjectile);
        Check("プレイヤー 1 の体力が減った", sawHpDrop);
        Check("KO か時間切れで試合が終わった", !bs.MatchActive);
        Check("勝者または引き分けが記録された", bs.IsDraw || bs.Winner != nullptr);
        std::cout << "  結果: P1hp=" << bs.Player1.CurrentHP
                  << " P2hp=" << bs.Player2.CurrentHP
                  << " 引き分け=" << bs.IsDraw
                  << " 勝者=" << (bs.Winner ? bs.Winner->Stats.Name : "なし") << "\n";
    }

    // =================================================================
    std::cout << "\n=== ガードのルール表 ===\n";
    // =================================================================
    TestGuard(dm, "中段技を立ちガード -> 防げる", "standing_medium", 0, true);
    TestGuard(dm, "中段技をしゃがみガード -> 防げる", "standing_medium", 1, true);
    TestGuard(dm, "下段技をしゃがみガード -> 防げる", "crouch_light", 1, true);
    TestGuard(dm, "下段技を立ちガード -> 食らう", "crouch_light", 0, false);
    TestGuard(dm, "中段(Overhead)を立ちガード -> 防げる", "jump_attack", 0, true);
    TestGuard(dm, "中段(Overhead)をしゃがみガード -> 食らう", "jump_attack", 1, false);
    TestGuard(dm, "投げを立ちガード -> 食らう（ガード不能）", "standing_throw", 0, false);
    TestGuard(dm, "投げをしゃがみガード -> 食らう（ガード不能）", "standing_throw", 1, false);
    TestGuard(dm, "ガード入力なし -> 食らう", "standing_medium", -1, false);

    // =================================================================
    std::cout << "\n=== コマンド判定とキャンセル ===\n";
    // =================================================================
    {
        // 2,2,3,3,6 と入力してからボタン -> 236 コマンド成立
        InputBuffer buf;
        int f = 0;
        for (int d : {2, 2, 3, 3, 6}) { f++; buf.RecordFrame(f, d, {}); }
        f++; buf.RecordFrame(f, 6, {"Special"});
        Check("236+ボタン が成立する",
              CommandParser::Matches(buf, "236", "Special", Constants::CommandWindow));
        Check("236 の履歴で 214 は成立しない",
              !CommandParser::Matches(buf, "214", "Special", Constants::CommandWindow));

        // レバーを入れてから 20 フレーム後にボタン -> 遅すぎて不成立
        InputBuffer buf2;
        f = 0;
        for (int d : {2, 3, 6}) { f++; buf2.RecordFrame(f, d, {}); }
        for (int i = 0; i < 20; i++) { f++; buf2.RecordFrame(f, 5, {}); }
        f++; buf2.RecordFrame(f, 5, {"Special"});
        Check("ボタンが遅すぎるとコマンドは成立しない",
              !CommandParser::Matches(buf2, "236", "Special", Constants::CommandWindow));

        double dt = 1.0 / 60.0;
        Fighter fighter;
        fighter.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        fighter.Opponent = &fighter;
        RawInput neutral2;
        RawInput lightInput; lightInput.Buttons.LP = true;
        fighter.FrameStep(dt, lightInput);
        Check("LP を押すと立ち弱パンチが出る", fighter.SM.CurrentMove == "standing_light");

        // 技を出したフレームが 1F 目なので、4 フレーム進めると 5F 目。
        for (int i = 0; i < 4; i++) fighter.FrameStep(dt, neutral2);
        Check("4 フレーム後もまだ立ち弱パンチ中",
              fighter.SM.CurrentMove == "standing_light" && fighter.SM.CurrentFrame == 5);

        // キャンセルは「当たったかどうか」でも変わります。
        // 立ち弱パンチのターゲットコンボ設定は空振り時 × なので、
        // 誰にも当たっていないこの状態ではキャンセルできません。
        RawInput heavyInput; heavyInput.Buttons.HP = true;
        fighter.FrameStep(dt, heavyInput);
        Check("空振りではキャンセルできない（空振り時 × の設定）",
              fighter.SM.CurrentMove == "standing_light");

        // ヒットしたことにすると、同じ入力でキャンセルできます。
        // さっき押した強パンチは先行入力に残っているので、
        // ボタンを押し直さなくても次のフレームで技になります。
        fighter.CurrentMoveContact = MoveContact::Hit;
        fighter.FrameStep(dt, RawInput{});
        Check("ヒット時はキャンセル可能時間内に強パンチでコンボが繋がる",
              fighter.SM.CurrentMove == "standing_heavy" && fighter.SM.CurrentFrame == 1);
    }

    // =================================================================
    std::cout << "\n=== データの保存と読み直し ===\n";
    // =================================================================
    {
        CharacterStats statsCopy = *dm.GetCharacter("ryu");
        int originalHp = statsCopy.MaxHP;
        statsCopy.MaxHP = originalHp + 250;
        statsCopy.Name = "RYU-TEST";
        dm.SaveCharacter(statsCopy);

        MoveData moveCopy = *dm.GetMove("ryu", "standing_light");
        int originalDamage = moveCopy.Damage;
        moveCopy.Damage = originalDamage + 17;
        dm.SaveMove("ryu", moveCopy);

        // 別のインスタンスで読み直して、保存が効いているか確認します。
        DataManager dm2(dataDir, tempUserDir);
        dm2.ReloadAll();
        Check("保存した体力の値が読み直せる", dm2.GetCharacter("ryu")->MaxHP == originalHp + 250);
        Check("保存した名前が読み直せる", dm2.GetCharacter("ryu")->Name == "RYU-TEST");
        Check("保存した技のダメージが読み直せる",
              dm2.GetMove("ryu", "standing_light")->Damage == originalDamage + 17);
    }

    // =================================================================
    std::cout << "\n=== キャラクターの新規作成 ===\n";
    // =================================================================
    {
        bool created = dm.CreateCharacter("ken", "KEN", "ryu");
        Check("新しいキャラクターを作成できた", created);
        Check("作成直後から取得できる", dm.GetCharacter("ken") != nullptr);
        const auto* kenMoves = dm.GetMoveset("ken");
        Check("雛形の技一式がコピーされた", kenMoves != nullptr && kenMoves->size() == 23);
        Check("同じ ID の重複作成は拒否される", !dm.CreateCharacter("ken", "KEN2", "ryu"));

        DataManager dm3(dataDir, tempUserDir);
        dm3.ReloadAll();
        Check("再読み込みしても残っている", dm3.GetCharacter("ken") != nullptr);
    }

    // =================================================================
    std::cout << "\n=== 当たり判定の寸法（仕様どおりか）===\n";
    // =================================================================
    Check("ラウンド開始の間合いは 175（左右対称に -87.5/+87.5）",
          StageConstants::Player1StartX == -87.5 && StageConstants::Player2StartX == 87.5 &&
          StageConstants::RoundStartDistance == 175.0);
    Check("ステージ幅は 850、キャラ間の上限 304 は画面幅 384 より狭い",
          StageConstants::StageWidth == 850.0 &&
          StageConstants::MaxPlayerDistance == 304.0 &&
          StageConstants::MaxPlayerDistance < GameSpec::BaseWidth);
    // 上限は「画面幅 － 絵 1 体ぶん」で決めています。2 人が上限まで
    // 離れて、カメラを中間に置いたとき、左右の絵の端が画面の端に
    // ちょうど重なる、という関係です（platform/Camera.cpp が使います）。
    Check("キャラ間の上限＋絵 1 体ぶん＝画面幅（端がぴったり合う）",
          StageConstants::MaxPlayerDistance +
              StageConstants::PlayerScreenMargin * 2 == GameSpec::BaseWidth &&
          StageConstants::PlayerScreenMargin * 2 == GameSpec::CharacterSpriteWidth);
    Check("地面は Y=200、キャラの身長 95 は画面高の 40〜45%",
          GameSpec::GroundY == 200 &&
          GameSpec::CharacterVisualHeight == 95 &&
          StageConstants::PlayerHeightRatio > 0.40 &&
          StageConstants::PlayerHeightRatio < 0.45);

    {
        Fighter f;
        f.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        f.Opponent = &f;
        f.PositionX = 0; f.PositionY = 0; f.Facing = Constants::FacingRight;

        RectBox stand = f.PushboxRect();
        Check("立ちの押し合い判定は 32x78 で足元に接地",
              stand.Width == 32 && stand.Height == 78 && stand.Left() == -16 &&
              stand.Right() == 16 && stand.Top() == -78 && stand.Bottom() == 0);

        f.SM.ChangeState(CharState::Crouch, "");
        RectBox crouch = f.PushboxRect();
        Check("しゃがみの押し合い判定は 35x52",
              crouch.Width == 35 && crouch.Height == 52 && crouch.Bottom() == 0 &&
              crouch.Left() == -17.5);

        f.SM.ChangeState(CharState::Jump, "");
        f.PositionY = -40;
        RectBox air = f.PushboxRect();
        Check("空中の押し合い判定は 30x56 で胴体の位置",
              air.Width == 30 && air.Height == 56 &&
              air.CenterY == -40 + f.Stats.Pushboxes.Air.CenterY);
        f.SM.ChangeState(CharState::Idle, "");
        f.PositionY = 0;

        std::vector<RectBox> hurt = f.HurtboxRects();
        double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
        for (const auto& r : hurt) {
            left = std::min(left, r.Left()); right = std::max(right, r.Right());
            top = std::min(top, r.Top()); bottom = std::max(bottom, r.Bottom());
        }
        Check("立ちの食らい判定は 3 部位（頭・胴・脚）", hurt.size() == 3);
        Check("立ちの食らい判定の外形は 32 幅 x 95 高（身長ぴったり）、足元が 0",
              (right - left) == 32 && (bottom - top) == GameSpec::CharacterVisualHeight &&
              bottom == 0);
        Check("食らい判定の幅は見た目の幅（52）より細い", (right - left) < GameSpec::CharacterVisualWidth);

        f.SM.ChangeState(CharState::Crouch, "");
        std::vector<RectBox> churt = f.HurtboxRects();
        double ctop = 1e9;
        for (const auto& r : churt) ctop = std::min(ctop, r.Top());
        Check("しゃがみの食らい判定は立ちより十分低い（62 ＜ 95）",
              churt.size() == 3 && -ctop == 62 && -ctop < GameSpec::CharacterVisualHeight);
        f.SM.ChangeState(CharState::Idle, "");

        f.PositionX = 10.4;
        Check("小数の座標でも判定は整数に丸められる",
              f.PushboxRect().CenterX == 10.0 && f.HurtboxRects()[0].CenterX == 10.0);
    }

    // =================================================================
    std::cout << "\n=== 技のフレームデータと攻撃判定 ===\n";
    // =================================================================
    {
        const MoveData* lp = dm.GetMove("ryu", "standing_light");
        const MoveData* hp = dm.GetMove("ryu", "standing_heavy");
        const MoveData* lk = dm.GetMove("ryu", "standing_light_kick");
        const MoveData* hk = dm.GetMove("ryu", "standing_heavy_kick");
        const MoveData* clk = dm.GetMove("ryu", "crouch_light");
        // 全体フレームは (発生 - 1) + 持続 + 硬直。
        //   弱P 3+3+7=13 / 強P 6+3+13=22 / 弱K 4+3+8=15 / 強K 8+4+16=28
        Check("フレームデータ: 弱P 4/3/7、強P 7/3/13、弱K 5/3/8、強K 9/4/16",
              lp->Startup == 4 && lp->Active == 3 && lp->Recovery == 7 && lp->TotalFrames() == 13 &&
              hp->Startup == 7 && hp->Active == 3 && hp->Recovery == 13 && hp->TotalFrames() == 22 &&
              lk->Startup == 5 && lk->Active == 3 && lk->Recovery == 8 && lk->TotalFrames() == 15 &&
              hk->Startup == 9 && hk->Active == 4 && hk->Recovery == 16 && hk->TotalFrames() == 28);

        // 右向きで X=100 から出したときの弱パンチの判定位置
        auto rightBoxes = MoveExecutor::GetActiveHitboxRects(*lp, lp->Startup, Constants::FacingRight, 100, 0);
        auto leftBoxes = MoveExecutor::GetActiveHitboxRects(*lp, lp->Startup, Constants::FacingLeft, 100, 0);
        // 具体的な数値はデータ側（JSON）で調整するものなので、
        // ここでは「前方の、胸の高さに、体より小さく出る」ことを確認します。
        Check("弱パンチの判定は前方の胸の高さに出る",
              rightBoxes.size() == 1 &&
              rightBoxes[0].Left() > 100 &&                       // 体より前
              rightBoxes[0].Right() < 100 + 45 &&                 // 弱なので短い
              rightBoxes[0].Top() < -55 && rightBoxes[0].Bottom() > -80 && // 胸の高さ
              rightBoxes[0].Width < 25 && rightBoxes[0].Height < 15);
        Check("左向きでは同じデータが左右反転される",
              leftBoxes.size() == 1 &&
              std::abs((100 - leftBoxes[0].CenterX) - (rightBoxes[0].CenterX - 100)) < 0.001 &&
              leftBoxes[0].Width == rightBoxes[0].Width);
        Check("発生フレーム中には攻撃判定が存在しない",
              MoveExecutor::GetActiveHitboxRects(*lp, 1, Constants::FacingRight, 100, 0).empty() &&
              MoveExecutor::GetActiveHitboxRects(*lp, lp->Startup - 1, Constants::FacingRight, 100, 0)
                  .empty());

        auto hkBoxes = MoveExecutor::GetActiveHitboxRects(*hk, hk->Startup, Constants::FacingRight, 0, 0);
        Check("強キックは弱パンチより遠くまで届く",
              hkBoxes.size() == 1 && hkBoxes[0].Right() > 45 &&
              hkBoxes[0].Right() > (rightBoxes[0].Right() - 100));
        auto clkBoxes = MoveExecutor::GetActiveHitboxRects(*clk, clk->Startup, Constants::FacingRight, 0, 0);
        Check("しゃがみ弱キックは足元付近の高さに出る（下段）",
              clkBoxes.size() == 1 && clkBoxes[0].Bottom() > -10 && clkBoxes[0].Top() > -20);

        // フレーム単位の上書き指定が効くか
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
        Check("上書き指定は発生フレーム中でも効く",
              MoveExecutor::HasLiveHitboxes(custom, 0) && ovr.size() == 1 && ovr[0].Width == 6 &&
              !MoveExecutor::HasLiveHitboxes(custom, 2));
        Check("上書きの無いフレームは通常の持続判定に戻る",
              normal.size() == 1 && normal[0].Width == 8);

        Fighter f2;
        f2.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        f2.Opponent = &f2;
        f2.CurrentMoveData = &custom;
        f2.SM.ChangeState(CharState::Attack, "custom");
        Check("食らい判定・押し合い判定の上書きも効く",
              f2.HurtboxRects().size() == 1 && f2.HurtboxRects()[0].Height == 80 &&
              f2.PushboxRect().Width == 10);
    }

    // =================================================================
    std::cout << "\n=== 技ごとの食らい判定 ===\n";
    // =================================================================
    // 技を出している間だけ、その技専用の食らい判定に差し替わることを
    // 確かめます。優先順位は
    //   frameBoxes（フレーム単位）＞ 技ごと ＞ 姿勢ごと
    // です。技が終われば姿勢ごとの標準に戻らなければいけません。
    {
        // 前へ長く伸ばした脚を表す、技ごとの食らい判定。
        MoveData kick;
        kick.Id = "test_kick";
        kick.Startup = 4; kick.Active = 3; kick.Recovery = 8;
        kick.Hitboxes.push_back({40, -14, 24, 10});
        kick.HurtboxOverrideEnabled = true;
        kick.Hurtboxes.push_back({"torso", RectBox{0, -50, 24, 40}});
        kick.Hurtboxes.push_back({"leg", RectBox{22, -12, 60, 20}}); // 前へ 52 まで伸びる

        Fighter f;
        f.Setup(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"));
        f.Opponent = &f;
        f.PositionX = 0;

        std::vector<RectBox> standing = f.HurtboxRects();
        double standRight = -1e9;
        for (const auto& r : standing) standRight = std::max(standRight, r.Right());

        f.CurrentMoveData = &kick;
        f.SM.ChangeState(CharState::Attack, kick.Id);
        std::vector<RectBox> during = f.HurtboxRects();
        double kickRight = -1e9;
        for (const auto& r : during) kickRight = std::max(kickRight, r.Right());

        Check("技を出すと、その技の食らい判定に差し替わる",
              during.size() == 2 && kickRight > standRight && kickRight == 52);

        // 左を向けば、攻撃判定と同じように左右反転する。
        f.Facing = Constants::FacingLeft;
        std::vector<RectBox> flipped = f.HurtboxRects();
        double kickLeft = 1e9;
        for (const auto& r : flipped) kickLeft = std::min(kickLeft, r.Left());
        Check("技ごとの食らい判定も左右反転する", kickLeft == -52);
        f.Facing = Constants::FacingRight;

        // 使わない設定にすれば、中身を残したまま姿勢どおりに戻る。
        kick.HurtboxOverrideEnabled = false;
        Check("使わない設定なら、中身を残したまま姿勢どおりに戻る",
              !kick.Hurtboxes.empty() && f.HurtboxRects().size() == standing.size());
        kick.HurtboxOverrideEnabled = true;

        // frameBoxes のほうが強い（同じフレームなら frameBoxes が勝つ）。
        FrameBoxSet fb;
        fb.startFrame = 0; fb.endFrame = 99; // 今のフレーム（0）を含める
        fb.hasHurtboxes = true;
        fb.hurtboxes.push_back({"body", RectBox{0, -30, 10, 60}});
        kick.FrameBoxes.push_back(fb);
        Check("frameBoxes の指定は技ごとの指定より優先される",
              f.HurtboxRects().size() == 1 && f.HurtboxRects()[0].Width == 10);
        kick.FrameBoxes.clear();

        // 技が終われば姿勢ごとの標準へ戻る。
        f.SM.ChangeState(CharState::Idle, "");
        Check("技が終われば姿勢ごとの食らい判定に戻る",
              f.HurtboxRects().size() == standing.size());

        // JSON への書き出しと読み込みで、内容が保たれること。
        MoveData reloaded = MoveData::FromJson(kick.ToJson());
        Check("技ごとの食らい判定は保存・読み込みで保たれる",
              reloaded.HasHurtboxOverride() && reloaded.Hurtboxes.size() == 2 &&
              reloaded.Hurtboxes[1].Name == "leg" &&
              reloaded.Hurtboxes[1].Box.Width == 60);

        // 「使わない」で保存した場合も、中身は残り、設定だけが伝わること。
        kick.HurtboxOverrideEnabled = false;
        MoveData reloadedOff = MoveData::FromJson(kick.ToJson());
        Check("使わない設定も保存・読み込みで保たれる",
              !reloadedOff.HasHurtboxOverride() && reloadedOff.Hurtboxes.size() == 2);

        // 昔ながらの「配列だけ」の書き方でも読めること。
        Json legacy = Json::MakeObject();
        legacy.Set("id", Json(std::string("legacy")));
        Json parts = Json::MakeArray();
        Json one = Json::MakeObject();
        one.Set("part", Json(std::string("torso")));
        one.Set("offsetX", Json(0.0));
        one.Set("offsetY", Json(-40.0));
        one.Set("width", Json(20.0));
        one.Set("height", Json(80.0));
        parts.Push(std::move(one));
        legacy.Set("hurtboxes", std::move(parts));
        MoveData legacyMove = MoveData::FromJson(legacy);
        Check("配列だけの書き方（\"hurtboxes\": [...]）も使える",
              legacyMove.HasHurtboxOverride() && legacyMove.Hurtboxes.size() == 1 &&
              legacyMove.Hurtboxes[0].Box.Height == 80);

        // 実際に試合の中で、伸ばした脚を狙って当てられること。
        // 技を出していないときは届かない距離に相手を置き、技を出した
        // 瞬間だけ当たるようになることを確かめます。
        BattleSystem bs;
        bs.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"),
                      *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        Fighter& atk = bs.Player1;
        Fighter& def = bs.Player2;
        atk.PositionX = 0;
        // 立ち姿勢の食らい判定は体の中心から 15 しか広がらないので、
        // 攻撃判定（28〜52）は届きません。技で脚を 52 まで伸ばして
        // はじめて届く距離に置きます。
        def.PositionX = 70;
        // 攻撃側は「相手の脚に届く」判定を持つ技を出す。
        MoveData poke;
        poke.Id = "test_poke";
        poke.Startup = 1; poke.Active = 3; poke.Recovery = 5;
        poke.Damage = 100;
        poke.Hitboxes.push_back({40, -12, 24, 12}); // 28〜52 の範囲を叩く
        atk.CurrentMoveData = &poke;
        atk.SM.ChangeState(CharState::Attack, poke.Id);
        atk.SM.CurrentFrame = 1;
        atk.ActiveHitboxRects = MoveExecutor::GetActiveHitboxRects(
            poke, 1, atk.Facing, atk.PositionX, atk.PositionY);

        int hpBefore = def.CurrentHP;
        bs.ResolveCombat(atk, def);            // 相手は立ち姿勢 → 届かない
        bool missedWhileStanding = (def.CurrentHP == hpBefore);

        kick.HurtboxOverrideEnabled = true;
        def.CurrentMoveData = &kick;           // 相手が脚を伸ばす技を出した
        def.SM.ChangeState(CharState::Attack, kick.Id);
        atk.AlreadyHit.clear();
        bs.ResolveCombat(atk, def);            // 伸びた脚に当たる
        Check("伸ばした脚（技ごとの食らい判定）を狙って潰せる",
              missedWhileStanding && def.CurrentHP < hpBefore);

        // 付属データの例（しゃがみ強キック＝足払い）。
        // 脚を前へ伸ばすので、姿勢どおりの判定より前に出ているはずです。
        const MoveData* sweep = dm.GetMove("ryu", "crouch_heavy");
        const CharacterStats* ryu = dm.GetCharacter("ryu");
        double sweepFront = -1e9, crouchFront = -1e9;
        if (sweep != nullptr) {
            for (const auto& p : sweep->Hurtboxes) sweepFront = std::max(sweepFront, p.Box.Right());
        }
        for (const auto& p : ryu->Hurtboxes.Crouch) crouchFront = std::max(crouchFront, p.Box.Right());
        Check("付属データ: しゃがみ強キックは食らい判定も前へ伸びている",
              sweep != nullptr && sweep->HasHurtboxOverride() && sweepFront > crouchFront);
        // 攻撃判定より食らい判定が前に出ていたら、技として成立しません
        //（自分から相手の拳に脚を差し出していることになるため）。
        double sweepHit = -1e9;
        if (sweep != nullptr) {
            for (const auto& hb : sweep->Hitboxes) {
                sweepHit = std::max(sweepHit, hb.offsetX + hb.width / 2.0);
            }
        }
        Check("付属データ: 攻撃判定のほうが食らい判定より前に出ている",
              sweep != nullptr && sweepHit > sweepFront);
    }

    // =================================================================
    std::cout << "\n=== 投げの距離判定 ===\n";
    // =================================================================
    {
        BattleSystem bsThrow;
        bsThrow.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"),
                           *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        const MoveData* throwMove = bsThrow.Player1.GetMove("standing_throw");
        auto armThrow = [&](double defX, double defY) {
            bsThrow.ResetHP();
            // 前回の試行の状態が残らないように、攻撃側を完全に戻します。
            // ChangeState は同じ状態への切り替えを無視するので、
            // 明示的に Idle を挟まないとフレーム数が引き継がれてしまいます。
            bsThrow.Player1.SM.ChangeState(CharState::Idle, "");
            bsThrow.Player1.CurrentMoveData = nullptr;
            bsThrow.Player1.ActiveHitboxRects.clear();
            bsThrow.Player1.PositionX = 0; bsThrow.Player1.PositionY = 0;
            bsThrow.Player1.Facing = Constants::FacingRight;
            bsThrow.Player2.PositionX = defX; bsThrow.Player2.PositionY = defY;
            bsThrow.Player2.Facing = Constants::FacingLeft;
            bsThrow.Player2.SM.ChangeState(defY < 0 ? CharState::Jump : CharState::Idle, "");
            bsThrow.Player1.StartMove(*throwMove);
            for (int i = 0; i < throwMove->Startup; i++) bsThrow.Player1.SM.Tick();
            bsThrow.Player1.ProgressMove();
            bsThrow.ResolveCombat(bsThrow.Player1, bsThrow.Player2);
            return bsThrow.Player2.SM.CurrentState == CharState::Throw;
        };
        Check("距離 20（間合い 30 以内）なら投げが成立する", armThrow(20, 0));
        Check("距離 40 では投げが空振りする", !armThrow(40, 0));
        Check("空中の相手は投げられない", !armThrow(20, -30));
        Check("投げは距離判定であって当たり判定ではない",
              throwMove->ThrowRange == static_cast<double>(GameSpec::NormalThrowRange) &&
              throwMove->GuardType == Constants::GuardThrow);
    }

    // =================================================================
    std::cout << "\n=== 押し合いの解決 ===\n";
    // =================================================================
    {
        BattleSystem bsPush;
        bsPush.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"),
                          *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);
        // 幅 32 の判定どうしが 20 の距離 -> 12 だけ重なる
        bsPush.Player1.PositionX = 0; bsPush.Player2.PositionX = 20;
        bsPush.ResolvePushboxes();
        Check("重なり 12 は半分ずつ（-6 / +6）に分けられる",
              bsPush.Player1.PositionX == -6 && bsPush.Player2.PositionX == 26);

        // 画面端に張り付いている側は動かず、相手が全部下がる。
        // 「張り付いた位置」は判定の半分の幅ぶん内側（体の表面が壁）です。
        const double wallX = BattleSystem::StageMinX + GameSpec::PushboxStandWidth / 2.0;
        bsPush.Player1.PositionX = wallX;
        bsPush.Player2.PositionX = wallX + 20;
        bsPush.ResolvePushboxes();
        Check("画面端側は動かず、相手が重なりを全部引き受ける",
              bsPush.Player1.PositionX == wallX &&
              bsPush.Player2.PositionX == wallX + 32);
    }

    // =================================================================
    std::cout << "\n=== 画面端とキャラ間距離の制限 ===\n";
    // =================================================================
    {
        BattleSystem bs;
        bs.StartMatch(*dm.GetCharacter("ryu"), dm.GetMoveset("ryu"),
                      *dm.GetCharacter("ryu"), dm.GetMoveset("ryu"), 99);

        // 壁にめり込ませようとしても、体（押し合い判定）の分だけ内側で止まる
        bs.Player1.PositionX = BattleSystem::StageMinX - 100;
        bs.Player1.ClampToStage();
        RectBox box = bs.Player1.PushboxRect();
        Check("左端: 判定がステージの外へはみ出さない",
              box.Left() >= BattleSystem::StageMinX - 0.001);
        Check("左端: 壁にぴったり接する（隙間なく詰められる）",
              std::abs(box.Left() - BattleSystem::StageMinX) < 0.001);

        bs.Player1.PositionX = BattleSystem::StageMaxX + 100;
        bs.Player1.ClampToStage();
        box = bs.Player1.PushboxRect();
        Check("右端: 判定がステージの外へはみ出さない",
              box.Right() <= BattleSystem::StageMaxX + 0.001);

        // しゃがみは判定が広いので、立ちより手前で止まる
        bs.Player1.PositionX = BattleSystem::StageMinX - 100;
        bs.Player1.SM.ChangeState(CharState::Crouch, "");
        bs.Player1.ClampToStage();
        RectBox crouchBox = bs.Player1.PushboxRect();
        Check("しゃがみ（判定が広い）でも外へはみ出さない",
              crouchBox.Left() >= BattleSystem::StageMinX - 0.001);
        bs.Player1.SM.ChangeState(CharState::Idle, "");

        // 離れすぎの制限
        bs.Player1.PositionX = -400;
        bs.Player2.PositionX = 400;
        bs.ResolveMaxDistance();
        double dist = std::abs(bs.Player2.PositionX - bs.Player1.PositionX);
        Check("2 人の距離は上限 304 まで詰められる",
              std::abs(dist - StageConstants::MaxPlayerDistance) < 0.001);
        Check("上限 304 は画面幅 384 より狭いので必ず両方映る",
              StageConstants::MaxPlayerDistance < GameSpec::BaseWidth);

        // 追い詰められた側が、相手が逃げただけで角から引き出されないこと。
        // （これが起きると画面端に押し込む攻防が成立しなくなります）
        bs.Player1.PositionX = std::ceil(BattleSystem::StageMinX + 16); // 左端に張り付き
        bs.Player1.VelocityX = 0.0;                                     // 動いていない
        bs.Player2.PositionX = bs.Player1.PositionX + 380;
        bs.Player2.VelocityX = 60.0;                                    // 右へ逃げている
        double p1Before = bs.Player1.PositionX;
        bs.ResolveMaxDistance();
        Check("逃げている側だけが止まり、追い詰められた側は動かない",
              std::abs(bs.Player1.PositionX - p1Before) < 0.001 &&
              std::abs(bs.Player2.PositionX - bs.Player1.PositionX
                       - StageConstants::MaxPlayerDistance) < 0.001);

        // どちらも逃げていなければ半分ずつ
        bs.Player1.PositionX = -162; bs.Player1.VelocityX = 0.0;
        bs.Player2.PositionX = 162;  bs.Player2.VelocityX = 0.0;
        bs.ResolveMaxDistance();
        Check("どちらも逃げていなければ半分ずつ詰める",
              std::abs(bs.Player1.PositionX - (-152)) < 0.001 &&
              std::abs(bs.Player2.PositionX - 152) < 0.001);

        // 上限内なら何もしない
        bs.Player1.PositionX = 0; bs.Player2.PositionX = 100;
        bs.ResolveMaxDistance();
        Check("上限より近ければ位置を動かさない",
              bs.Player1.PositionX == 0 && bs.Player2.PositionX == 100);

        // ---- KO された体が滑り続けないこと ----
        // 以前は、死亡後に吹き飛び速度を 0 に近づける処理も、
        // ステージ端で止める処理も通らなかったため、死体が同じ速さで
        // 滑り続け、やがて画面の外へ出ていっていました
        //（トレーニングモードのように KO で試合が終わらない場面）。
        bs.Player1.PositionX = 0.0;
        bs.Player1.PositionY = 0.0;
        bs.Player1.VelocityX = -300.0; // 左へ強く吹き飛んだところで KO
        bs.Player1.IsDead = true;
        bs.Player1.SM.ChangeState(CharState::Dead, "");
        RawInput none;
        for (int i = 0; i < 60; ++i) bs.Player1.FrameStep(1.0 / 60.0, none);
        Check("KO された体は滑って止まる（速度が 0 に戻る）",
              std::abs(bs.Player1.VelocityX) < 0.001);

        // 端の外へ吹き飛ばされても、壁の内側で止まる。
        bs.Player1.PositionX = BattleSystem::StageMinX + 40;
        bs.Player1.VelocityX = -900.0;
        for (int i = 0; i < 120; ++i) bs.Player1.FrameStep(1.0 / 60.0, none);
        Check("KO された体はステージの外へ出ない",
              bs.Player1.PushboxRect().Left() >= BattleSystem::StageMinX - 0.001);
        bs.Player1.IsDead = false;
        bs.Player1.SM.ChangeState(CharState::Idle, "");
    }

    // =================================================================
    std::cout << "\n=== 画面揺れ（技の強さで段階的に）===\n";
    // =================================================================
    {
        auto shakeOf = [](const MoveData* m) {
            double mag = -1; int fr = -1; double v = 0;
            BattleSystem::ShakeForMove(*m, mag, fr, v);
            return std::pair<double,int>(mag, fr);
        };
        auto light = shakeOf(dm.GetMove("ryu", "standing_light"));
        auto medium = shakeOf(dm.GetMove("ryu", "standing_medium"));
        auto heavy = shakeOf(dm.GetMove("ryu", "standing_heavy"));
        auto special = shakeOf(dm.GetMove("ryu", "fireball"));
        auto super_ = shakeOf(dm.GetMove("ryu", "super_combo"));

        Check("弱・中の通常技では画面を揺らさない",
              light.first == 0.0 && medium.first == 0.0);
        Check("強 1〜2px / 必殺 2〜3px / 超必 3〜5px",
              heavy.first >= 1.0 && heavy.first <= 2.0 &&
              special.first >= 2.0 && special.first <= 3.0 &&
              super_.first >= 3.0 && super_.first <= 5.0);
        Check("揺れの長さは 2〜6 フレーム",
              heavy.second >= 2 && heavy.second <= 6 &&
              special.second >= 2 && special.second <= 6 &&
              super_.second >= 2 && super_.second <= 6);

        BattleSystem bs;
        bs.AddShake(2.0, 4, 1.0);
        bs.AddShake(1.0, 3, -1.0); // 弱い揺れは強い揺れを打ち消さない
        Check("弱い揺れが強い揺れを上書きしない",
              bs.ShakeMagnitude == 2.0 && bs.ShakeDirX == 1.0);
        bs.AddShake(9.0, 4, 1.0);
        Check("揺れ幅は 5px を超えない", bs.ShakeMagnitude == 5.0);
    }

    // =================================================================
    std::cout << "\n=== フレームの数え方（発生4F = 4F目に判定）===\n";
    // =================================================================
    // 仕様:
    //   発生 4F / 持続 3F / 硬直 7F の技は
    //     1-3F   発生前
    //     4-6F   攻撃判定
    //     7-13F  硬直
    //     14F    行動可能
    //   全体フレーム = (発生 - 1) + 持続 + 硬直 = 13F
    {
        MoveData m;
        m.Startup = 4; m.Active = 3; m.Recovery = 7;
        m.Hitboxes.push_back({20, -60, 16, 10});

        bool phasesOk = true;
        for (int f = 1; f <= 3; ++f) {
            if (MoveExecutor::GetPhase(m, f) != MovePhase::Startup) phasesOk = false;
        }
        for (int f = 4; f <= 6; ++f) {
            if (MoveExecutor::GetPhase(m, f) != MovePhase::Active) phasesOk = false;
        }
        for (int f = 7; f <= 13; ++f) {
            if (MoveExecutor::GetPhase(m, f) != MovePhase::Recovery) phasesOk = false;
        }
        Check("1-3F 発生前 / 4-6F 持続 / 7-13F 硬直", phasesOk);
        Check("14F 目には技が終わっている（行動可能）",
              MoveExecutor::GetPhase(m, 14) == MovePhase::Done && m.ActionableFrame() == 14);
        Check("全体フレームは (発生-1)+持続+硬直 = 13F", m.TotalFrames() == 13);

        bool hitboxOk = true;
        for (int f = 1; f <= 14; ++f) {
            bool live = MoveExecutor::HasLiveHitboxes(m, f);
            bool expected = (f >= 4 && f <= 6);
            if (live != expected) hitboxOk = false;
        }
        Check("攻撃判定が出るのは 4・5・6F 目だけ", hitboxOk);

        MoveData fast;
        fast.Startup = 1; fast.Active = 1; fast.Recovery = 5;
        fast.Hitboxes.push_back({20, -60, 16, 10});
        Check("発生 1F の技は 1F 目にもう判定が出る",
              MoveExecutor::HasLiveHitboxes(fast, 1) && fast.TotalFrames() == 6 &&
              fast.ActionableFrame() == 7);
    }

    // =================================================================
    std::cout << "\n=== 硬直差は入力値、のけぞりはそこから決まる ===\n";
    // =================================================================
    {
        // 仕様書の例: 立ち弱P 4/3/7、ヒット +4、ガード -1
        const MoveData* lp = dm.GetMove("ryu", "standing_light");
        Check("立ち弱P は 発生4/持続3/硬直7、硬直差 +4 / -1",
              lp->Startup == 4 && lp->Active == 3 && lp->Recovery == 7 &&
              lp->HitAdvantage == 4 && lp->BlockAdvantage == -1);
        // 仕様書の例: 立ち中P 6/3/10、ヒット +5、ガード -2
        const MoveData* mp = dm.GetMove("ryu", "standing_medium");
        Check("立ち中P は 発生6/持続3/硬直10、硬直差 +5 / -2",
              mp->Startup == 6 && mp->Active == 3 && mp->Recovery == 10 &&
              mp->HitAdvantage == 5 && mp->BlockAdvantage == -2);

        // のけぞり = 硬直差 + (持続 + 硬直)
        Check("のけぞりは硬直差から決まる（弱P ヒット 14F / ガード 9F）",
              lp->HitstunFrames() == 14 && lp->BlockstunFrames() == 9);
        Check("ヒット時とガード時で別々の値になる",
              lp->HitstunFrames() != lp->BlockstunFrames() &&
              lp->HitAdvantage != lp->BlockAdvantage);

        // 入力した硬直差と、そこから計算した値は必ず一致する（矛盾しない）
        bool consistent = true;
        for (const auto& kv : *dm.GetMoveset("ryu")) {
            const MoveData& m = kv.second;
            int hit = m.HitstunFrames() - m.RemainingFramesOnEarliestHit();
            int blk = m.BlockstunFrames() - m.RemainingFramesOnEarliestHit();
            if (m.HitAdvantage >= 0 && hit != m.HitAdvantage) consistent = false;
            if (m.BlockAdvantage >= -m.RemainingFramesOnEarliestHit() && blk != m.BlockAdvantage) {
                consistent = false;
            }
        }
        Check("全技で「入力した硬直差」と「のけぞりから逆算した硬直差」が一致", consistent);

        // 硬直差を変えると、のけぞりも自動で変わる
        MoveData edited = *lp;
        edited.HitAdvantage = 8;
        Check("硬直差を +8 にするとのけぞりも 18F になる",
              edited.HitstunFrames() == 18 && edited.HitstunFrames() == lp->HitstunFrames() + 4);

        // 保存して読み直しても硬直差が保たれる
        MoveData saved = *lp;
        saved.HitAdvantage = 6;
        saved.BlockAdvantage = -5;
        MoveData reloaded = MoveData::FromJson(saved.ToJson());
        Check("硬直差は JSON に保存・復元される",
              reloaded.HitAdvantage == 6 && reloaded.BlockAdvantage == -5 &&
              reloaded.HitstunFrames() == 6 + saved.Active + saved.Recovery);
    }

    // =================================================================
    std::cout << "\n=== コンボ成立条件と確定反撃 ===\n";
    // =================================================================
    {
        MoveData first;  first.Startup = 5; first.Active = 3; first.Recovery = 10;
        first.HitAdvantage = 5; first.BlockAdvantage = -6;
        MoveData next4;  next4.Startup = 4;
        MoveData next5;  next5.Startup = 5;
        MoveData next6;  next6.Startup = 6;
        Check("ヒット +5 なら 発生 5F 以下の技がつながる",
              first.CombosInto(next4) && first.CombosInto(next5) && !first.CombosInto(next6));
        Check("ガード -6 なら 発生 6F 以下の技で確定反撃",
              first.IsPunishableBy(4) && first.IsPunishableBy(6) && !first.IsPunishableBy(7));

        MoveData safe; safe.Startup = 4; safe.Active = 3; safe.Recovery = 7;
        safe.BlockAdvantage = -2;
        Check("ガード -2 は 発生 4F の技では反撃にならない", !safe.IsPunishableBy(4));
        MoveData plus; plus.BlockAdvantage = 1;
        Check("ガードして有利な技には確定反撃が無い", !plus.IsPunishableBy(1));
    }

    // =================================================================
    std::cout << "\n=== ヒットストップ（技の強さで段階的に）===\n";
    // =================================================================
    {
        auto stop = [&](const char* id) { return dm.GetMove("ryu", id)->Hitstop; };
        Check("弱 2〜4F", stop("standing_light") >= 2 && stop("standing_light") <= 4);
        Check("中 4〜6F", stop("standing_medium") >= 4 && stop("standing_medium") <= 6);
        Check("強 6〜9F", stop("standing_heavy") >= 6 && stop("standing_heavy") <= 9);
        Check("必殺技 5〜10F", stop("fireball") >= 5 && stop("fireball") <= 10);
        Check("強い技ほどヒットストップが長い",
              stop("standing_light") < stop("standing_medium") &&
              stop("standing_medium") < stop("standing_heavy"));
    }


    // =================================================================
    std::cout << "\n=== ストップ（ヒットストップ / ガードストップ）===\n";
    // =================================================================
    // 仕様:
    //   1. 接触した瞬間、攻撃側・防御側の双方に同じ長さのストップが掛かる
    //      （ヒットならヒットストップ、ガードならガードストップ）。
    //   2. ストップ中は座標もアニメーションもタイマーも完全に停止する。
    //      入力（先行入力）だけは受け付ける。
    //   3. ストップが解けた瞬間から、ノックバックと硬直の消費が始まる。
    //   4. ストップの長さは硬直差に影響しない。
    // ここではその 4 つを、実際に試合を回して確かめます。
    {
        // 1 試合を組み立てるための道具。密着させた状態から始めます。
        struct Rig {
            BattleSystem bs;
            void Start(const CharacterStats& cs,
                       const std::unordered_map<std::string, MoveData>* ms,
                       DummyMode dummy) {
                bs.StartMatch(cs, ms, cs, ms, 99);
                bs.TrainingMode = true;          // KO や時間切れで終わらせない
                bs.TrainingAutoHeal = false;     // 体力回復で状態が変わらないように
                bs.CpuAI->Mode = dummy;
                bs.Player1.PositionX = -18;
                bs.Player2.PositionX = 18;
            }
        };

        // 技を 1 回出して、攻撃側・防御側それぞれが「動けるようになる」
        // フレーム番号を測る。硬直差 = 防御側 - 攻撃側。
        struct Measured {
            int contactFrame = -1;      // 当たったフレーム
            int attackerFrameAtContact = -1; // そのときの技の経過フレーム
            int attackerFree = -1;
            int defenderFree = -1;
            bool blocked = false;
            int advantage = 0;
        };
        auto run = [&](const std::string& moveId, DummyMode dummy) {
            const MoveData* mv = dm.GetMove("ryu", moveId);
            const CharacterStats* cs = dm.GetCharacter("ryu");
            Rig rig;
            rig.Start(*cs, dm.GetMoveset("ryu"), dummy);
            Measured m;
            RawInput in;
            if (moveId.rfind("crouch_", 0) == 0) in.Down = true; // しゃがみ技
            in.Buttons.Set(mv->Button, true);
            bool released = false;
            for (int f = 0; f < 300; ++f) {
                rig.bs.Update(1.0 / 60.0, in);
                if (!released) { in.Buttons.Set(mv->Button, false); released = true; }
                bool defenderReacting = rig.bs.Player2.SM.CurrentState == CharState::Hitstun ||
                                        (rig.bs.Player2.SM.CurrentState == CharState::Block &&
                                         rig.bs.Player2.BlockstunTimer > 0);
                if (m.contactFrame < 0 && defenderReacting) {
                    m.contactFrame = f;
                    m.blocked = rig.bs.Player2.BlockstunTimer > 0;
                    m.attackerFrameAtContact = rig.bs.Player1.SM.CurrentFrame;
                }
                if (m.contactFrame >= 0) {
                    // 攻撃側が自由になった＝技が終わった。
                    if (m.attackerFree < 0 && rig.bs.Player1.SM.IsActionable()) m.attackerFree = f;
                    // 防御側が自由になった＝のけぞり／ガード硬直が切れた。
                    //
                    // ここで「状態が Idle になったか」で判定してはいけません。
                    // 後ろを入れっぱなしにしていると、硬直が切れたあとも
                    // ガード姿勢（Block 状態）に留まります。それは本人が
                    // 選んで構えているだけで、硬直しているわけではありません。
                    // 硬直差はあくまで「硬直が切れたフレーム」で測ります。
                    if (m.defenderFree < 0 && rig.bs.Player2.HitstunTimer <= 0 &&
                        rig.bs.Player2.BlockstunTimer <= 0) m.defenderFree = f;
                }
                if (m.attackerFree >= 0 && m.defenderFree >= 0) break;
            }
            m.advantage = m.defenderFree - m.attackerFree;
            return m;
        };

        // ---- ガードストップはヒットストップより短い ----
        {
            bool allShorter = true, allPositive = true;
            for (const char* id : {"standing_light", "standing_medium", "standing_heavy",
                                   "standing_light_kick", "standing_heavy_kick"}) {
                const MoveData* mv = dm.GetMove("ryu", id);
                if (mv->Guardstop >= mv->Hitstop) allShorter = false;
                if (mv->Guardstop < 1) allPositive = false;
            }
            Check("ガードストップはヒットストップより短い", allShorter);
            Check("ガードストップは 1F 以上ある", allPositive);
            Check("ガードストップの既定値は ヒットストップ-2（最低 1）",
                  MoveData::DefaultGuardstop(8) == 6 && MoveData::DefaultGuardstop(2) == 1 &&
                  MoveData::DefaultGuardstop(1) == 1 && MoveData::DefaultGuardstop(0) == 0);
        }

        // ---- 硬直差が定義どおりか（ヒット）----
        {
            bool ok = true;
            std::string detail;
            for (const char* id : {"standing_light", "standing_medium", "standing_light_kick",
                                   "standing_medium_kick"}) {
                const MoveData* mv = dm.GetMove("ryu", id);
                Measured m = run(id, DummyMode::Stand);
                // 定義: 硬直差 = 防御側の硬直F - 攻撃側の残り全体F
                int expected = mv->HitstunFrames() - mv->RemainingFramesAt(m.attackerFrameAtContact);
                if (m.advantage != expected) {
                    ok = false;
                    detail += std::string(" ") + id + "(" + std::to_string(m.advantage) +
                              "!=" + std::to_string(expected) + ")";
                }
            }
            Check("ヒット時の硬直差が定義どおり（実測 = 硬直F - 残り全体F）" + detail, ok);
        }

        // ---- 表に出す値（持続 1F 目で当てた場合）と実測が一致するか ----
        {
            bool ok = true;
            for (const char* id : {"standing_light", "standing_medium", "standing_light_kick",
                                   "standing_medium_kick"}) {
                const MoveData* mv = dm.GetMove("ryu", id);
                Measured m = run(id, DummyMode::Stand);
                // 持続 1F 目で当たっているなら、表の値と一致するはず
                if (m.attackerFrameAtContact == mv->Startup && m.advantage != mv->OnHitAdvantage()) {
                    ok = false;
                }
            }
            Check("エディタに出る有利フレームが実測と一致する", ok);
        }

        // ---- 硬直差（ガード）----
        {
            bool ok = true;
            std::string detail;
            for (const char* id : {"standing_light", "standing_medium", "standing_light_kick"}) {
                const MoveData* mv = dm.GetMove("ryu", id);
                Measured m = run(id, DummyMode::Guard);
                if (!m.blocked) { ok = false; detail += std::string(" ") + id + "(ガードせず)"; continue; }
                int expected = mv->BlockstunFrames() - mv->RemainingFramesAt(m.attackerFrameAtContact);
                if (m.advantage != expected) {
                    ok = false;
                    detail += std::string(" ") + id + "(" + std::to_string(m.advantage) +
                              "!=" + std::to_string(expected) + ")";
                }
            }
            Check("ガード時の硬直差が定義どおり" + detail, ok);
        }

        // ---- ストップの長さを変えても硬直差が変わらない ----
        {
            const CharacterStats* cs = dm.GetCharacter("ryu");
            // 技のコピーを作り、ヒットストップだけ変えて比べます。
            std::unordered_map<std::string, MoveData> ms = *dm.GetMoveset("ryu");
            auto advantageWithStop = [&](int hitstop) {
                ms["standing_light"].Hitstop = hitstop;
                ms["standing_light"].Guardstop = MoveData::DefaultGuardstop(hitstop);
                BattleSystem bs;
                bs.StartMatch(*cs, &ms, *cs, &ms, 99);
                bs.TrainingMode = true; bs.TrainingAutoHeal = false;
                bs.CpuAI->Mode = DummyMode::Stand;
                bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;
                RawInput in;
                in.Buttons.LP = true;
                int atk = -1, def = -1, contact = -1;
                for (int f = 0; f < 300; ++f) {
                    bs.Update(1.0 / 60.0, in);
                    in.Buttons.LP = false;
                    if (contact < 0 && bs.Player2.SM.CurrentState == CharState::Hitstun) contact = f;
                    if (contact >= 0) {
                        if (atk < 0 && bs.Player1.SM.IsActionable()) atk = f;
                        if (def < 0 && bs.Player2.HitstunTimer <= 0) def = f;
                    }
                    if (atk >= 0 && def >= 0) break;
                }
                return def - atk;
            };
            int a0 = advantageWithStop(0);
            int a4 = advantageWithStop(4);
            int a15 = advantageWithStop(15);
            Check("ストップの長さは硬直差に影響しない (" + std::to_string(a0) + "/" +
                      std::to_string(a4) + "/" + std::to_string(a15) + ")",
                  a0 == a4 && a4 == a15);
        }

        // ---- ストップ中は完全停止する ----
        {
            const CharacterStats* cs = dm.GetCharacter("ryu");
            const auto* ms = dm.GetMoveset("ryu");
            BattleSystem bs;
            bs.StartMatch(*cs, ms, *cs, ms, 99);
            bs.TrainingMode = false; // 制限時間が止まることも見たいので通常モード
            bs.CpuAI->Mode = DummyMode::Stand;
            bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;

            RawInput in;
            in.Buttons.LP = true;
            bool moved = false, animAdvanced = false, timerAdvanced = false, clockAdvanced = false;
            int stopFramesSeen = 0;
            double px1 = 0, px2 = 0;
            int frame1 = 0, frame2 = 0, hitstun = 0, roundTime = 0;
            bool prevStopped = false;
            int knockbackStartFrame = -1, stopEndFrame = -1;
            for (int f = 0; f < 120; ++f) {
                bool stoppedNow = bs.IsStopped();
                if (stoppedNow) {
                    // 止まっている間、前フレームからの変化を調べる
                    if (prevStopped) {
                        stopFramesSeen++;
                        if (bs.Player1.PositionX != px1 || bs.Player2.PositionX != px2) moved = true;
                        if (bs.Player1.SM.CurrentFrame != frame1 ||
                            bs.Player2.SM.CurrentFrame != frame2) animAdvanced = true;
                        if (bs.Player2.HitstunTimer != hitstun) timerAdvanced = true;
                        if (bs.FramesLeft != roundTime) clockAdvanced = true;
                    }
                    px1 = bs.Player1.PositionX; px2 = bs.Player2.PositionX;
                    frame1 = bs.Player1.SM.CurrentFrame; frame2 = bs.Player2.SM.CurrentFrame;
                    hitstun = bs.Player2.HitstunTimer;
                    roundTime = bs.FramesLeft;
                } else if (prevStopped) {
                    stopEndFrame = f;               // ストップが解けた最初のフレーム
                }
                prevStopped = stoppedNow;

                double before = bs.Player2.PositionX;
                bs.Update(1.0 / 60.0, in);
                in.Buttons.LP = false;
                if (knockbackStartFrame < 0 && bs.Player2.PositionX != before &&
                    bs.Player2.SM.CurrentState == CharState::Hitstun) {
                    knockbackStartFrame = f;
                }
            }
            Check("ストップが実際に発生している (" + std::to_string(stopFramesSeen) + "F)",
                  stopFramesSeen > 0);
            Check("ストップ中は座標が動かない", !moved);
            Check("ストップ中はアニメーションのコマが進まない", !animAdvanced);
            Check("ストップ中はのけぞりタイマーが減らない", !timerAdvanced);
            Check("ストップ中は試合の制限時間も止まる", !clockAdvanced);
            Check("ノックバックはストップが解けたフレームから始まる (" +
                      std::to_string(knockbackStartFrame) + " == " +
                      std::to_string(stopEndFrame) + ")",
                  knockbackStartFrame >= 0 && knockbackStartFrame == stopEndFrame);
        }

        // ---- ストップ中の先行入力 ----
        {
            const CharacterStats* cs = dm.GetCharacter("ryu");
            const auto* ms = dm.GetMoveset("ryu");
            BattleSystem bs;
            bs.StartMatch(*cs, ms, *cs, ms, 99);
            bs.TrainingMode = true; bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Stand;
            bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;

            RawInput in;
            in.Buttons.LP = true;
            bool pressedDuringStop = false;
            std::string moveAfterStop;
            for (int f = 0; f < 60; ++f) {
                bs.Update(1.0 / 60.0, in);
                in.Buttons.LP = false;
                in.Buttons.MP = false;
                if (bs.IsStopped() && !pressedDuringStop) {
                    // 止まっている最中に中パンチを 1 フレームだけ押す
                    in.Buttons.MP = true;
                    pressedDuringStop = true;
                } else if (pressedDuringStop && !bs.IsStopped()) {
                    if (bs.Player1.SM.CurrentMove == "standing_medium") {
                        moveAfterStop = bs.Player1.SM.CurrentMove;
                        break;
                    }
                }
            }
            Check("ストップ中に押したボタンが、解けた直後に技として出る",
                  moveAfterStop == "standing_medium");
        }
    }


    // =================================================================
    std::cout << "\n=== 実測: 入力した硬直差どおりに動くか ===\n";
    // =================================================================
    // 「硬直差 +4」と入力した技は、本当に攻撃側が 4 フレーム早く
    // 動けるようになるのか。実際に試合を回して数えます。
    {
        // ノックバックがあると 2 発目の間合いが変わってしまうので、
        // 測定用に「押し戻さない」技のコピーを作ります。
        std::unordered_map<std::string, MoveData> ms = *dm.GetMoveset("ryu");
        for (auto& kv : ms) { kv.second.KnockbackX = 0.0; kv.second.KnockbackY = 0.0; }
        const CharacterStats* cs = dm.GetCharacter("ryu");

        // 技を 1 回出して、攻撃側・防御側が「動けるようになる」フレームを
        // それぞれ数え、その差（＝硬直差）を返します。
        struct Result { int advantage = 0; int contactMoveFrame = -1; bool blocked = false;
                        bool connected = false; };
        auto measure = [&](const std::string& moveId, DummyMode dummy) {
            const MoveData& mv = ms.at(moveId);
            BattleSystem bs;
            bs.StartMatch(*cs, &ms, *cs, &ms, 99);
            bs.TrainingMode = true;
            bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = dummy;
            bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;

            RawInput in;
            if (moveId.rfind("crouch_", 0) == 0) in.Down = true;
            in.Buttons.Set(mv.Button, true);

            Result r;
            int attackerFree = -1, defenderFree = -1, contactFrame = -1;
            for (int f = 0; f < 400; ++f) {
                bs.Update(1.0 / 60.0, in);
                in.Buttons.Set(mv.Button, false); // 1 フレームだけ押す
                bool stunned = bs.Player2.HitstunTimer > 0 || bs.Player2.BlockstunTimer > 0;
                if (contactFrame < 0 && stunned) {
                    contactFrame = f;
                    r.connected = true;
                    r.blocked = bs.Player2.BlockstunTimer > 0;
                    // 当たった瞬間の、攻撃側の技の経過フレーム
                    r.contactMoveFrame = bs.Player1.SM.CurrentFrame;
                }
                if (contactFrame >= 0) {
                    if (attackerFree < 0 && bs.Player1.SM.IsActionable()) attackerFree = f;
                    if (defenderFree < 0 && bs.Player2.SM.IsActionable()) defenderFree = f;
                }
                if (attackerFree >= 0 && defenderFree >= 0) break;
            }
            r.advantage = defenderFree - attackerFree;
            return r;
        };

        const char* groundNormals[] = {"standing_light", "standing_medium", "standing_light_kick",
                                       "standing_medium_kick", "crouch_light", "crouch_medium"};
        {
            bool ok = true;
            std::string detail;
            for (const char* id : groundNormals) {
                Result r = measure(id, DummyMode::Stand);
                const MoveData& mv = ms.at(id);
                if (!r.connected || r.contactMoveFrame != mv.Startup) {
                    ok = false; detail += std::string(" ") + id + "(当たらず)"; continue;
                }
                if (r.advantage != mv.HitAdvantage) {
                    ok = false;
                    detail += std::string(" ") + id + "(" + std::to_string(r.advantage) +
                              "!=" + std::to_string(mv.HitAdvantage) + ")";
                }
            }
            Check("ヒット時の硬直差が入力値と一致する（実測）" + detail, ok);
        }
        {
            bool ok = true;
            std::string detail;
            for (const char* id : groundNormals) {
                Result r = measure(id, DummyMode::Guard);
                const MoveData& mv = ms.at(id);
                if (!r.connected || !r.blocked) {
                    ok = false; detail += std::string(" ") + id + "(ガードせず)"; continue;
                }
                if (r.advantage != mv.BlockAdvantage) {
                    ok = false;
                    detail += std::string(" ") + id + "(" + std::to_string(r.advantage) +
                              "!=" + std::to_string(mv.BlockAdvantage) + ")";
                }
            }
            Check("ガード時の硬直差が入力値と一致する（実測）" + detail, ok);
        }

        // ---- コンボ成立条件（前の技のヒット時硬直差 >= 次の技の発生）----
        //
        // 立ち弱P は +4。発生 4F の技（立ち弱P）ならつながり、
        // しゃがみ弱K（+3）から発生 4F の技はつながりません。
        auto comboCount = [&](const std::string& firstId, const std::string& secondId) {
            const MoveData& first = ms.at(firstId);
            const MoveData& second = ms.at(secondId);
            BattleSystem bs;
            bs.StartMatch(*cs, &ms, *cs, &ms, 99);
            bs.TrainingMode = true;
            bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Stand;
            bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;

            RawInput in;
            bool firstCrouch = firstId.rfind("crouch_", 0) == 0;
            bool secondCrouch = secondId.rfind("crouch_", 0) == 0;
            in.Down = firstCrouch;
            in.Buttons.Set(first.Button, true);
            int best = 0;
            bool queued = false;
            for (int f = 0; f < 200; ++f) {
                bs.Update(1.0 / 60.0, in);
                in.Buttons.Set(first.Button, false);
                in.Buttons.Set(second.Button, false);
                best = std::max(best, bs.P1ComboCount);
                // 1 発目の硬直が明ける少し前に 2 発目を先行入力しておく。
                // 先行入力は動けるようになった最初のフレームで技になります。
                if (!queued && bs.Player1.SM.CurrentMove == firstId &&
                    bs.Player1.SM.CurrentFrame >= first.ActionableFrame() - 3) {
                    in.Buttons.Set(second.Button, true);
                    in.Down = secondCrouch;
                    queued = true;
                }
            }
            return best;
        };
        Check("+4 の技から発生 4F の技はコンボになる (" +
                  std::to_string(comboCount("standing_light", "standing_light")) + " ヒット)",
              comboCount("standing_light", "standing_light") >= 2);
        Check("+3 の技から発生 4F の技はコンボにならない (" +
                  std::to_string(comboCount("crouch_light", "standing_light")) + " ヒット)",
              comboCount("crouch_light", "standing_light") < 2);
    }

    // =================================================================
    std::cout << "\n=== 技のあとの姿勢・しゃがみガード中の攻撃 ===\n";
    // =================================================================
    {
        double dt = 1.0 / 60.0;
        const CharacterStats* cs = dm.GetCharacter("ryu");
        const auto* ms = dm.GetMoveset("ryu");

        // ---- しゃがみ技のあとは、しゃがみのまま ----
        Fighter f;
        f.Setup(*cs, ms);
        f.Opponent = &f;
        RawInput down; down.Down = true;
        RawInput downLK = down; downLK.Buttons.LK = true;
        f.FrameStep(dt, downLK);
        bool startedCrouchMove = f.SM.CurrentMove == "crouch_light";
        bool sawStanding = false;
        for (int i = 0; i < 40; ++i) {
            f.FrameStep(dt, down);
            if (f.SM.CurrentState == CharState::Idle) sawStanding = true;
        }
        Check("しゃがみ攻撃を出したあと、一瞬も立ち状態にならない",
              startedCrouchMove && !sawStanding && f.SM.CurrentState == CharState::Crouch);

        // ---- 立ち技のあとは立ち ----
        Fighter f2;
        f2.Setup(*cs, ms);
        f2.Opponent = &f2;
        RawInput lp; lp.Buttons.LP = true;
        f2.FrameStep(dt, lp);
        bool sawCrouch = false;
        for (int i = 0; i < 40; ++i) {
            f2.FrameStep(dt, RawInput{});
            if (f2.SM.CurrentState == CharState::Crouch) sawCrouch = true;
        }
        Check("立ち攻撃のあとは立ち状態に戻る",
              !sawCrouch && f2.SM.CurrentState == CharState::Idle);

        // ---- しゃがみガード中でも攻撃ボタンで技が出る ----
        Fighter f3;
        f3.Setup(*cs, ms);
        f3.Opponent = &f3;                 // 自分が相手＝右向き。後ろは左。
        RawInput guard; guard.Down = true; guard.Left = true;
        for (int i = 0; i < 5; ++i) f3.FrameStep(dt, guard);
        bool inCrouchGuard = f3.SM.CurrentState == CharState::Block && f3.IsCrouchingGuard;
        RawInput guardLK = guard; guardLK.Buttons.LK = true;
        f3.FrameStep(dt, guardLK);
        Check("しゃがみガード中に弱K を押すとしゃがみ弱K が出る",
              inCrouchGuard && f3.SM.CurrentState == CharState::Attack &&
              f3.SM.CurrentMove == "crouch_light");

        // ---- 立ちガード中は立ち技が出る ----
        Fighter f4;
        f4.Setup(*cs, ms);
        f4.Opponent = &f4;
        RawInput back; back.Left = true;
        for (int i = 0; i < 5; ++i) f4.FrameStep(dt, back);
        RawInput backLP = back; backLP.Buttons.LP = true;
        f4.FrameStep(dt, backLP);
        Check("立ちガード姿勢から弱P を押すと立ち弱P が出る",
              f4.SM.CurrentState == CharState::Attack && f4.SM.CurrentMove == "standing_light");

        // ---- ガード姿勢はレバーを離せばすぐ解ける ----
        Fighter f5;
        f5.Setup(*cs, ms);
        f5.Opponent = &f5;
        RawInput crouchGuard; crouchGuard.Down = true; crouchGuard.Left = true;
        for (int i = 0; i < 5; ++i) f5.FrameStep(dt, crouchGuard);
        bool wasGuarding = f5.SM.CurrentState == CharState::Block;
        RawInput backOnly; backOnly.Left = true;
        f5.FrameStep(dt, backOnly);   // 下を離す → 後ろ歩きへ
        bool walksBack = f5.SM.CurrentState == CharState::WalkBackward;
        f5.FrameStep(dt, RawInput{}); // 全部離す → 立ち
        Check("ガード姿勢から下を離すと、その場で後ろ歩きに戻れる",
              wasGuarding && walksBack && f5.SM.CurrentState == CharState::Idle);
    }

    // =================================================================
    std::cout << "\n=== ジャンプ攻撃の硬直は着地後 ===\n";
    // =================================================================
    {
        double dt = 1.0 / 60.0;
        const CharacterStats* cs = dm.GetCharacter("ryu");
        const auto* ms = dm.GetMoveset("ryu");
        const MoveData* air = dm.GetMove("ryu", "jump_light_kick");

        Fighter f;
        f.Setup(*cs, ms);
        f.Opponent = &f;
        RawInput up; up.Up = true;
        f.FrameStep(dt, up);                      // ジャンプ
        for (int i = 0; i < 5; ++i) f.FrameStep(dt, RawInput{});
        RawInput lk; lk.Buttons.LK = true;
        f.FrameStep(dt, lk);                      // 空中で弱K
        bool startedAirMove = f.SM.CurrentMove == "jump_light_kick";

        // 持続が終わっても、着地するまで技は終わらない
        for (int i = 0; i < air->ActionableFrame() + 2; ++i) f.FrameStep(dt, RawInput{});
        bool stillAirborneAttack = f.SM.CurrentState == CharState::Attack && f.PositionY < -1.0;

        // 着地したフレームに着地硬直が積まれ、そのぶん動けない
        int landedFrame = -1, freeFrame = -1;
        for (int i = 0; i < 120; ++i) {
            f.FrameStep(dt, RawInput{});
            if (landedFrame < 0 && f.LandedDuringMove) landedFrame = i;
            if (landedFrame >= 0 && freeFrame < 0 && f.SM.IsActionable()) freeFrame = i;
            if (freeFrame >= 0) break;
        }
        Check("空中技は着地するまで終わらない", startedAirMove && stillAirborneAttack);
        Check("着地硬直は技データどおりのフレーム数 (" +
                  std::to_string(freeFrame - landedFrame) + "F / 設定 " +
                  std::to_string(air->LandingRecoveryFrames()) + "F)",
              landedFrame >= 0 && freeFrame - landedFrame == air->LandingRecoveryFrames());
        Check("着地硬直は技データで指定できる",
              air->LandingRecovery > 0 &&
              air->LandingRecoveryFrames() == air->LandingRecovery);
    }

    // =================================================================
    std::cout << "\n=== トレーニングのガード設定（当たる瞬間だけ入力）===\n";
    // =================================================================
    {
        const CharacterStats* cs = dm.GetCharacter("ryu");
        const auto* ms = dm.GetMoveset("ryu");

        // ---- 届かない距離で振っても、練習相手は動かない ----
        {
            BattleSystem bs;
            bs.StartMatch(*cs, ms, *cs, ms, 99);
            bs.TrainingMode = true;
            bs.CpuAI->Mode = DummyMode::Guard;
            bs.Player1.PositionX = -120; bs.Player2.PositionX = 120; // 遠い
            double startX = bs.Player2.PositionX;
            RawInput in; in.Buttons.LP = true;
            bool guarded = false, moved = false;
            for (int f = 0; f < 60; ++f) {
                bs.Update(1.0 / 60.0, in);
                in.Buttons.LP = false;
                if (bs.Player2.SM.CurrentState == CharState::Block ||
                    bs.Player2.SM.CurrentState == CharState::WalkBackward) guarded = true;
                if (std::abs(bs.Player2.PositionX - startX) > 0.5) moved = true;
            }
            Check("空振りにはガードしない（棒立ちのまま）", !guarded);
            Check("ガード設定でも後ろに下がっていかない", !moved);
        }

        // ---- 当たる距離なら、その瞬間だけガードする ----
        auto guardTest = [&](const char* moveId, bool useDown) {
            BattleSystem bs;
            bs.StartMatch(*cs, ms, *cs, ms, 99);
            bs.TrainingMode = true;
            bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Guard;
            bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;
            int startHp = bs.Player2.CurrentHP;
            RawInput in;
            in.Down = useDown;
            in.Buttons.Set(dm.GetMove("ryu", moveId)->Button, true);
            bool blocked = false;
            int guardInputFrames = 0;
            for (int f = 0; f < 120; ++f) {
                bs.Update(1.0 / 60.0, in);
                in.Buttons.Set(dm.GetMove("ryu", moveId)->Button, false);
                if (bs.Player2.BlockstunTimer > 0) blocked = true;
                // 「後ろを入れている」フレーム数を数える
                if (!bs.Player2.InputBuf.History.empty()) {
                    int digit = bs.Player2.InputBuf.History.back().digit;
                    if (digit == 1 || digit == 4 || digit == 7) guardInputFrames++;
                }
            }
            return std::make_pair(blocked && bs.Player2.CurrentHP == startHp, guardInputFrames);
        };
        auto standing = guardTest("standing_light", false);
        auto low = guardTest("crouch_heavy", true);   // 下段（しゃがみガードでしか防げない）
        Check("届く距離ならガードする（体力が減らない）", standing.first);
        Check("下段はしゃがみガードで防ぐ", low.first);
        Check("ガード入力は当たる瞬間の数フレームだけ (" +
                  std::to_string(standing.second) + "F)",
              standing.second > 0 && standing.second <= 6);
    }

    // =================================================================
    std::cout << "\n=== 前へ踏み込む方式（ダッシュ / ステップ）===\n";
    // =================================================================
    {
        const CharacterStats* base = dm.GetCharacter("ryu");
        const auto* ms = dm.GetMoveset("ryu");

        // 前・前 と入れて踏み込ませ、進んだ距離と掛かったフレーム数を測る。
        auto runForward = [&](const CharacterStats& cs, int frames) {
            BattleSystem bs;
            bs.StartMatch(cs, ms, cs, ms, 99);
            bs.TrainingMode = true; bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Stand;
            // 押し合いにも「離れすぎの制限」にも掛からない距離に置きます。
            // 上限（300）より離して置くと、毎フレーム自動で詰められて
            // しまい、踏み込んだ距離を正しく測れません。
            bs.Player1.PositionX = -100; bs.Player2.PositionX = 100;
            double start = bs.Player1.PositionX;
            RawInput in;
            // 前 → 離す → 前 で「前・前」の 2 回入力になります。
            for (int f = 0; f < frames; ++f) {
                in.Right = (f == 0) || (f >= 2);
                bs.Update(1.0 / 60.0, in);
            }
            return bs.Player1.PositionX - start;
        };

        // ---- ステップ: 決まった距離で必ず止まる ----
        {
            CharacterStats stepper = *base;
            stepper.ForwardMoveType = "step";
            stepper.StepDistance = GameSpec::CharacterVisualWidth; // キャラ 1 体ぶん
            stepper.StepFrames = 12;
            // ステップが終わるだけの時間を回す（レバーは入れっぱなし）
            double moved = runForward(stepper, 14);
            Check("ステップはキャラ 1 体ぶん（52px）進む (" +
                      std::to_string(static_cast<int>(std::lround(moved))) + "px)",
                  std::abs(moved - GameSpec::CharacterVisualWidth) < 2.0);

            // 距離を変えたらそのぶんだけ進む
            stepper.StepDistance = 100.0;
            double moved2 = runForward(stepper, 14);
            Check("ステップ距離を変えるとそのとおりに進む (" +
                      std::to_string(static_cast<int>(std::lround(moved2))) + "px)",
                  std::abs(moved2 - 100.0) < 2.0);

            // フレーム数を変えても距離は変わらない（速さが変わるだけ）
            stepper.StepDistance = GameSpec::CharacterVisualWidth;
            stepper.StepFrames = 6;
            double moved3 = runForward(stepper, 8);
            Check("ステップのフレーム数を変えても距離は同じ",
                  std::abs(moved3 - GameSpec::CharacterVisualWidth) < 2.0);
        }

        // ---- ステップは途中でレバーを離しても最後まで進む ----
        {
            CharacterStats stepper = *base;
            stepper.ForwardMoveType = "step";
            stepper.StepFrames = 12;
            BattleSystem bs;
            bs.StartMatch(stepper, ms, stepper, ms, 99);
            bs.TrainingMode = true; bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Stand;
            bs.Player1.PositionX = -100; bs.Player2.PositionX = 100;
            double start = bs.Player1.PositionX;
            RawInput in;
            for (int f = 0; f < 14; ++f) {
                in.Right = (f == 0) || (f == 2); // 2 回入れたらすぐ離す
                bs.Update(1.0 / 60.0, in);
            }
            double moved = bs.Player1.PositionX - start;
            Check("レバーを離してもステップは最後まで進みきる (" +
                      std::to_string(static_cast<int>(std::lround(moved))) + "px)",
                  std::abs(moved - stepper.StepDistance) < 2.0);
        }

        // ---- ダッシュ: 押している間ずっと速い ----
        {
            CharacterStats dasher = *base;
            dasher.ForwardMoveType = "dash";
            double moved = runForward(dasher, 14);
            // ダッシュは「速さ×時間」なので、ステップの決まった距離とは
            // 一致しません。歩きより速いことだけを確かめます。
            CharacterStats walker = *base;
            walker.ForwardMoveType = "dash";
            BattleSystem bs;
            bs.StartMatch(walker, ms, walker, ms, 99);
            bs.TrainingMode = true; bs.TrainingAutoHeal = false;
            bs.CpuAI->Mode = DummyMode::Stand;
            bs.Player1.PositionX = -100; bs.Player2.PositionX = 100;
            double start = bs.Player1.PositionX;
            RawInput in; in.Right = true; // 入れっぱなし＝ただの歩き
            for (int f = 0; f < 14; ++f) bs.Update(1.0 / 60.0, in);
            double walked = bs.Player1.PositionX - start;
            Check("ダッシュは歩きより速く進む (" +
                      std::to_string(static_cast<int>(std::lround(moved))) + "px > " +
                      std::to_string(static_cast<int>(std::lround(walked))) + "px)",
                  moved > walked);
        }

        // ---- 設定の読み書き ----
        {
            CharacterStats cs = *base;
            cs.ForwardMoveType = "step";
            cs.StepDistance = 77.0;
            cs.StepFrames = 9;
            CharacterStats back = CharacterStats::FromJson(cs.ToJson());
            Check("前進方式が JSON に保存・復元される",
                  back.ForwardMoveType == "step" && std::abs(back.StepDistance - 77.0) < 0.001 &&
                  back.StepFrames == 9);

            Json bad = cs.ToJson();
            bad.Set("forwardMoveType", Json(std::string("teleport")));
            Check("知らない前進方式は dash 扱いになる",
                  CharacterStats::FromJson(bad).ForwardMoveType == "dash");
        }
    }


    // =================================================================
    std::cout << "\n=== ヒットストップとガードストップは別々の値 ===\n";
    // =================================================================
    {
        MoveData m;
        m.Hitstop = 9;
        m.Guardstop = 9;
        // 片方を変えても、もう片方は変わらないこと（これが仕様の要点）。
        m.Hitstop = 15;
        Check("ヒットストップを変えてもガードストップは変わらない",
              m.Hitstop == 15 && m.Guardstop == 9);
        m.Guardstop = 13;
        Check("ガードストップだけ別の値にできる", m.Hitstop == 15 && m.Guardstop == 13);

        MoveData back = MoveData::FromJson(m.ToJson());
        Check("2 つとも別々に保存・復元される",
              back.Hitstop == 15 && back.Guardstop == 13);

        // 別名（hitStopFrames / blockStopFrames）でも書ける。
        Json j = Json::MakeObject();
        j.Set("id", Json(std::string("alias_test")));
        j.Set("hitStopFrames", Json(11));
        j.Set("blockStopFrames", Json(7));
        MoveData alias = MoveData::FromJson(j);
        Check("hitStopFrames / blockStopFrames の名前でも読める",
              alias.Hitstop == 11 && alias.Guardstop == 7);

        // ガードストップが書かれていない古いデータは、既定値で補う。
        Json legacy = Json::MakeObject();
        legacy.Set("id", Json(std::string("legacy_stop")));
        legacy.Set("hitstop", Json(9));
        MoveData old = MoveData::FromJson(legacy);
        Check("ガードストップが無い古いデータも読める", old.Hitstop == 9 && old.Guardstop > 0);
    }

    // =================================================================
    std::cout << "\n=== キャンセルの種類ごとの設定 ===\n";
    // =================================================================
    {
        MoveData m;
        m.Startup = 6; m.Active = 3; m.Recovery = 10;
        // 必殺技キャンセルは 6-11F、ヒット/ガード時のみ。
        CancelRule& sp = m.Cancel(CancelKind::Special);
        sp.Enabled = true; sp.StartFrame = 6; sp.EndFrame = 11;
        sp.OnHit = true; sp.OnBlock = true; sp.OnWhiff = false;
        // 超必キャンセルは硬直中（12-16F）だけ、ヒット時のみ。
        CancelRule& su = m.Cancel(CancelKind::Super);
        su.Enabled = true; su.StartFrame = 12; su.EndFrame = 16;
        su.OnHit = true; su.OnBlock = false; su.OnWhiff = false;

        Check("種類ごとに別々の時間帯を持てる",
              m.AllowsCancel(CancelKind::Special, 6, MoveContact::Hit) &&
              !m.AllowsCancel(CancelKind::Special, 12, MoveContact::Hit) &&
              m.AllowsCancel(CancelKind::Super, 12, MoveContact::Hit) &&
              !m.AllowsCancel(CancelKind::Super, 6, MoveContact::Hit));
        Check("時間帯は硬直中にも置ける（持続中だけではない）",
              m.Cancel(CancelKind::Super).StartFrame > m.Startup + m.Active - 1 &&
              m.AllowsCancel(CancelKind::Super, 14, MoveContact::Hit));
        Check("ヒット時・ガード時・空振り時を別々に設定できる",
              m.AllowsCancel(CancelKind::Special, 8, MoveContact::Blocked) &&
              !m.AllowsCancel(CancelKind::Special, 8, MoveContact::Whiff) &&
              !m.AllowsCancel(CancelKind::Super, 13, MoveContact::Blocked));

        // ターゲットコンボの派生先（指定した技だけ）
        CancelRule& tc = m.Cancel(CancelKind::TargetCombo);
        tc.Enabled = true; tc.StartFrame = 6; tc.EndFrame = 11;
        tc.AllowedMoves = {"standing_heavy"};
        Check("ターゲットコンボは派生先を技ごとに指定できる",
              m.AllowsCancel(CancelKind::TargetCombo, 7, MoveContact::Hit, "standing_heavy") &&
              !m.AllowsCancel(CancelKind::TargetCombo, 7, MoveContact::Hit, "crouch_medium"));
        Check("派生先が空なら制限なし",
              m.Cancel(CancelKind::Special).AllowsTarget("何でも"));

        // 保存・復元
        MoveData back = MoveData::FromJson(m.ToJson());
        Check("キャンセル設定が JSON に保存・復元される",
              back.Cancel(CancelKind::Super).StartFrame == 12 &&
              back.Cancel(CancelKind::Super).OnBlock == false &&
              back.Cancel(CancelKind::TargetCombo).AllowedMoves.size() == 1 &&
              back.Cancel(CancelKind::TargetCombo).AllowedMoves[0] == "standing_heavy");

        // 古い形式（1 つだけの時間帯）も読める
        Json legacy = Json::MakeObject();
        legacy.Set("id", Json(std::string("legacy_cancel")));
        legacy.Set("cancelStartFrame", Json(4));
        legacy.Set("cancelEndFrame", Json(10));
        Json routes = Json::MakeArray();
        routes.Push(Json(std::string("standing_heavy")));
        legacy.Set("cancelRoutes", std::move(routes));
        MoveData conv = MoveData::FromJson(legacy);
        Check("古い形式のキャンセル指定も読める",
              conv.Cancel(CancelKind::Special).Enabled &&
              conv.Cancel(CancelKind::Special).StartFrame == 4 &&
              conv.Cancel(CancelKind::Special).EndFrame == 10 &&
              conv.Cancel(CancelKind::TargetCombo).AllowedMoves.size() == 1);

        // 技の種類から、どの設定を見るかが決まる
        const MoveData* fireball = dm.GetMove("ryu", "fireball");
        const MoveData* super = dm.GetMove("ryu", "super_combo");
        const MoveData* heavy = dm.GetMove("ryu", "standing_heavy");
        Check("必殺技は必殺技キャンセル、超必は超必キャンセルで判定される",
              fireball->CancelKindAsTarget() == CancelKind::Special &&
              super->CancelKindAsTarget() == CancelKind::Super &&
              heavy->CancelKindAsTarget() == CancelKind::TargetCombo);
    }

    // =================================================================
    std::cout << "\n=== 技中の空中判定 ===\n";
    // =================================================================
    {
        MoveData m;
        m.Startup = 4; m.Active = 3; m.Recovery = 10;
        m.AirborneEnabled = true;
        m.AirborneStart = 5;
        m.AirborneDuration = 8;
        // 5F から 8 フレーム＝ 5〜12F が空中、13F からは地上。
        Check("指定したフレームだけ空中判定になる（5-12F）",
              !m.IsAirborneAtFrame(4, false) && m.IsAirborneAtFrame(5, false) &&
              m.IsAirborneAtFrame(12, false) && !m.IsAirborneAtFrame(13, false));
        Check("空中判定は技のフェーズと関係なく設定できる",
              m.IsAirborneAtFrame(5, false) &&              // 発生中
              m.IsAirborneAtFrame(8, false));               // 硬直中

        m.AirborneKind = AirborneMode::UntilLanding;
        Check("着地までモードでは、実際に着地するまで続く",
              m.IsAirborneAtFrame(30, true) && !m.IsAirborneAtFrame(30, false));

        MoveData back = MoveData::FromJson(m.ToJson());
        Check("空中判定の設定が JSON に保存・復元される",
              back.AirborneEnabled && back.AirborneStart == 5 &&
              back.AirborneDuration == 8 &&
              back.AirborneKind == AirborneMode::UntilLanding);

        // 実際のキャラクターで、地面にいても空中扱いになるか
        std::unordered_map<std::string, MoveData> ms = *dm.GetMoveset("ryu");
        MoveData& shoryu = ms["anti_air_special"];
        shoryu.AirborneEnabled = true;
        shoryu.AirborneStart = 5;
        shoryu.AirborneDuration = 8;
        shoryu.AirborneKind = AirborneMode::FixedDuration;

        Fighter f;
        f.Setup(*dm.GetCharacter("ryu"), &ms);
        f.Opponent = &f;
        f.CurrentMoveData = &shoryu;
        f.SM.ChangeState(CharState::Attack, shoryu.Id);
        f.SM.CurrentFrame = 6;
        f.PositionY = 0.0; // 見た目は地面に立ったまま
        Check("Y 座標が地面でも、指定フレームなら空中判定になる",
              f.IsAirborne() && f.Stance() == "air" && !f.IsThrowable());
        f.SM.CurrentFrame = 14;
        Check("指定フレームを過ぎれば地上判定に戻る",
              !f.IsAirborne() && f.Stance() == "stand" && f.IsThrowable());
    }

    // =================================================================
    std::cout << "\n=== バックステップ（前ステップとは独立）===\n";
    // =================================================================
    {
        double dt = 1.0 / 60.0;
        CharacterStats cs = *dm.GetCharacter("ryu");
        const auto* ms = dm.GetMoveset("ryu");

        // 前とバックで別々の設定を持てること
        cs.ForwardMoveType = "step";
        cs.StepDistance = 52; cs.StepFrames = 12;
        cs.BackMoveType = "step";
        cs.BackStepDistance = 45; cs.BackStepFrames = 20;
        CharacterStats copy = cs;
        copy.StepDistance = 100; copy.StepFrames = 5;
        Check("前ステップの値を変えてもバックステップは変わらない",
              copy.BackStepDistance == cs.BackStepDistance &&
              copy.BackStepFrames == cs.BackStepFrames);

        CharacterStats back = CharacterStats::FromJson(cs.ToJson());
        Check("バックステップの設定が JSON に保存・復元される",
              back.BackMoveType == "step" && back.BackStepDistance == 45 &&
              back.BackStepFrames == 20);

        // 実際に後ろへ 2 回入れて下がる距離を測る
        auto stepDistance = [&](bool forward) {
            Fighter f;
            f.Setup(cs, ms);
            Fighter dummy;
            dummy.Setup(cs, ms);
            dummy.PositionX = 200; // 右にいる＝ f は右向き
            f.Opponent = &dummy;
            f.PositionX = 0;
            double start = f.PositionX;
            RawInput dir;
            // 右向きなので、前 = 右、後ろ = 左
            RawInput tap;
            if (forward) tap.Right = true; else tap.Left = true;
            f.FrameStep(dt, tap);            // 1 回目
            f.FrameStep(dt, RawInput{});     // 離す
            f.FrameStep(dt, tap);            // 2 回目 → 成立
            for (int i = 0; i < 40; ++i) f.FrameStep(dt, RawInput{});
            return f.PositionX - start;
        };
        double fwd = stepDistance(true);
        double bwd = stepDistance(false);
        Check("後ろ・後ろ でバックステップが出る (" + std::to_string(static_cast<int>(bwd)) + "px)",
              bwd < -30.0);
        Check("前ステップとバックステップで距離が違う (" +
                  std::to_string(static_cast<int>(fwd)) + " / " +
                  std::to_string(static_cast<int>(bwd)) + ")",
              fwd > 30.0 && std::abs(std::abs(fwd) - std::abs(bwd)) > 3.0);
    }

    // =================================================================
    std::cout << "\n=== 技の追加・複製・削除（保存まで）===\n";
    // =================================================================
    {
        DataManager dm3(dataDir, tempUserDir / "moveedit");
        dm3.ReloadAll();
        size_t before = dm3.GetMoveset("ryu")->size();

        // 追加
        MoveData added;
        added.Id = "move_001";
        added.Name = "New Move";
        added.Startup = 1; added.Active = 1; added.Recovery = 1;
        dm3.SaveMove("ryu", added);
        Check("技を追加できる", dm3.GetMove("ryu", "move_001") != nullptr &&
                                dm3.GetMoveset("ryu")->size() == before + 1);

        // 複製（ID だけ新しくする）
        MoveData copy = *dm3.GetMove("ryu", "standing_light");
        copy.Id = "standing_light_copy";
        copy.Name = copy.Name + " Copy";
        dm3.SaveMove("ryu", copy);
        const MoveData* saved = dm3.GetMove("ryu", "standing_light_copy");
        Check("技を複製できる（中身は同じ、ID だけ別）",
              saved != nullptr && saved->Startup == 4 && saved->HitAdvantage == 4 &&
              saved->Id != "standing_light");

        // 再読み込みしても残っている
        DataManager dm4(dataDir, tempUserDir / "moveedit");
        dm4.ReloadAll();
        Check("追加・複製した技は読み直しても残る",
              dm4.GetMove("ryu", "move_001") != nullptr &&
              dm4.GetMove("ryu", "standing_light_copy") != nullptr);

        // 削除（自分で足した技）
        dm4.DeleteMove("ryu", "move_001");
        Check("追加した技を削除できる", dm4.GetMove("ryu", "move_001") == nullptr);

        // 削除（元データにある技）→ 消した印で残らないこと
        dm4.DeleteMove("ryu", "standing_light_kick");
        DataManager dm5(dataDir, tempUserDir / "moveedit");
        dm5.ReloadAll();
        Check("元データにある技を消しても、読み直して復活しない",
              dm5.GetMove("ryu", "standing_light_kick") == nullptr &&
              dm5.GetMove("ryu", "standing_light") != nullptr);
    }

    // =================================================================
    std::cout << "\n=== ストップ中のキャンセル入力 ===\n";
    // =================================================================
    // ヒットストップ中も入力は受け付け、ストップが解けた瞬間に
    // キャンセルが成立する、という流れを実際に走らせて確かめます。
    {
        const CharacterStats* cs = dm.GetCharacter("ryu");
        std::unordered_map<std::string, MoveData> ms = *dm.GetMoveset("ryu");
        for (auto& kv : ms) kv.second.KnockbackX = 0.0; // 間合いを保つ

        BattleSystem bs;
        bs.StartMatch(*cs, &ms, *cs, &ms, 99);
        bs.TrainingMode = true;
        bs.TrainingAutoHeal = false;
        bs.CpuAI->Mode = DummyMode::Stand;
        bs.Player1.PositionX = -18; bs.Player2.PositionX = 18;

        RawInput in;
        in.Buttons.MP = true;              // 立ち中パンチ
        bool stopSeen = false, commandDuringStop = false;
        bool cancelled = false;
        int commandStep = 0;
        for (int f = 0; f < 90; ++f) {
            bs.Update(1.0 / 60.0, in);
            in.Buttons.MP = false;

            if (bs.Player1.IsStopped()) {
                stopSeen = true;
                commandDuringStop = true;
                // ストップ中に波動拳コマンド（2 → 3 → 6 ＋ P）を入れる
                RawInput cmd;
                if (commandStep == 0) { cmd.Down = true; }
                else if (commandStep == 1) { cmd.Down = true; cmd.Right = true; }
                else if (commandStep == 2) { cmd.Right = true; }
                else { cmd.Right = true; cmd.Buttons.LP = true; }
                commandStep++;
                in = cmd;
            } else if (commandDuringStop) {
                in = RawInput{}; // ストップが解けたら入力はもう足さない
            }
            if (bs.Player1.SM.CurrentMove == "fireball") { cancelled = true; break; }
        }
        Check("ヒットストップが実際に起きている", stopSeen);
        Check("ストップ中に入れたコマンドで、解けたあとキャンセルが成立する", cancelled);
    }

    // =================================================================
    std::cout << "\n=== ドライブラッシュキャンセル（技をキャンセルして踏み込む）===\n";
    // =================================================================
    {
        double dt = 1.0 / 60.0;
        CharacterStats cs = *dm.GetCharacter("ryu");
        std::unordered_map<std::string, MoveData> ms = *dm.GetMoveset("ryu");

        Fighter f;
        Fighter dummy;
        f.Setup(cs, &ms);
        dummy.Setup(cs, &ms);
        dummy.PositionX = 200;          // 右にいる＝ f は右向き
        f.Opponent = &dummy;

        RawInput mp; mp.Buttons.MP = true;
        f.FrameStep(dt, mp);            // 立ち中パンチ（キャンセル 6-11F）
        f.CurrentMoveContact = MoveContact::Hit; // 当たったことにする
        for (int i = 0; i < 5; ++i) f.FrameStep(dt, RawInput{}); // 6F 目まで進める
        bool inWindow = f.SM.CurrentState == CharState::Attack && f.SM.CurrentFrame >= 6;

        RawInput fwd; fwd.Right = true;
        f.FrameStep(dt, fwd);              // 前 1 回目
        f.FrameStep(dt, RawInput{});       // 離す
        f.FrameStep(dt, fwd);              // 前 2 回目 → キャンセルして踏み込む
        Check("キャンセル可能時間中に 前・前 で技をキャンセルして踏み込める",
              inWindow && f.SM.CurrentState == CharState::WalkForward &&
              f.CurrentMoveData == nullptr);

        // 設定を切ってあれば起きないこと
        ms["standing_medium"].Cancel(CancelKind::DriveRush).Enabled = false;
        Fighter g;
        g.Setup(cs, &ms);
        g.Opponent = &dummy;
        g.FrameStep(dt, mp);
        g.CurrentMoveContact = MoveContact::Hit;
        for (int i = 0; i < 5; ++i) g.FrameStep(dt, RawInput{});
        g.FrameStep(dt, fwd);
        g.FrameStep(dt, RawInput{});
        g.FrameStep(dt, fwd);
        Check("設定を切ればドライブラッシュキャンセルは起きない",
              g.SM.CurrentState == CharState::Attack);
    }

    // =================================================================
    std::cout << "\n=== 日本語フォントの読み込み（TrueType）===\n";
    // =================================================================
    // パソコンに入っているフォントを読むので、環境によっては
    // 見つからないことがあります。その場合は「内蔵のカナに戻る」ことだけ
    // 確かめて、あとは飛ばします（失敗にはしません）。
    {
        TrueTypeFont font;
        bool found = LoadSystemJapaneseFont(font, std::string());
        if (!found) {
            std::cout << "  （日本語フォントが見つからない環境のため、"
                         "内蔵のカナ表示に切り替わります）\n";
            Check("フォントが無くても読み込みは安全に失敗する", !font.IsLoaded());
        } else {
            std::cout << "  使用フォント: " << font.Path() << "\n";
            Check("日本語フォントを読み込めた", font.IsLoaded());
            Check("ひらがな・カタカナ・漢字が入っている",
                  font.HasGlyph(0x3042) && font.HasGlyph(0x30A2) && font.HasGlyph(0x6C34));
            Check("英数字も入っている", font.HasGlyph('A') && font.HasGlyph('0'));

            // 12 ピクセルで描いたときの形をざっと確かめます。
            const int size = 12;
            const TrueTypeFont::Glyph* g = font.GetGlyph(0x751F, size); // 「生」
            bool shaped = g != nullptr && g->width > 4 && g->width <= size + 2 &&
                          g->height > 4 && g->height <= size + 2;
            Check("漢字が 12px の枠に収まる大きさで描ける", shaped);

            if (g != nullptr) {
                int ink = 0;
                for (int y = 0; y < g->height; ++y) {
                    for (int x = 0; x < g->width; ++x) if (g->Get(x, y)) ink++;
                }
                // 真っ白（1 つも点が無い）でも、真っ黒（全部点）でもないこと。
                int total = g->width * g->height;
                Check("漢字の中身が塗られている（白紙でも塗りつぶしでもない）",
                      ink > total / 10 && ink < total * 9 / 10);
            }

            // 同じ字を 2 回頼んだら、同じものが返る（覚えている）。
            const TrueTypeFont::Glyph* again = font.GetGlyph(0x751F, size);
            Check("同じ字は覚えておいて使い回す", g == again);

            // 送り幅は全角ぶん（およそ 1 文字ぶん）
            Check("全角の送り幅がだいたい 1 文字ぶん",
                  g != nullptr && g->advance >= size - 2 && g->advance <= size + 2);

            // 使われていない領域の文字は「無い」と答える
            Check("フォントに無い文字は nullptr を返す",
                  font.GetGlyph(0x0F0000, size) == nullptr);
        }

        // 壊れたファイルを渡しても落ちない
        TrueTypeFont broken;
        fs::path junk = tempUserDir / "not-a-font.ttf";
        {
            std::ofstream out(junk, std::ios::binary);
            out << "this is definitely not a font file";
        }
        Check("フォントでないファイルは読み込みに失敗する（落ちない）",
              !broken.LoadFromFile(junk.string()));
        Check("存在しないファイルも安全に失敗する",
              !broken.LoadFromFile((tempUserDir / "missing.ttf").string()));
    }

    // =================================================================
    std::cout << "\n=== 付属キャラクターのデータ点検（全キャラ共通）===\n";
    // =================================================================
    // キャラクターを増やしたときに、データの書き間違いを自動で
    // 見つけるための点検です。ここが通っていれば、少なくとも
    // 「出せない技」「絶対に当たらない技」は入っていません。
    {
        for (const std::string& charId : dm.GetCharacterIds()) {
            const CharacterStats* cs = dm.GetCharacter(charId);
            const auto* moveset = dm.GetMoveset(charId);
            Check(charId + ": キャラクターと技一覧が読み込める",
                  cs != nullptr && moveset != nullptr && !moveset->empty());
            if (!cs || !moveset) continue;

            bool framesOk = true, boxesOk = true, hurtOk = true, buttonsOk = true;
            std::string firstBad;
            for (const auto& kv : *moveset) {
                const MoveData& m = kv.second;
                // フレームは 1 以上。全体フレームは (発生-1)+持続+硬直。
                if (m.Startup < 1 || m.Active < 1 || m.Recovery < 1 ||
                    m.TotalFrame != m.TotalFrames()) {
                    framesOk = false;
                    if (firstBad.empty()) firstBad = m.Id + "(フレーム)";
                }
                // 出し方: コマンド技でなければボタンの指定が要ります
                //（無いと、どのボタンでも出せない技になります）。
                if (m.InputCommand.empty() && m.Button.empty()) {
                    buttonsOk = false;
                    if (firstBad.empty()) firstBad = m.Id + "(ボタン)";
                }
                // 攻撃判定: 投げと飛び道具以外は、判定が無いと当たりません。
                bool needsHitbox = m.GuardType != Constants::GuardThrow &&
                                   !m.Projectile.present;
                if (needsHitbox && m.Hitboxes.empty()) {
                    boxesOk = false;
                    if (firstBad.empty()) firstBad = m.Id + "(攻撃判定なし)";
                }
                // 技ごとの食らい判定は、攻撃判定より前に出てはいけません。
                // 出ていると「自分から相手の拳へ体を差し出す技」になります。
                if (m.HasHurtboxOverride() && !m.Hitboxes.empty()) {
                    double hurtFront = -1e9, hitFront = -1e9;
                    for (const auto& p : m.Hurtboxes) {
                        hurtFront = std::max(hurtFront, p.Box.Right());
                    }
                    for (const auto& hb : m.Hitboxes) {
                        hitFront = std::max(hitFront, hb.offsetX + hb.width / 2.0);
                    }
                    if (hurtFront > hitFront) {
                        hurtOk = false;
                        if (firstBad.empty()) firstBad = m.Id + "(食らい判定が前すぎ)";
                    }
                }
            }
            Check(charId + ": すべての技のフレーム数が筋が通っている" +
                      (framesOk ? "" : " -> " + firstBad), framesOk);
            Check(charId + ": すべての技に出し方（ボタンかコマンド）がある", buttonsOk);
            Check(charId + ": 打撃技には攻撃判定がある", boxesOk);
            Check(charId + ": 技ごとの食らい判定が攻撃判定より前に出ていない", hurtOk);
        }
    }

    // =================================================================
    std::cout << "\n=== サンプルキャラクター デク ===\n";
    // =================================================================
    // 追加したキャラクターが「データとして正しい」だけでなく、
    // 実際に動かして技が出るところまで確かめます。
    {
        double dt = 1.0 / 60.0;
        const CharacterStats* cs = dm.GetCharacter("deku");
        const auto* ms = dm.GetMoveset("deku");
        Check("デクが読み込める", cs != nullptr && ms != nullptr);
        if (cs != nullptr && ms != nullptr) {
            Check("デクはリュウより速く、体力が低い",
                  cs->WalkForwardSpeed > dm.GetCharacter("ryu")->WalkForwardSpeed &&
                  cs->MaxHP < dm.GetCharacter("ryu")->MaxHP);
            Check("デクの前進は「ステップ」方式（前・前 で 1 体ぶん踏み込む）",
                  cs->ForwardMoveType == "step" && cs->StepDistance > 0);

            // 立ち弱パンチが実際に出る
            Fighter f;
            f.Setup(*cs, ms);
            f.Opponent = &f;
            RawInput lp;
            lp.Buttons.LP = true;
            f.FrameStep(dt, lp);
            Check("弱P を押すと立ち弱パンチが出る",
                  f.SM.CurrentState == CharState::Attack &&
                  f.SM.CurrentMove == "standing_light");

            // 弱P（+5）から中P（発生 5F）へつながる
            const MoveData* light = dm.GetMove("deku", "standing_light");
            const MoveData* medium = dm.GetMove("deku", "standing_medium");
            Check("立ち弱P から立ち中P がつながる（+5 ≧ 発生 5F）",
                  light != nullptr && medium != nullptr && light->CombosInto(*medium));

            // 236 + P でエアバースト（飛び道具）が出る
            BattleSystem bs;
            bs.StartMatch(*cs, ms, *cs, ms, 99);
            Fighter& p1 = bs.Player1;
            const int cmdDigits[3] = {2, 3, 6};
            for (int i = 0; i < 3; ++i) {
                RawInput in;
                in.Down = (cmdDigits[i] == 2 || cmdDigits[i] == 3);
                in.Right = (cmdDigits[i] == 3 || cmdDigits[i] == 6);
                bs.Update(dt, in);
            }
            RawInput punch;
            punch.Right = true;
            punch.Buttons.HP = true;
            bs.Update(dt, punch);
            bool started = (p1.SM.CurrentState == CharState::Attack &&
                            p1.SM.CurrentMove == "fireball");
            // 持続フレームに入ると弾が生まれます。
            for (int i = 0; i < 20 && bs.Projectiles.empty(); ++i) {
                bs.Update(dt, RawInput{});
            }
            Check("236＋P でエアバースト（飛び道具）が出る",
                  started && !bs.Projectiles.empty());

            // 足払いは、伸ばした脚が食らい判定になる
            const MoveData* sweep = dm.GetMove("deku", "crouch_heavy");
            double front = -1e9;
            if (sweep != nullptr) {
                for (const auto& p : sweep->Hurtboxes) front = std::max(front, p.Box.Right());
            }
            double crouchFront = -1e9;
            for (const auto& p : cs->Hurtboxes.Crouch) {
                crouchFront = std::max(crouchFront, p.Box.Right());
            }
            Check("足払いは食らい判定も前へ伸びている（技ごとの食らい判定）",
                  sweep != nullptr && sweep->HasHurtboxOverride() && front > crouchFront);
        }
    }

    // 一時フォルダを片付ける
    std::error_code ec;
    fs::remove_all(tempUserDir, ec);

    std::cout << "\n=== RESULT: " << Passed << " passed, " << Failed << " failed ===\n";
    return Failed > 0 ? 1 : 0;
}
