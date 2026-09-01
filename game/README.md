# Kakuge - 2D対戦格闘ゲーム

ストリートファイターのような横視点・1対1の2D対戦格闘ゲーム。今後のキャラクター/技/コンボ/CPU/ステージ追加を見据えて、キャラクター基本性能・状態管理・技データ・フレーム管理・当たり判定・ヒット処理・ガード・ノックバック・キャンセル・入力バッファ・ゲージなどを役割ごとに分離して実装しています。

## 1. 使用技術

**Godot Engine 4.3 (GDScript)** を採用しました。

- **フレーム管理**: Godotの物理フレーム(`_physics_process`)は既定で60Hzに固定でき、格闘ゲームに必須の「1フレーム=1/60秒」の管理とそのまま噛み合う
- **当たり判定**: `Area2D` + `CollisionShape2D`でHitbox / Hurtbox / Pushboxを衝突レイヤーごとに完全分離でき、同一フレーム内で明示的にポーリングできる(`get_overlapping_areas()`)ためヒット処理を決定的に制御できる
- **拡張性**: シーン(`.tscn`)とスクリプト(`.gd`)が1対1で分離される構造のため、キャラクター/戦闘処理/技/入力/UI/エディタを自然にファイル単位で分離できる
- **データ外部化**: JSONの読み書きが標準搭載で、`characters/*.json`・`moves/<id>/*.json`のような外部データ管理をそのまま実現できる
- **Windows配布**: 公式のexport templateを使うことで、Linux上からでもWindows向け`.exe`をクロスビルドできる(本プロジェクトも実際にこの環境からビルド・同梱)

Python + Pygame は当たり判定・フレーム管理を全て自前実装する必要がありWindows向け`.exe`化(PyInstaller)も本開発環境からのクロスビルドが困難なため、Electronはリアルタイムの入力/フレーム精度・当たり判定処理に不向きなため、それぞれ不採用としました。

## 2. 必要環境

### プレイ(完成版)する場合
- Windows 10 / 11 (x86_64)
- 追加ランタイムのインストール不要 (`build/Kakuge.exe` はGodotの.pckを埋め込んだ単一実行ファイル)

### 開発する場合
- [Godot Engine 4.3 (Standard版, .NET不要)](https://godotengine.org/download) — GDScriptのみを使用しているため通常版でOK
- Windows向けにビルドする場合は Godot Editor の Export Templates (4.3.stable) を追加インストール

## 3. プロジェクト構成

```
game/
├─ project.godot            # Godotプロジェクト設定 (autoload / 解像度 / 物理FPSなど)
├─ export_presets.cfg       # Windows向けエクスポート設定
├─ scenes/                  # 画面/オブジェクトのシーン(.tscn)
│   ├─ Title.tscn            # タイトル画面
│   ├─ Game.tscn              # 対戦画面 (MatchController + Player1/Player2 + HUD + DebugOverlay)
│   ├─ Result.tscn            # リザルト画面
│   ├─ CharacterEditor.tscn   # キャラクターエディタ画面
│   ├─ Fighter.tscn           # キャラクター本体 (Hitbox/Hurtbox/Pushbox/Visual)
│   ├─ Projectile.tscn        # 飛び道具
│   └─ HitEffect.tscn         # ヒット/ガード/必殺技/超必殺技の簡易エフェクト
├─ scripts/
│   ├─ autoload/              # シングルトン: DataManager / GameManager / AudioManager
│   ├─ core/                  # 戦闘の中核ロジック
│   │   ├─ Fighter.gd           # キャラクターコントローラ (State/Move/Input/Gaugeを保持し委譲)
│   │   ├─ StateMachine.gd      # 状態管理 (Idle/Walk/Crouch/Jump/Attack/Block/Hitstun/Knockdown/WakeUp/Throw/Dead)
│   │   ├─ MoveExecutor.gd      # 技のフレーム進行 (Startup/Active/Recovery, 無敵, キャンセル可否)
│   │   ├─ InputBuffer.gd       # 直近20フレームの入力履歴
│   │   ├─ CommandParser.gd     # 236/623/236236等のコマンド認識 (多少の入力猶予あり)
│   │   ├─ Hitbox.gd / Hurtbox.gd / Pushbox.gd  # 3種の当たり判定を完全分離
│   │   ├─ SuperGauge.gd        # 0〜100のスーパーゲージ
│   │   ├─ Projectile.gd        # 飛び道具オブジェクト (キャラクター本体と独立)
│   │   ├─ MatchController.gd   # 1試合の進行管理 (フレームごとにFighter.frame_step()を呼び、ヒット/Pushbox解決)
│   │   ├─ DebugOverlay.gd      # デバッグ表示 (F1)
│   │   └─ FighterVisual.gd / HitEffect.gd / ProjectileVisual.gd  # 仮グラフィック(図形描画)
│   ├─ ai/CPUController.gd    # CPU AI (Fighterから完全に分離)
│   ├─ ui/                    # Title/HUD/Result/CharacterEditorの画面スクリプト
│   └─ resources/              # MoveData.gd / CharacterStats.gd (データクラス)
├─ characters/               # キャラクター基本性能 (JSON)
│   └─ ryu.json
├─ moves/                    # 技データ (JSON, キャラクターIDごとのフォルダ)
│   └─ ryu/standing_light.json 他
├─ data/                     # 試合ルール等の共有設定 (match_rules.json)
├─ assets/sprites/           # 実素材を入れる場所 (現状は手続き描画で代用)
├─ ui/ , effects/ , audio/   # 各フォルダの役割はフォルダ内README参照 (Godotの慣習でシーン本体はscenes/に集約)
├─ tests/                    # ヘッドレス自動テスト (下記参照)
└─ build/                    # `Kakuge.exe` (Windows向けビルド成果物)
```

設計方針 (巨大な単一ファイルを避ける):
- Character (`Fighter.gd`) と Move (`MoveData.gd` / `moves/*.json`) を分離
- GameManager (画面遷移・試合設定) と MatchController (1試合のフレーム進行) と Character を分離
- CPU AI (`CPUController.gd`) を Character から分離
- UI (`scripts/ui/*`) と戦闘処理 (`scripts/core/*`) を分離
- 技データは可能な限りJSON化 (ハードコードしない)

## 4. 開発版の起動方法

1. [Godot 4.3](https://godotengine.org/download) をインストール
2. Godotを起動し「Import」から `game/project.godot` を選択
3. エディタ上部の再生ボタン(▶)、またはコマンドラインから:

```bash
godot --path game
```

## 5. Windows向けビルド方法

1. Godot Editor で `Editor > Manage Export Templates` から **4.3.stable** の Export Templates をインストール
2. `game` フォルダを開いた状態で `Project > Export...` を開くと `export_presets.cfg` により **Windows Desktop** プリセットが読み込まれる
3. `Export Project` を押し、出力先を `build/Kakuge.exe` にしてエクスポート

コマンドラインの場合 (CI/自動化向け、本プロジェクトのビルドもこの方法で作成):

```bash
cd game
godot --headless --export-release "Windows Desktop" build/Kakuge.exe
```

`binary_format/embed_pck=true` を設定しているため、`.pck`を別途同梱する必要のない単一の`.exe`が出力されます。

## 6. 完成したゲームの起動方法

`build/Kakuge.exe` をWindows 10/11環境でダブルクリックするだけで起動します。インストール作業は不要です。

## 7. 操作方法

| 操作 | キー |
|---|---|
| 左移動 / 後退 | A |
| 右移動 / 前進 | D |
| しゃがみ | S |
| ジャンプ | Space |
| 弱攻撃 | J |
| 中攻撃 | K |
| 強攻撃 | L |
| 必殺技ボタン | U |
| 超必殺技ボタン | I |
| 投げ (弱+中同時押し) | J + K |
| デバッグ表示ON/OFF | F1 |

- **ガード**: 相手と逆方向(後退方向)を入力すると成立します。しゃがみながら後退でしゃがみガード。
- **必殺技コマンド例 (波動拳)**: `↓ → ↘` の入力後に必殺技ボタン(U)
- **対空必殺技コマンド**: `→ ↓ ↘` の入力後にU (発生直後に打撃無敵あり)
- **超必殺技コマンド**: 波動拳コマンドを2回連続 (`236236`) の入力後にI (ゲージ100必要)
- **キャンセル**: 通常技のキャンセル受付フレーム中に必殺技/超必殺技を入力すると硬直をキャンセルして技を出せます (例: 弱攻撃 → 強攻撃 → 波動拳 → 超必殺技)

## 8. キャラクターエディタの使い方

タイトル画面から **CHARACTER EDIT** を選択して入ります。

1. 左上のドロップダウンで編集するキャラクターを選択
2. 右側のドロップダウンで編集する技を選択
3. 左カラムでキャラクター基本性能 (名前・最大HP・前進/後退速度・ダッシュ速度・ジャンプ力・重力) を編集
4. 右カラムで選択中の技のフレームデータ (発生/持続/硬直・ダメージ・ヒット硬直・ガード硬直・ヒットストップ・ノックバックX/Y・Hitbox位置とサイズ・ガード属性・キャンセル可否・キャンセル先) を編集
   - 画面下部に **On Hit / On Block / TotalFrame** がリアルタイム計算されて表示されます (硬直差の確認)
5. **New Move** ボタンで新しい技スロットを追加できます (後述)
6. **SAVE** ボタンで保存すると `user://characters/*.json` と `user://moves/<id>/*.json` に書き出され、ゲームを再起動しても内容が保持されます (`res://` 側の初期データは上書きせず、常にユーザーデータが優先されます)

保存先の実体 (Windows): `%APPDATA%\Godot\app_userdata\Kakuge\`

## 9. 新しい技を追加する方法

方法A: **キャラクターエディタから追加**
1. CHARACTER EDIT画面で対象キャラクターを選び、**New Move** を押す
2. 生成された技 (`new_move` など) を選択してパラメータを入力し **SAVE**

方法B: **JSONを直接追加**
1. `moves/<character_id>/` フォルダに新しい `.json` ファイルを作成 (`MoveData.gd` の `to_dict()`/`from_dict()` がフィールド一覧です)
2. 最低限 `id` / `name` / `startup` / `active` / `recovery` / `damage` / `guardType` / `hitbox` / `input` / `button` / `stance` を設定
3. 既存の技の `cancelRoutes` にこの技のidを追加すればキャンセルで出せるようになります
4. ゲームを再起動 (または `DataManager.reload_all()`) すると自動的に読み込まれます (コード変更不要)

## 10. 新しいキャラクターを追加する方法

1. `characters/<new_id>.json` を作成 (`ryu.json` をコピーして `id`/`name`/各種速度値を変更)
2. `moves/<new_id>/` フォルダを作成し、そのキャラクター用の技JSON一式を配置 (最低限は `moves/ryu/` をコピーして調整すればすぐ動きます)
3. `scripts/autoload/GameManager.gd` の `selected_character_id` / `cpu_character_id` を変更するか、キャラクターセレクト画面を追加してプレイヤーに選ばせる (現状は選択画面は未実装のため固定値です)
4. コード変更は不要 - `DataManager` が `characters/` `moves/` フォルダを走査して自動的に読み込みます

## 実装した機能

- タイトル画面 (GAME START / CHARACTER EDIT / EXIT)
- 1P vs CPU の対戦画面 (HP・ラウンド表示・99秒タイマー・スーパーゲージ)
- State Machine (Idle/WalkForward/WalkBackward/Crouch/Jump/Attack/Block/Hitstun/Knockdown/WakeUp/Throw/Dead)
- Hitbox / Hurtbox (立ち・しゃがみ・空中で形状変化) / Pushbox の完全分離
- フレーム単位のStartup→Active→Recoveryの技進行、ヒットストップ、キャンセル (通常技→必殺技→超必殺技)
- ヒット/ガード判定 (High/Low/Overhead/Throwのガード属性ルール)、削りダメージの仕組み
- ノックバック、通常ヒットとKnockdownの区別、ダウン後の自動起き上がり
- 打撃無敵/投げ無敵/完全無敵の仕組み (対空必殺技の発生無敵、超必殺技の発動時無敵)
- 入力バッファ+コマンド認識 (236/623/236236, 多少のリニエンシーあり)
- 飛び道具 (本体と独立したオブジェクト)、スーパーゲージ (ヒット/被弾で増加、超必殺技はゲージ消費)
- CPU AI (距離に応じた近距離攻撃/中距離接近/遠距離飛び道具/対空/低HP時の防御的行動、Characterから分離)
- キャラクターエディタ (基本性能+技フレームデータの編集、JSON保存で再起動後も保持)
- デバッグ表示 (F1: Hitbox/Hurtbox/Pushbox/State/Move/Frame/HP/Gauge/Velocity/Hitstun/Blockstun)
- リザルト画面 (PLAYER WIN / CPU WIN / DRAW, REMATCH / TITLE)
- Windows向け`.exe`ビルド (`build/Kakuge.exe`)

## 未実装・簡略化した機能

- グラフィックは全て図形によるプレースホルダー (`scripts/core/FighterVisual.gd` 等) — スプライトは `assets/sprites/README.md` の手順で差し替え可能な設計
- サウンドは無音 (`scripts/autoload/AudioManager.gd` が `audio/*.ogg` を自動再生する仕組みだけ用意、音源ファイル自体は未同梱)
- Hard Knockdown / Launch / Wall Bounce / Ground Bounce はデータ上の区分 (`hitOutcome`) のみ用意されており、現状の挙動はいずれも「通常ノックダウン」として処理されます (個別の物理演出は今後の拡張ポイント)
- Best of 3 は `GameManager.rounds_to_win` としてデータ構造のみ用意 (現状は1本先取で試合終了)
- キャラクターセレクト画面は未実装 (キャラクターは `GameManager.gd` の固定値、複数キャラクターを追加してもエディタでは編集できますが対戦での選択UIはまだありません)
- 1キャラクター(RYU)のみ同梱。複数キャラクター対応の構造 (`characters/`, `moves/<id>/`) はあります
- 本開発環境 (Linux, 表示デバイスなし) では実機Windowsでの最終目視確認ができていません。ゲームシステム自体は `tests/` のヘッドレス自動テストで検証済みです (下記参照)

## 動作確認について

本開発はLinux上のサンドボックス環境で行っており、画面表示・実機Windows実行はできません。そのため、Godotをヘッドレスモードで実際に動かす自動テストを `tests/` に用意し、下記を検証しています。

```bash
cd game
# 1試合をCPU対CPU的に自動プレイし、Hitbox発生・ダメージ・ノックダウン・起き上がり・
# ゲージ増加・KO・リザルトへの遷移までを検証
godot --headless --path . --fixed-fps 600 res://tests/HeadlessSmokeTest.tscn

# ガードルール (High/Low/Overhead/Throw × 立ち/しゃがみ) を検証
godot --headless --path . res://tests/GuardRulesTest.tscn

# コマンド入力認識とキャンセルの成立/不成立を検証
godot --headless --path . res://tests/CommandAndCancelTest.tscn

# キャラクターエディタの保存→再読み込みを検証
godot --headless --path . res://tests/EditorPersistenceTest.tscn
```

いずれも `[TEST] N passed, 0 failed` で終了することを確認済みです。また `godot --headless --editor --quit --path .` でスクリプトのパースエラーが無いことも確認しています。`build/Kakuge.exe` は有効なWindows PE32+実行ファイルとして生成されていることを確認済みですが、実機Windowsでの起動・操作確認はユーザー側での実施をお願いします。
