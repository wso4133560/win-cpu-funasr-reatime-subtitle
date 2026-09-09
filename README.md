# Windows CPU FunASR 实时字幕

Windows 系统声音 → WASAPI Loopback → 常驻 FSMN-VAD / SenseVoice CPU → 透明置顶字幕。
运行时只有一个 `FunASRSubtitle.exe` 进程，无 Python、HTTP 服务、识别子进程或 GPU 依赖。

## 构建与运行

需要 Windows 10 1903 或更新版本 / Windows 11 x64、支持 AVX2 的 CPU、Visual Studio 2022 C++ 桌面开发工具和 PowerShell 7。
依赖、模型、构建产物都保存在项目目录内。Git 不包含大型模型、录音或识别日志。

## 模型下载

程序需要以下两个 GGUF 模型，必须放在项目根目录的 `models/` 下：

| 文件 | 用途 | SHA256 |
| --- | --- | --- |
| `sensevoice-small-q8.gguf` | SenseVoiceSmall 中英文语音识别 | `4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5` |
| `fsmn-vad.gguf` | FSMN 语音活动检测 | `1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479` |

联网时单独下载模型：

```powershell
pwsh -File scripts/download-models.ps1
```

脚本从 [SenseVoiceSmall-GGUF](https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF)
和 [fsmn-vad-GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF) 下载，下载到 `.part`
文件后校验 SHA256，校验成功才改名为正式模型文件。已存在且校验正确的模型会跳过下载。

如果目标机器不能联网，可以先把两个同名文件放到本地缓存目录，再执行：

```powershell
pwsh -File scripts/download-models.ps1 -ModelSource D:\path\to\funasr-models
```

`bootstrap.ps1` 会自动调用这个模型脚本，同时下载项目锁定的 llama.cpp 依赖；只需要完整初始化时执行：

```powershell
pwsh -File scripts/bootstrap.ps1
# 已有本地模型缓存时：
# pwsh -File scripts/bootstrap.ps1 -ModelSource ..\gguf
pwsh -File scripts/build.ps1
.\build\Release\FunASRSubtitle.exe
```

制作完整安装包（需要安装 NSIS）：

```powershell
pwsh -File scripts/package-installer.ps1
```

安装包输出到 `install/FunASRSubtitle-Setup.exe`，包含运行程序、SenseVoiceSmall
识别模型、FSMN-VAD 模型、应用图标、README、第三方说明、开始菜单和桌面快捷方式以及卸载程序。
安装默认使用当前用户目录 `%LOCALAPPDATA%\Programs\FunASRSubtitle`，不要求管理员权限；
字幕日志和设置保存在安装目录中。安装包旁边会生成同名 `.sha256` 校验文件。
GitHub Release 的安装包如果因网络限制无法一次下载，可下载 Release 中的全部
`FunASRSubtitle-Setup.exe.partNN` 文件、`FunASRSubtitle-Setup.exe.sha256` 和
`join-installer.ps1`，放在同一目录后执行：

```powershell
pwsh -File .\join-installer.ps1 -InputDirectory . -OutputPath .\FunASRSubtitle-Setup.exe
```

脚本会按编号合并分卷并自动校验 SHA256，校验通过后再运行安装程序。

程序向上查找包含 `models/sensevoice-small-q8.gguf` 的目录。打包时将 EXE、`models/`、
第三方许可证放在同一目录即可。CPU 图和音频转换均在本进程完成。

## 使用

- 播放浏览器、视频、会议软件的声音，字幕自动显示。采集当前默认输出设备的混音，不录制麦克风。
- 启动后立即显示半透明置顶字幕栏；没有声音时显示等待提示。
- 拖动字幕面板调整位置，右键字幕栏或托盘图标打开菜单。
- 再次打开 EXE、左键点击托盘图标或按 `Ctrl+Alt+S`，可将字幕栏恢复到鼠标所在屏幕下方，同时解除点击穿透。
- 右键字幕栏或托盘图标 → **CPU recognition threads**，可选择 1 / 2 / 4 / 8 / 12 / 16 / 24 / 32 个线程；选择后自动重启识别流水线。
- `Ctrl+Alt+P`：暂停 / 继续；`Ctrl+Alt+L`：点击穿透 / 解锁；`Ctrl+Alt+Q`：退出。
- 菜单可调字号、重启采集及模型、打开字幕目录。
- `logs/` 按会话保存最终 SRT、含临时修订的 TSV，以及 VAD 产生的 `.segments.tsv`。
  TSV 中的 `<ASR_EMPTY>` 表示已经检测到语音片段但 ASR 没有返回文字，可用来区分 VAD 漏检和模型空结果；
  如果最终重识别为空但之前的临时结果有文字，程序会保留最后一个临时结果生成最终字幕。本地保存，不上传。
- `settings.ini` 可设 `[engine] threads=8`，`[overlay] font=28`、`opacity=175`（30–240）。

## 实现边界

SenseVoice 是非流式模型。本程序在语音未结束时约每秒重新识别当前句并替换临时字幕，
检测到约 400 ms 非语音后提交最终句；连续语音的强制切句上限为 12 秒，并在切点保留 200 ms
上下文给下一段。它不是逐词稳定的流式解码器。
字幕延迟取决于当前句长度、模型耗时及音频设备。不能把文件转写倍率直接当作实时端到端延迟。

VAD 模型常驻，使用 800 ms 左侧音频上下文、40 ms 前瞻及每 160 ms 更新的有限窗口，
重新计算窗口内 FSMN 概率，不是保存每一层卷积缓存的原生流式 VAD。临时任务只保留最新修订；
最终句队列最多 4 条，过载会提示并丢弃最旧待处理句，避免无界延迟。

静音时部分设备不回调，程序会补静音来结束句尾。设备切换或缓冲溢出会清除上下文并丢弃过期结果。
不支持受 DRM 保护的不可回放采集声音。英文质量需要用更多自然语音验证；JFK 的 22 词样本仅作回归用。
数字静音门限和模型非语音标签用于减少误触发，音乐及系统提示音仍可能产生误识别。
当前版本原先的 8 秒强制句界可能切断词语；修复版改为 12 秒并在边界保留 200 ms，
仍不宣称无缝跨句拼接。

本机已完成短时 GUI + 真实系统回放测试。具体数据与未验证项见 [验证记录](docs/VALIDATION.md)。

## 验证工具

```powershell
# 同一模型实例连续识别 3 次，报告加载和每次推理耗时
.\build\Release\subtitle_bench.exe --audio ..\english-test.wav
# 经过有限窗口 VAD、临时修订和最终分段的文件回放验证
.\build\Release\subtitle_bench.exe --audio ..\english-test.wav --stream
# 真正播放音频并通过 WASAPI loopback 识别（会从扬声器出声）
.\build\Release\subtitle_bench.exe --live 15 --audio ..\english-test.wav
python scripts/wer.py tests/english-reference.txt test-output/live-final.txt
```

GUI 布局可离线渲染：`FunASRSubtitle.exe --render-preview preview.png`，不启动采集。
构建脚本还会启动隔离的 GUI 测试实例（不加载模型、不采集声音），检查隐藏启动、透明图层、
重复打开、移出屏幕和最小化后的恢复。测试实例自动退出，不干扰正在运行的正式实例。

若提示已经运行却看不到字幕，请先按 `Ctrl+Alt+S` 或再次打开 EXE。若仍提示无法连接已有实例，
在任务管理器中结束 `FunASRSubtitle.exe` 后，从资源管理器重新打开。
GUI 必须运行在用户的交互桌面；自动化工具的隔离桌面内即使窗口标记为可见，用户也看不到。
诊断命令 `build\Release\overlay_tests.exe --inspect-running` 可输出调用环境所在桌面、当前桌面
是否有字幕窗口、进程号和窗口位置；须在用户正常桌面终端执行。

源代码来源和修改范围见 [vendor/NOTICE.md](vendor/NOTICE.md)。

