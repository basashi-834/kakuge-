# スプライト（手描きの絵）の入れ方

このフォルダに絵を置くと、図形で組み立てたキャラクターの代わりに
その絵が使われます。置かなければ、これまでどおり図形で描かれます。

置き場所は **キャラクター ID ごとのフォルダ** です。

```
data/sprites/ryu/sprites.json   ← どのコマがどの姿勢かを書く
data/sprites/ryu/ryu.bmp        ← 絵をまとめた 1 枚のシート画像
```

`%APPDATA%\Kakuge\sprites\ryu\` に同じものを置くと、そちらが優先されます
（元のデータを触らずに自分の絵を試したいときに使ってください）。

---

## いちばん簡単な始め方

絵を 1 枚も持っていなくても、**今の図形描画をそのまま型紙として
書き出せます**。

```
Kakuge --export-sprites          全キャラクターぶん
Kakuge --export-sprites ryu      ryu だけ
```

`data/sprites/<ID>/` に `<ID>.bmp` と `sprites.json` ができます。
ゲームを起動すると、もうその絵で表示されています（見た目は今までと
同じです。中身が図形から絵に変わっただけです）。

あとはできあがった BMP をお絵かきソフトで開き、コマの上から
描き直していくだけです。1 コマ描き直すたびに、その姿勢だけが
新しい絵に変わっていきます。

> **足元の位置を動かさないこと。**
> ゲームはキャラクターの位置を「足の裏の 1 点」で持っています。
> 書き出した型紙では、それが各コマの左右中央・上から 104 ピクセル目に
> あります。ここがずれると、その姿勢のときだけ絵が浮いたり沈んだり
> します。

---

## 画像の決まり

| 項目 | 内容 |
|---|---|
| 形式 | **BMP**（24bit / 32bit）。SDL2 本体だけで読めるのが理由です |
| 透明 | 既定はマゼンタ（255, 0, 255）。その色の部分が抜けます |
| 並べ方 | 左上から右へ、`columns` 個で次の段へ（格子状） |
| 向き | **必ず右向きで描く**。左向きは自動で反転して表示されます |

PNG は使えません（SDL_image という別のライブラリが必要になるため）。
お絵かきソフトの「名前を付けて保存」で BMP を選んでください。

---

## sprites.json の書き方

```json
{
    "sheet": "ryu.bmp",
    "transparent": [255, 0, 255],
    "cellWidth": 160,
    "cellHeight": 136,
    "columns": 8,
    "originX": 80,
    "originY": 104,
    "stateTint": true,
    "animations": {
        "idle":   { "cells": [0, 1, 2, 3], "hold": 8 },
        "crouch": { "cells": [4] },
        "punch":  { "cells": [12, 13, 14], "mode": "phase" },
        "move:fireball": { "cells": [30, 31, 32], "mode": "phase" }
    }
}
```

### 全体の設定

| キー | 意味 |
|---|---|
| `sheet` | シート画像のファイル名（この JSON と同じフォルダ） |
| `transparent` | 透明にする色。`null` と書くと画像の透明度をそのまま使います |
| `cellWidth` / `cellHeight` | 1 コマの大きさ |
| `columns` | 1 段に何コマ並んでいるか |
| `originX` / `originY` | コマの中での足元の位置 |
| `stateTint` | ガード中を青く、食らい中を赤く染めるか（既定 `true`）。絵の色をそのまま出したいときは `false` |

### アニメーション 1 本の設定

| キー | 意味 |
|---|---|
| `cells` | 使うコマの番号（左上が 0、右へ 1, 2, …） |
| `frames` | 番号ではなく切り出し位置を直接書く場合（`x` `y` `w` `h` `originX` `originY`）。大きさの違うコマを混ぜたいとき |
| `hold` | 1 コマを何フレーム表示するか（既定 4） |
| `loop` | 最後まで行ったら先頭へ戻るか（既定 `true`） |
| `mode` | `"time"`（既定・時間で進む）か `"phase"`（技の段階に合わせる） |

`"mode": "phase"` は技むけです。コマを 3 つ書くと、
**発生 → 1 つ目、持続 → 2 つ目、硬直 → 3 つ目** が表示されます。
技のフレーム数（`data/moves/*.json` の `startup` / `active` / `recovery`）を
あとから変えても、絵と動きがずれません。

---

## 用意できる姿勢の名前

必要なものだけ書けば大丈夫です。用意していない姿勢は、
下の「代わりに使うもの」の順に降りていき、最後は必ず `idle` になります
（つまり **立ち絵 1 枚だけでも動きます**）。

| 名前 | いつ使われるか | 無いときの代わり |
|---|---|---|
| `idle` | 立ち | （最後の受け皿） |
| `walk_forward` | 前進 | `walk` → `idle` |
| `walk_backward` | 後退 | `walk` → `idle` |
| `crouch` | しゃがみ | `idle` |
| `jump` | ジャンプ中 | `idle` |
| `block` | 立ちガード | `idle` |
| `block_crouch` | しゃがみガード | `block` → `crouch` → `idle` |
| `hitstun` | 食らい（地上） | `idle` |
| `hitstun_air` | 食らい（空中） | `hitstun` → `jump` → `idle` |
| `throw` | 投げられ中 | `hitstun` → `idle` |
| `knockdown` | ダウン | `dead` → `hitstun` → `idle` |
| `wakeup` | 起き上がり | `crouch` → `idle` |
| `dead` | KO | `knockdown` → `hitstun` → `idle` |
| `punch` / `kick` | 立ち技 | `idle` |
| `crouch_punch` / `crouch_kick` | しゃがみ技 | `punch` / `kick` → `idle` |
| `jump_punch` / `jump_kick` | 空中技 | `punch` / `kick` → `idle` |
| `move:<技のID>` | その技だけの専用の絵 | 上の姿勢＋パンチ / キック |

技に専用の絵を付けたいときは `move:` に技の ID を続けて書きます
（例: `"move:fireball"`, `"move:super_combo"`）。技の ID は
`data/moves/<キャラクターID>/` のファイル名と同じです。
