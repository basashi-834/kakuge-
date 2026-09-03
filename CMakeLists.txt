# =====================================================================
#  Kakuge - 2D 対戦格闘ゲーム (SDL2 版) のビルド設定ファイル
# =====================================================================
#  このファイルは「CMake」という道具への指示書です。
#  CMake は、皆さんが書いた C++ のソースコードを、Visual Studio が
#  理解できる形（.sln / .vcxproj）に翻訳してくれる道具です。
#
#  皆さんが実際に打つコマンドは 2 つだけです（詳しくは BUILD_WINDOWS.md）:
#
#    1) cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
#       → このファイルを読んで build フォルダに Visual Studio 用の
#         プロジェクトを作る（「設定」の作業）
#
#    2) cmake --build build --config Release
#       → 実際にコンパイルして .exe を作る（「ビルド」の作業）
#
#  使う外部ライブラリは SDL2 だけです。SDL2 は「ウィンドウを出す」
#  「キーボードを読む」「絵を描く」を担当してくれるライブラリで、
#  vcpkg というパッケージ管理ツールで入れます。
# =====================================================================

# CMake のバージョン下限。3.21 以上を要求しているのは、下のほうで使う
# $<TARGET_RUNTIME_DLLS:...>（必要な DLL を自動で見つけてくれる機能）が
# 3.21 で入ったからです。Visual Studio 2022 に付属する CMake は 3.2x
# なので、そのまま条件を満たします。
cmake_minimum_required(VERSION 3.21)

# プロジェクト名とバージョン。ここで付けた名前が Visual Studio の
# ソリューション名（Kakuge.sln）になります。
project(Kakuge VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------
# C++ の言語バージョン
# ---------------------------------------------------------------------
# C++17 を使います（要件で決めた通り）。std::filesystem や
# 構造化束縛など、このプロジェクトで実際に使っている機能が C++17 です。
# CXX_STANDARD_REQUIRED を ON にしておくと、もしコンパイラが C++17 に
# 対応していない場合に「黙って古い規格でビルドする」のではなく、
# はっきりエラーで止まってくれます。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------
# MSVC（Visual Studio のコンパイラ）向けの設定
# ---------------------------------------------------------------------
if(MSVC)
    # /utf-8 : このプロジェクトのソースコードには日本語のコメントが
    #          たくさん入っています。これを付けないと MSVC は日本語を
    #          「その PC の地域設定の文字コード」で読もうとして、
    #          環境によっては文字化けやコンパイルエラーになります。
    #          「ソースは UTF-8 で書いてあるよ」と教える指定です。
    # /W4    : 警告のレベルを高めにして、あやしいコードを教えてもらう。
    add_compile_options(/utf-8 /W4)
    # MSVC が自動で付ける古い関数への警告（sprintf は危険です等）は
    # このプロジェクトでは不要なので黙らせます。
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # Linux / macOS （開発中の動作確認用）
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------
# 出力先（できあがった .exe が置かれる場所）
# ---------------------------------------------------------------------
# 何も指定しないと Visual Studio は build/Release/ や build/Debug/ と
# いった構成ごとのフォルダに .exe を作り、初めての人が「.exe どこ？」と
# 迷いがちです。そこで、どの構成でビルドしても必ず
#   build/bin/  （Release も Debug も同じ場所）
# に出るようにそろえます。SDL2.dll と data フォルダも、あとの
# add_custom_command でこの隣にコピーします。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# ---------------------------------------------------------------------
# SDL2 を探す
# ---------------------------------------------------------------------
# SDL2 の入手方法は 2 通りあり、どちらでも下の find_package で見つかります。
#
#  ルートA（Git 不要・おすすめ）:
#      libsdl.org から SDL2-devel-2.xx.x-VC.zip をダウンロードして解凍し、
#      できた SDL2-2.xx.x フォルダを、この CMakeLists.txt と同じ場所
#      （または 1 つ上の階層）に置くだけ。下の自動検出が拾います。
#
#  ルートB（vcpkg を使う。Git が必要）:
#      「.\vcpkg install sdl2:x64-windows」を済ませ、設定コマンドに
#      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
#      を付ける。
#
# ---- ルートA のための自動検出 ----
# vcpkg を使っていないときだけ、近くに置かれた SDL2 フォルダを探します。
# 公式の VC 版 zip には cmake\sdl2-config.cmake が同梱されているので、
# それがあるフォルダを CMAKE_PREFIX_PATH に足せば find_package が
# 見つけてくれます（この 1 手間で -D オプションが不要になります）。
# 条件が「NOT SDL2_DIR」であって「NOT DEFINED SDL2_DIR」でないのが重要です。
# find_package が一度失敗すると、SDL2_DIR には "SDL2_DIR-NOTFOUND" という
# 値がキャッシュに残ります。この状態は「定義済み」なので、
# NOT DEFINED で判定すると 2 回目以降この探索が丸ごと飛ばされ、
# SDL2 を正しい場所に置いても永久に見つからなくなります。
# CMake では末尾が -NOTFOUND の値は偽なので、NOT SDL2_DIR なら
# 「未設定」と「見つからなかった」の両方を拾えます。
set(_kakuge_sdl2_found FALSE)
if(NOT SDL2_DIR AND NOT VCPKG_TOOLCHAIN)
    file(GLOB _kakuge_sdl2_candidates
         "${CMAKE_CURRENT_SOURCE_DIR}/SDL2"
         "${CMAKE_CURRENT_SOURCE_DIR}/SDL2-[0-9]*"
         "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2"
         "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2-[0-9]*")
    foreach(_cand IN LISTS _kakuge_sdl2_candidates)
        if(EXISTS "${_cand}/cmake/sdl2-config.cmake")
            message(STATUS "SDL2 をこの場所で見つけました: ${_cand}")
            list(APPEND CMAKE_PREFIX_PATH "${_cand}")
            set(_kakuge_sdl2_found TRUE)
            break()
        endif()
    endforeach()

    # よくある間違い: SDL2 のつもりで SDL3 を落としてきてしまった場合。
    # 公式サイトの「最新リリース」は今 SDL3 なので、ふつうに探すと
    # SDL3 のほうに行き着きます。放っておくと下の find_package が
    # 「Could NOT find SDL2」とだけ言って止まり、原因が分かりません。
    # SDL2 と SDL3 は関数名から違う別物なので、入れ替えでは動きません。
    if(NOT _kakuge_sdl2_found)
        file(GLOB _kakuge_sdl3_candidates
             "${CMAKE_CURRENT_SOURCE_DIR}/SDL3*"
             "${CMAKE_CURRENT_SOURCE_DIR}/../SDL3*")
        if(_kakuge_sdl3_candidates)
            message(WARNING
                "SDL3 のフォルダが見つかりましたが、このゲームに必要なのは SDL2 です。\n"
                "  見つかった場所: ${_kakuge_sdl3_candidates}\n"
                "SDL3 は SDL2 の後継ですが、関数名から異なる別物なので入れ替えでは動きません。\n"
                "次のリンクから SDL2 の開発用パッケージを取得してください:\n"
                "  https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip\n"
                "詳しくは BUILD_WINDOWS.md の「手順 A-1」を参照してください。")
        endif()
    endif()
endif()

# ---- SDL2 を探す ----
# QUIET で一度試し、見つからなかったときだけ自前のエラーを出します。
#
# いきなり REQUIRED にしないのは、find_package の既定のエラーが
# 「SDL2Config.cmake が無い」としか言わず、初めての人には何をすれば
# よいか分からないからです。かといって find_package の前で自前に
# 判定してしまうと、Linux のようにシステムに SDL2 が入っている環境
# （SDL2_DIR を指定しなくても見つかる）を誤って弾いてしまいます。
# 「まず探させて、駄目だったときだけ説明する」のが正しい順番です。
find_package(SDL2 CONFIG QUIET)

if(NOT SDL2_FOUND)
    # 実際に何が置かれているかを一覧にして見せる
    file(GLOB _kakuge_nearby RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/*")
    set(_kakuge_dirs "")
    foreach(_e IN LISTS _kakuge_nearby)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_e}")
            string(APPEND _kakuge_dirs "    ${_e}\n")
        endif()
    endforeach()
    message(FATAL_ERROR
        "SDL2 が見つかりませんでした。\n"
        "\n"
        "このゲームは SDL2 の「開発用パッケージ」を必要とします。\n"
        "次のどちらかを行ってから、もう一度やり直してください。\n"
        "\n"
        "【ルートA】SDL2 を直接置く（Git 不要・おすすめ）\n"
        "  1. 次のリンクからダウンロード（7MB ほど）:\n"
        "     https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip\n"
        "  2. 解凍してできた SDL2-2.32.10 フォルダを、次の場所に置く:\n"
        "     ${CMAKE_CURRENT_SOURCE_DIR}\n"
        "     （CMakeLists.txt と同じ階層です）\n"
        "  3. 置いたフォルダの直下に cmake\\sdl2-config.cmake があることを確認\n"
        "     ※ 二重フォルダ（SDL2-2.32.10\\SDL2-2.32.10\\...）になりがちです\n"
        "\n"
        "【ルートB】vcpkg を使う（Git が必要）\n"
        "  .\\vcpkg install sdl2:x64-windows を実行し、設定コマンドに\n"
        "  -DCMAKE_TOOLCHAIN_FILE=C:\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake を付ける\n"
        "\n"
        "--- 今この場所にあるフォルダ ---\n"
        "${_kakuge_dirs}"
        "\n"
        "※ 置き直したのにこのエラーが出る場合は、build フォルダを\n"
        "  まるごと削除してから、もう一度 cmake -B build -S . を実行してください。")
endif()

# SDL_RenderGeometry（三角形を並べて自由な形を塗る関数。このゲームの
# キャラクター描画の土台）は SDL 2.0.18 で追加された関数です。
# それより古い SDL2 だとコンパイルは通ってもリンクで失敗するので、
# 分かりにくいエラーになる前にここではっきり止めます。
if(DEFINED SDL2_VERSION AND SDL2_VERSION VERSION_LESS "2.0.18")
    message(FATAL_ERROR
        "SDL2 ${SDL2_VERSION} が見つかりましたが、このゲームは SDL_RenderGeometry を使うため "
        "SDL2 2.0.18 以降が必要です。vcpkg を更新してから "
        "'.\\vcpkg upgrade sdl2:x64-windows' を実行してください。")
endif()

# ---------------------------------------------------------------------
# ゲーム本体のソースファイル一覧
# ---------------------------------------------------------------------
# ここに並べたファイルがコンパイルされます。ファイルを新しく足したら
# この一覧にも追記して、設定コマンド（手順 1）をやり直してください。
# ※ engine/ の中身はほとんどがヘッダ (.h) だけで完結しているので、
#    .cpp が並んでいるのは主に platform/（SDL2 を触る層）です。
set(KAKUGE_SOURCES
    src/main.cpp
    src/core/Json.cpp
    src/core/TrueType.cpp
    src/platform/Renderer.cpp
    src/platform/Font.cpp
    src/platform/Figure.cpp
    src/platform/Camera.cpp
    src/platform/Gamepad.cpp
    src/platform/Hud.cpp
    src/platform/Paths.cpp
    src/platform/Game.cpp
    src/platform/Editor.cpp
    src/platform/Screens.cpp
)

# WIN32 を付けると「コンソール（黒い画面）を出さないアプリ」になります。
# Windows 以外（Linux での動作確認）では無視されます。
add_executable(Kakuge WIN32 ${KAKUGE_SOURCES})

# #include "engine/Fighter.h" のように src/ からの相対パスで書けるように
# src/ をインクルードパスに追加します。
target_include_directories(Kakuge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# SDL2 をリンク（＝ゲームと結びつける）します。
#   SDL2::SDL2main - Windows で必要な起動処理を肩代わりしてくれるもの。
#                    これのおかげで WinMain ではなく普通の main() が
#                    書けます。Linux には存在しないので
#                    TARGET_NAME_IF_EXISTS で「あれば使う」にしています。
#   SDL2::SDL2     - SDL2 本体。
target_link_libraries(Kakuge PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
)

# ---------------------------------------------------------------------
# ビルド後の自動コピー（ここが「追加の作業は不要」の正体です）
# ---------------------------------------------------------------------
# ゲームは起動時に、実行ファイルの隣にある data フォルダから
# キャラクターや技のデータ（JSON）を読み込みます。また Windows では
# SDL2.dll が実行ファイルの隣にないと「SDL2.dll が見つかりません」と
# 言われて起動できません。
# そこでビルドが終わった直後に、CMake 自身にコピーさせます。

# (1) data フォルダを丸ごと .exe の隣へ
add_custom_command(TARGET Kakuge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:Kakuge>/data"
    COMMENT "data フォルダを実行ファイルの隣にコピーしています")

# (2) 必要な DLL（SDL2.dll など）を .exe の隣へ。
#     $<TARGET_RUNTIME_DLLS:Kakuge> が「このアプリの実行に必要な DLL」を
#     CMake に自動で列挙させる書き方です。Windows のときだけ動きます。
if(WIN32)
    add_custom_command(TARGET Kakuge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:Kakuge>" "$<TARGET_FILE_DIR:Kakuge>"
        COMMAND_EXPAND_LISTS
        COMMENT "SDL2.dll を実行ファイルの隣にコピーしています")
endif()

# Visual Studio で F5 を押したときに、.exe と同じフォルダを作業フォルダに
# しておきます（そうしないと data フォルダを見つけられません）。
set_target_properties(Kakuge PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Kakuge>")

# ---------------------------------------------------------------------
# エンジンの単体テスト（任意）
# ---------------------------------------------------------------------
# ゲーム画面を出さずに、戦闘ロジック（当たり判定・フレーム処理など）
# だけを検証する小さなテストです。SDL2 を一切使わないので、
# ウィンドウが出ない環境でも走ります。
# 既定で ON ですが、不要なら設定時に -DKAKUGE_BUILD_TESTS=OFF を付けて
# ください。ビルド後は build/bin/EngineTests.exe を実行するだけです。
option(KAKUGE_BUILD_TESTS "エンジンの単体テストもビルドする" ON)
if(KAKUGE_BUILD_TESTS)
    add_executable(EngineTests tests/EngineTests.cpp src/core/Json.cpp src/core/TrueType.cpp)
    target_include_directories(EngineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # コントローラのテストだけは SDL2 を使います（仮想ジョイスティックを
    # 作るのに SDL の機能が要るためです）。実物のコントローラは不要です。
    add_executable(GamepadTests tests/GamepadTests.cpp src/platform/Gamepad.cpp)
    target_include_directories(GamepadTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(GamepadTests PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        SDL2::SDL2
    )
endif()
