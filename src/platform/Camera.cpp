// =====================================================================
// platform/Camera.cpp - カメラの計算
// =====================================================================
#include "platform/Camera.h"

#include <algorithm>
#include <cmath>

namespace kakuge {

namespace {

// 目標へ近づく速さ（1 秒あたりの割合）。
//
// この値の決め方は、次の 2 つの板挟みです。
//   大きすぎる … カメラが中間点にぴったり張り付き、歩くたびに
//                 背景がガタガタ動いて酔いやすくなる
//   小さすぎる … ダッシュや吹き飛びにカメラが付いてこられず、
//                 キャラクターが画面端に押し付けられて見えなくなる
// 8.0 は「少し滑らかだが、入力には十分ついてくる」あたりです。
// （1/60 秒で目標との差の約 12% を詰め、約 0.12 秒でほぼ到達します）
constexpr double kCameraLerpSpeed = 8.0;

GameCamera g_Camera;

// カメラの中心を、ステージの外が映らない範囲に収める。
//
// これが無いと、片方が画面端に追い詰められたとき、2 人の中間点を
// そのまま中心にするせいで、端の向こうの何もない空間が映ります。
// 本物の格闘ゲームのカメラは、そういうとき中間点の追跡をやめて
// 壁に張り付きます。その動きを再現しています。
// これが「画面端に追い詰めた」という状況を作る仕組みでもあります。
double ClampCameraCenter(double centerX) {
    const double halfVisible = VirtualW / 2.0; // キャンバス幅の半分
    double minCenter = StageConstants::StageMinX + halfVisible;
    double maxCenter = StageConstants::StageMaxX - halfVisible;
    // ステージが画面より狭い場合は中央固定（今の設定では起きません）。
    if (minCenter > maxCenter) {
        return (StageConstants::StageMinX + StageConstants::StageMaxX) / 2.0;
    }
    return std::clamp(centerX, minCenter, maxCenter);
}

// 2 人ともが画面に収まる範囲へカメラを押し戻す。
//
// デッドゾーンだけに任せると、2 人が左右に離れたとき
// 「中間点は帯の中にあるのでカメラは動かないが、離れた側は
// 画面の外」という状態が作れてしまいます。
// （デッドゾーンの半分 70 ＋ 2 人の距離の半分 152 ＝ 222 で、
//   画面中央から 192 までしか映らないので 30 ほどはみ出す）
//
// そこで最後に、左のキャラクターの絵の左端と、右のキャラクターの
// 絵の右端が、どちらも画面の中に入る位置までカメラを寄せます。
// 「2 人の距離の上限（MaxPlayerDistance）＝ 画面幅 － 絵 1 体ぶん」
// と決めてあるので、この 2 つの条件は必ず同時に満たせます。
double ClampBothVisible(double centerX, double leftX, double rightX) {
    const double halfVisible = VirtualW / 2.0;
    const double margin = StageConstants::PlayerScreenMargin; // 絵の半分（40）
    // 右のキャラクターが画面の右端より内側に入るための下限、
    // 左のキャラクターが画面の左端より内側に入るための上限。
    double minCenter = rightX + margin - halfVisible;
    double maxCenter = leftX - margin + halfVisible;
    // 2 人が離れすぎていて両立できないとき（画面が基準より狭い場合など）は
    // 中間点を映して、少なくとも両方が同じだけはみ出すようにします。
    if (minCenter > maxCenter) return (leftX + rightX) / 2.0;
    return std::clamp(centerX, minCenter, maxCenter);
}

// デッドゾーンを考慮して、カメラが目指すべき位置を求める。
//
//   中間点がデッドゾーンの中  → 今の位置のまま（カメラは動かない）
//   はみ出した               → はみ出したぶんだけ動く
//
// 「はみ出したぶんだけ」なので、帯の縁に中間点が貼り付く形になり、
// カメラの動き出しが急にならず自然につながります。
// 最後に「2 人とも画面内」「ステージの外を映さない」の 2 つで補正します。
double CameraTargetX(double p1x, double p2x, double currentCenterX) {
    double midpoint = (p1x + p2x) / 2.0;
    const double half = StageConstants::CameraDeadZoneWidth / 2.0; // 70
    double diff = midpoint - currentCenterX;
    double target = currentCenterX;
    if (diff > half) target = midpoint - half;
    else if (diff < -half) target = midpoint + half;

    // 「2 人とも画面内」→「ステージの外を映さない」の順に補正します。
    // ステージ端の制限をあとに掛けるのは、そちらを必ず優先させるため
    // です（端の向こうの何もない空間が映るほうが不自然なので）。
    target = ClampBothVisible(target, std::min(p1x, p2x), std::max(p1x, p2x));
    return ClampCameraCenter(target);
}

} // namespace

GameCamera& GetCamera() { return g_Camera; }

double CameraDrawX() { return std::round(g_Camera.CenterX); }

void UpdateCamera(double p1x, double p2x, double dt) {
    double target = CameraTargetX(p1x, p2x, g_Camera.CenterX);
    // 目標との差の一定割合ずつ近づける（線形補間）。
    // 毎フレーム差が縮むので、なめらかに減速しながら到達します。
    double t = std::clamp(kCameraLerpSpeed * dt, 0.0, 1.0);
    g_Camera.CenterX += (target - g_Camera.CenterX) * t;

    // 動かしたあとに、もう一度「2 人とも画面内」で押さえます。
    //
    // なめらかに追いかける以上、カメラは必ず少し遅れます。ふだんは
    // それでよいのですが、2 人が上限いっぱいまで離れているときは
    // 画面に 1 ピクセルの余りもないので、その遅れがそのまま
    // 「片方が画面からはみ出す」になってしまいます。
    // そこで、目標を決めるときだけでなく、実際に動かしたあとにも
    // 同じ制限を掛けます。離れ切っている間だけカメラが硬く追従し、
    // それ以外の場面ではこれまでどおり滑らかに動きます。
    double center = ClampBothVisible(g_Camera.CenterX, std::min(p1x, p2x), std::max(p1x, p2x));
    g_Camera.CenterX = ClampCameraCenter(center);
}

void ResetCamera(double p1x, double p2x) {
    // 試合開始時はデッドゾーンを考えず、中間点にまっすぐ合わせます。
    double center = ClampBothVisible((p1x + p2x) / 2.0, std::min(p1x, p2x), std::max(p1x, p2x));
    g_Camera.CenterX = ClampCameraCenter(center);
}

} // namespace kakuge
