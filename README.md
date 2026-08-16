# Program Standby Source

OBS Studioのプログラム出力へシーンが切り替わった瞬間に、動画を先頭から自動再生するメディアソースプラグインです。プログラムから外れると再生位置を先頭へ戻し、次の切り替えに備えます。

スタジオモードのPreviewに表示しただけでは再生を始めません。トランジション完了後、実際のProgramに表示されたタイミングで動作します。

## 主な機能

- Programへのカットイン時に先頭から自動再生
- Programからのカットアウト時に停止し、先頭へリセット
- スタジオモードのPreviewでは待機を維持
- ネストしたシーンやグループ内のソースにも対応
- ループ、ハードウェアデコード、ネットワーク入力など、OBS標準のメディアソース相当の設定を利用可能
- 日本語・英語UIに対応

## 動作環境

- OBS Studio 32.2.1
- Windows x64

このプラグインはOBS内部の`media-playback`を利用しているため、異なるOBSバージョンでは再ビルドや追従修正が必要になる場合があります。

## インストール

[Releases](https://github.com/nodoka-kabata/OBS_MediaSource_0frame/releases/latest)から`Program Standby Source-{version}-Windows-x64-Setup.exe`をダウンロードします。OBS Studioを終了した状態でインストーラーを実行してください。管理者権限は不要です。アンインストールはWindowsの「インストールされているアプリ」から行えます。

手動で導入する場合は、ビルド済みファイルをOBSのユーザープラグインフォルダーへ次の構成で配置します。

```text
%PROGRAMDATA%\obs-studio\plugins\program-standby-source\
├─ bin\64bit\program-standby-source.dll
└─ data\locale\
   ├─ en-US.ini
   └─ ja-JP.ini
```

配置後にOBS Studioを再起動してください。読み込みに成功すると、OBSのログに`program-standby-source plugin loaded`と表示されます。

## 使い方

1. OBSの「ソース」欄で`+`を押します。
2. 「メディアソース (Program Standby)」を追加します。
3. 再生するローカルファイル、またはネットワーク入力を設定します。
4. 「プログラムに乗ったら自動再生（0フレーム目でスタンバイ）」を有効にします。
5. そのソースを含むシーンをProgramへ切り替えます。

この設定を有効にすると、競合を避けるため「ソースがアクティブになったときに再生を再開する」は無効になります。ループを有効にした場合は、シーンがProgramに出ている間だけ繰り返し再生します。

> [!NOTE]
> OBS標準の「メディアソース」とは別のソースタイプです。既存のメディアソースへ機能が追加されるわけではないため、利用したい箇所では本プラグインのソースを新しく追加してください。

## ビルド

### 必要なもの

- Visual Studio 2022（C++デスクトップ開発ワークロード）
- Windows 10/11 SDK
- CMake 3.28以降
- インターネット接続（初回構成時にOBS Studioと依存ファイルを取得します）

リポジトリのルートで次を実行します。

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix release/RelWithDebInfo
```

インストール用のファイルは`release/RelWithDebInfo/program-standby-source/`に生成されます。

### Windowsインストーラー

[Inno Setup 6](https://jrsoftware.org/isdl.php)をインストールした環境で、次を実行します。プラグインのReleaseビルド、ステージング、セットアップEXE生成をまとめて行います。

```powershell
.\installer\build-installer.ps1
```

生成物は`dist/Program Standby Source-{version}-Windows-x64-Setup.exe`です。バージョンと製品名は`buildspec.json`から取得されます。

## テスト

シーンの再帰探索と状態遷移はlibobsに依存しないため、単独でテストできます。Visual Studioの「x64 Native Tools Command Prompt」で次を実行してください。

```bat
cl /std:c11 /I src src\tests\test_scene_membership.c src\scene-membership.c /Fe:test_scene_membership.exe
test_scene_membership.exe

cl /std:c11 /I src src\tests\test_standby_state.c src\standby-state.c /Fe:test_standby_state.exe
test_standby_state.exe
```

成功時は、それぞれ`all assertions passed`と表示されます。

ビルド済みDLLがインストール済みOBSのランタイムDLLと互換であることは、次で確認できます。Windowsインストーラー生成時にも同じ検証が自動実行されます。

```powershell
.\installer\tests\validate-obs-compatibility.ps1
```

## 構成

```text
src/
├─ plugin-main.c            プラグインの登録処理
├─ program-standby-source.c メディア再生とProgram切り替えの連携
├─ scene-membership*.c      ネストしたシーン・グループの探索
├─ standby-state.c          カットイン・カットアウトの状態遷移
└─ tests/                   libobs非依存の単体テスト
```

実装はOBS Studio 31.1.1の`obs-ffmpeg-source.c`をベースにし、同じ`media-playback`再生エンジンを使用しています。Program判定には`OBS_FRONTEND_EVENT_SCENE_CHANGED`を利用しています。

## 現在の制限

- 対象はOBSのメインProgram出力です。マルチキャンバスなどの追加出力には対応していません。
- OBS本体の内部APIに依存するため、OBSの更新時に互換性を確認する必要があります。
- 現在のビルド対象はWindows x64です。

## ライセンス

[GNU General Public License v2.0](LICENSE)
