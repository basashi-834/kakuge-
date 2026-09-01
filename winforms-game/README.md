# Kakuge (WinForms版) - 2D対戦格闘ゲーム

**追加インストール一切不要**、Windows 10/11に標準搭載されている
**Windows PowerShell 5.1 + .NET Framework (WinForms)** だけで動く
2D対戦格闘ゲームです。`Launch.bat` をダブルクリックするだけで起動します。

これは元々Godot Engineで作った同名プロジェクト(`../game/`)を、実行環境に
Godot/Python/Node.js等を一切インストールできない制約のもとで、**Windows
標準機能のみで作り直したもの**です。ゲームシステムの設計・技データ・
JSONスキーマは元プロジェクトと共通です。

## 1. 使用技術と選定理由

| 技術 | 用途 |
|---|---|
| Windows PowerShell 5.1 | 実行エンジン(スクリプト言語) |
| .NET Framework 4.x | ランタイム(Windows 10/11に標準搭載) |
| System.Windows.Forms | ウィンドウ・UI・入力 |
| System.Drawing (GDI+) | 描画(図形ベース) |
| PowerShellの`class`構文 | Character/MoveData/BattleSystem等のロジック |
| `ConvertTo-Json`/`ConvertFrom-Json` | データ永続化(標準cmdlet、追加アセンブリ不要) |

Windows 10/11は例外なく Windows PowerShell 5.1 と .NET Framework 4.x を
標準搭載しており、`System.Windows.Forms`はそのFrameworkに含まれる標準
アセンブリです。そのため `Add-Type -AssemblyName System.Windows.Forms` を
呼ぶだけでGUIアプリが作成でき、**インストール作業が一切不要**です。

※本セッションの開発環境はLinuxのサンドボックスであり、実機Windowsで
`$PSVersionTable`等を直接確認することはできませんでした。上記は
Windows 10/11の標準構成として広く保証されているバージョンを前提に
選定したものです。もし万一お使いの環境でエラーが出た場合は、
`error.log`(起動フォルダに自動生成)の内容を教えてください。

### WinForms版で難しい点と対策

Godot版と比べて技術的に劣る点を隠さず記載します(機能は削っていません)。

| 項目 | 内容 | 対策 |
|---|---|---|
| 描画性能 | GDI+はCPU描画(GPU非使用) | 図形のみのシンプル描画+ダブルバッファリングで60FPS想定。低スペック機では多少の描画遅延の可能性 |
| タイマー精度 | `System.Windows.Forms.Timer`は正確に60Hzではない(~10-15ms単位) | Stopwatchによる固定タイムステップ(アキュムレータ)方式でロジック側は正確に1/60秒刻みで進行(フレームデータの正確性は保たれる) |
| サウンド | `System.Media.SoundPlayer`は**.wav形式のみ**対応 | `Audio/`フォルダに.wavを置けば自動再生される仕組みは用意済み(音源は無音のまま同梱) |
| フルスクリーン/VSync | WinForms専用APIがない | 固定サイズウィンドウで実行。ティアリングが出る場合あり |
| キャラクター選択画面 | 未実装 | `Main.ps1`の`$script:PlayerCharId`/`$script:CpuCharId`を直接編集すれば切り替え可能な構造 |

## 2. 必要環境

- Windows 10 または Windows 11
- 追加インストール一切不要(Windows標準機能のみ)

## 3. プロジェクト構成

```
winforms-game/
├─ Launch.bat                     # ダブルクリックで起動するランチャー
├─ Main.ps1                       # エントリポイント(全モジュール読込・画面遷移)
├─ Character/                     # キャラクター本体
│   ├─ Constants.ps1                 # CharState enum・ガード/タグ等の定数
│   ├─ Boxes.ps1                     # RectBox(当たり判定の矩形)・HurtboxSet
│   ├─ CharacterStats.ps1            # 基本性能データクラス
│   ├─ StateMachine.ps1              # 状態管理
│   ├─ SuperGauge.ps1                # スーパーゲージ
│   └─ Fighter.ps1                   # キャラクターコントローラ本体
├─ MoveData/                      # 技データ
│   ├─ MoveData.ps1                  # 技データクラス(JSON⇔オブジェクト)
│   └─ MoveExecutor.ps1              # Startup/Active/Recoveryのフレーム管理
├─ BattleSystem/                  # 戦闘処理
│   ├─ BattleSystem.ps1              # 1試合の進行・当たり判定解決・Pushbox解決
│   └─ Projectile.ps1                # 飛び道具(独立オブジェクト)
├─ InputSystem/
│   └─ InputSystem.ps1               # 入力バッファ・コマンド認識(236/623/236236)
├─ CPUAI/
│   └─ CPUAI.ps1                     # CPU AI(Characterから分離)
├─ UI/                            # 画面・描画・入力
│   ├─ RenderHelpers.ps1             # GDI+描画ヘルパー(HUD・デバッグ表示含む)
│   ├─ AudioHelper.ps1               # サウンド再生(.wav)
│   ├─ TitleScreen.ps1
│   ├─ GameScreen.ps1                # 対戦画面(ゲームループ・入力取得)
│   └─ ResultScreen.ps1
├─ CharacterEditor/
│   └─ CharacterEditorScreen.ps1     # キャラクターエディタ画面
├─ Data/                          # データ(JSON)
│   ├─ DataManager.ps1               # JSON読み書き(標準データ+ユーザー上書き)
│   ├─ match_rules.json              # 試合ルール(制限時間等)
│   ├─ characters/ryu.json
│   └─ moves/ryu/*.json              # 技データ11種
├─ Audio/                         # .wavを置く場所(空でも動作)
└─ tests/
    └─ HeadlessLogicTest.ps1         # 画面表示なしで戦闘ロジックを検証する自動テスト
```

設計方針(1ファイルに詰め込まない):
- Character(`Fighter.ps1`)とMoveData(`MoveData.ps1`)を分離
- BattleSystem(`BattleSystem.ps1`)とCharacterを分離、GameScreenは戦闘ロジックを直接持たない
- CPUAI(`CPUAI.ps1`)をCharacterから分離
- UI(`UI/`, `CharacterEditor/`)と戦闘処理(`BattleSystem/`, `Character/`)を分離
- 技・キャラクターデータはすべてJSON化(`Data/`)

## 4. 開発版の起動方法

`Launch.bat`をダブルクリックするだけです。開発中にコンソール出力を見たい
場合は、PowerShellを開いて直接実行してください。

```powershell
cd winforms-game
powershell -NoProfile -ExecutionPolicy Bypass -File .\Main.ps1
```

## 5. 完成したゲームの起動方法(ダブルクリック)

**`Launch.bat` をダブルクリックしてください。** これだけで起動します。
`.exe`化は行っていません(理由は次項)。

## 6. なぜ `.exe` ではなく `.bat` + `.ps1` なのか

PowerShellスクリプトを単一の`.exe`に変換するには`ps2exe`等の追加モジュール
のインストールが必要で、「追加インストール不要」という制約に反するため
行っていません。`Launch.bat`は`-ExecutionPolicy Bypass`をこの実行時のみ
一時的に適用してから`Main.ps1`を起動するため、PowerShellの実行ポリシーが
「制限」設定になっている環境でも(システム設定を変更することなく)
ダブルクリックで問題なく起動できます。

## 7. 操作方法

| 操作 | キー |
|---|---|
| 左移動/後退 | A |
| 右移動/前進 | D |
| しゃがみ | S |
| ジャンプ | Space |
| 弱攻撃 | J |
| 中攻撃 | K |
| 強攻撃 | L |
| 必殺技ボタン | U |
| 超必殺技ボタン | I |
| 投げ(弱+中同時押し) | J + K |
| デバッグ表示ON/OFF | F1 |

- **ガード**: 相手と逆方向(後退方向)を入力すると成立
- **波動拳コマンド**: `↓ → ↘` の入力後にU
- **対空必殺技コマンド**: `→ ↓ ↘` の入力後にU(発生直後に打撃無敵あり)
- **超必殺技コマンド**: 波動拳コマンドを2回連続入力後にI(ゲージ100必要)
- **キャンセル**: 通常技のキャンセル受付フレーム中に必殺技/超必殺技を入力

## 8. キャラクターエディタの使い方

タイトル画面から **CHARACTER EDIT** を選択します。

1. 左上のドロップダウンでキャラクターを選択
2. 右のドロップダウンで編集する技を選択
3. 左側で基本性能(名前・最大HP・前進/後退速度・ダッシュ速度・ジャンプ力・重力)を編集
4. 右側で選択中の技のフレームデータ(発生/持続/硬直・ダメージ・ヒット/ガード硬直・
   ヒットストップ・ノックバックX/Y・Hitbox位置とサイズ・ガード属性・キャンセル可否・
   キャンセル先)を編集(On Hit/On Block/TotalFrameがリアルタイム表示されます)
5. **New Move** で新しい技スロットを追加できます
6. **SAVE** で保存すると `%APPDATA%\Kakuge\characters\` と
   `%APPDATA%\Kakuge\moves\<id>\` にJSONとして書き出され、ゲームを
   再起動しても保持されます(`Data\`フォルダの初期データは上書きしません)

## 9. 新しい技を追加する方法

**方法A: エディタから** - CHARACTER EDIT画面で **New Move** → パラメータ入力 → **SAVE**

**方法B: JSONを直接追加**
1. `Data\moves\<character_id>\` に新しい`.json`を作成(既存ファイルをコピーして調整)
2. 最低限 `id`/`name`/`startup`/`active`/`recovery`/`damage`/`guardType`/`hitbox`/
   `input`/`button`/`stance` を設定
3. 既存技の `cancelRoutes` にこの技のidを追加すればキャンセルで出せる
4. ゲームを再起動すると自動的に読み込まれます(コード変更不要)

## 10. 新しいキャラクターを追加する方法

1. `Data\characters\<new_id>.json` を作成(`ryu.json`をコピーして調整)
2. `Data\moves\<new_id>\` フォルダを作成し技JSON一式を配置(`moves\ryu\`をコピーして調整すればすぐ動作)
3. `Main.ps1` の `$script:PlayerCharId` / `$script:CpuCharId` を変更して選択
   (キャラクターセレクト画面は未実装 - 上記の変数を切り替える構造のみ用意)

## 実装した機能

タイトル画面/対戦画面(HP・ラウンド・タイマー・ゲージ表示)/リザルト画面/
REMATCH・TITLE遷移/State Machine(Idle/WalkForward/WalkBackward/Crouch/
Jump/Attack/Block/Hitstun/Knockdown/WakeUp/Throw/Dead)/Hitbox・Hurtbox
(立ち・しゃがみ・空中)・Pushboxの分離/Startup→Active→Recoveryのフレーム
管理/ヒットストップ/ガード判定(High/Low/Overhead/Throw)/削りダメージ/
ノックバック/ダウンと自動起き上がり/打撃・投げ・完全無敵/通常技→必殺技→
超必殺技のキャンセル/入力バッファ+コマンド認識(236/623/236236)/飛び道具/
スーパーゲージ/CPU AI(距離に応じた行動選択)/キャラクターエディタ(JSON永続化)/
デバッグ表示(F1: Hitbox/Hurtbox/Pushbox/State/Move/Frame/HP/Gauge/Velocity/
Hitstun/Blockstun)/60FPS基準の固定タイムステップ管理

## 未実装・簡略化した機能

- グラフィックは全て図形によるプレースホルダー
- サウンドは無音同梱(.wav再生の仕組みのみ用意)
- Hard Knockdown/Launch/Wall Bounce/Ground Bounceはデータ区分のみで、挙動は現状「通常ノックダウン」相当
- Best of 3は未実装(1本先取で試合終了)
- キャラクターセレクト画面は未実装(固定値、上記参照)
- キャラクターはRYU 1体のみ同梱(複数キャラ対応の構造はあります)
- `.exe`化はしていません(理由は6項)

## 動作確認について(重要)

本開発はLinuxのサンドボックス環境で行っており、**実機Windowsでの動作確認は
できていません**。そのため、以下の2段階で可能な限りの検証を行いました。

1. **戦闘ロジック(画面表示に依存しない部分)**: `Character/`・`MoveData/`・
   `BattleSystem/`・`InputSystem/`・`CPUAI/`・`Data/` は`System.Windows.Forms`
   に一切依存しないため、開発環境に**PowerShell 7を用意して実際に実行**し、
   1試合を最後まで自動シミュレーション(ヒット判定・ダメージ・ノックダウン・
   起き上がり・ゲージ・KOまで)、ガードルール9パターン、コマンド認識と
   キャンセル、キャラクターエディタのデータ保存/再読み込みが正しく動作
   することを確認済みです。実行方法:
   ```powershell
   cd winforms-game
   pwsh -NoProfile -File tests\HeadlessLogicTest.ps1
   ```
   (`pwsh`が無ければ Windows PowerShell の `powershell.exe` でも動作するはずです)

2. **UI/描画部分(`UI/`, `CharacterEditor/`, `Main.ps1`)**: `System.Windows.Forms`
   はWindows専用のため、この環境では実行できません。代わりに全ファイルの
   構文解析(AST parse)と、依存関係順での読み込み(dot-source)が
   エラーなく完了することを確認し、WinFormsの既知の落とし穴
   (`DoubleBuffered`のリフレクション取得、イベントハンドラのクロージャで
   関数入れ子が見えなくなる問題など)は個別に検証・修正済みです。ただし
   **実際の画面表示・キー入力・マウス操作の動作は未確認**です。

起動して何かエラーが出た場合は、`error.log`(起動フォルダに自動生成)の
内容、またはメッセージボックスに表示されたエラー内容をそのまま教えて
いただければ、それを元に修正します。
