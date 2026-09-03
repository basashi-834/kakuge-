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

        for (int i = 0; i < 4; i++) fighter.FrameStep(dt, neutral2);
        Check("4 フレーム後もまだ立ち弱パンチ中",
              fighter.SM.CurrentMove == "standing_light" && fighter.SM.CurrentFrame == 4);

        RawInput heavyInput; heavyInput.Buttons.HP = true;
        fighter.FrameStep(dt, heavyInput);
        Check("キャンセル可能時間内に強パンチでコンボが繋がる",
              fighter.SM.CurrentMove == "standing_heavy" && fighter.SM.CurrentFrame == 0);
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
              air.CenterY == -40 + Fighter::AirPushboxCenterY);
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
        Check("フレームデータ: 弱P 4/3/7、強P 7/3/13、弱K 5/3/8、強K 9/4/16",
              lp->Startup == 4 && lp->Active == 3 && lp->Recovery == 7 && lp->TotalFrame == 14 &&
              hp->Startup == 7 && hp->Active == 3 && hp->Recovery == 13 && hp->TotalFrame == 23 &&
              lk->Startup == 5 && lk->Active == 3 && lk->Recovery == 8 && lk->TotalFrame == 16 &&
              hk->Startup == 9 && hk->Active == 4 && hk->Recovery == 16 && hk->TotalFrame == 29);

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
              MoveExecutor::GetActiveHitboxRects(*lp, 0, Constants::FacingRight, 100, 0).empty());

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
                int expected = mv->Hitstun - mv->RemainingFramesAt(m.attackerFrameAtContact);
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
                int expected = mv->Blockstun - mv->RemainingFramesAt(m.attackerFrameAtContact);
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

    // 一時フォルダを片付ける
    std::error_code ec;
    fs::remove_all(tempUserDir, ec);

    std::cout << "\n=== RESULT: " << Passed << " passed, " << Failed << " failed ===\n";
    return Failed > 0 ? 1 : 0;
}
