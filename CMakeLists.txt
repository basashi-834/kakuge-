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
if(NOT DEFINED SDL2_DIR AND NOT VCPKG_TOOLCHAIN)
    file(GLOB _kakuge_sdl2_candidates
         "${CMAKE_CURRENT_SOURCE_DIR}/SDL2"
         "${CMAKE_CURRENT_SOURCE_DIR}/SDL2-[0-9]*"
         "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2"
         "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2-[0-9]*")
    set(_kakuge_sdl2_found FALSE)
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

# ここで「Could NOT find SDL2」と言われたら、原因はほぼ次のどれかです:
#   - ルートA: SDL2 フォルダの置き場所が違う（cmake\sdl2-config.cmake が
#     入っているフォルダを、この CMakeLists.txt の隣に置いてください）
#   - ルートB: vcpkg install がまだ / -DCMAKE_TOOLCHAIN_FILE= の指定漏れ
#   - build フォルダに古い設定が残っている（build を削除してやり直す）
find_package(SDL2 CONFIG REQUIRED)

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
    src/platform/Renderer.cpp
    src/platform/Font.cpp
    src/platform/Figure.cpp
    src/platform/Camera.cpp
    src/platform/Hud.cpp
    src/platform/Paths.cpp
    src/platform/Game.cpp
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
    add_executable(EngineTests tests/EngineTests.cpp src/core/Json.cpp)
    target_include_directories(EngineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
