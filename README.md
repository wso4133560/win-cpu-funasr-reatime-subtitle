# Windows CPU FunASR 实时字幕

Windows 系统声音 → WASAPI Loopback → 常驻 FSMN-VAD / SenseVoice CPU → 透明置顶字幕。
运行时只有一个 `FunASRSubtitle.exe` 进程，无 Python、HTTP 服务、识别子进程或 GPU 依赖。

## 构建与运行

需要 Windows 10 1903 或更新版本 / Windows 11 x64、支持 AVX2 的 CPU、Visual Studio 2022 C++ 桌面开发工具和 PowerShell 7。
依赖、模型、构建产物都保存在项目目录内。Git 不包含大型模型、录音或识别日志。

```powershell
pwsh -File scripts/bootstrap.ps1
# 已有模型时可以避免重复下载：
# pwsh -File scripts/bootstrap.ps1 -ModelSource ..\gguf
pwsh -File scripts/build.ps1
.\build\Release\FunASRSubtitle.exe
```

程序向上查找包含 `models/sensevoice-small-q8.gguf` 的目录。打包时将 EXE、`models/`、
第三方许可证放在同一目录即可。CPU 图和音频转换均在本进程完成。

## 使用

- 播放浏览器、视频、会议软件的声音，字幕自动显示。采集当前默认输出设备的混音，不录制麦克风。
- 拖动字幕面板调整位置，右键或托盘图标打开菜单。
- `Ctrl+Alt+P`：暂停 / 继续；`Ctrl+Alt+L`：点击穿透 / 解锁；`Ctrl+Alt+Q`：退出。
- 菜单可调字号、重启采集及模型、打开字幕目录。
- `logs/` 按会话保存最终 SRT 和含临时修订的 TSV。本地保存，不上传。
- `settings.ini` 可设 `[engine] threads=8`，`[overlay] font=28`、`opacity=175`（30–240）。

## 实现边界

SenseVoice 是非流式模型。本程序在语音未结束时约每秒重新识别当前句并替换临时字幕，
检测到约 400 ms 非语音后提交最终句；单句最长 8 秒。它不是逐词稳定的流式解码器。
字幕延迟取决于当前句长度、模型耗时及音频设备。不能把文件转写倍率直接当作实时端到端延迟。

VAD 模型常驻，使用 800 ms 左侧音频上下文、40 ms 前瞻及每 160 ms 更新的有限窗口，
重新计算窗口内 FSMN 概率，不是保存每一层卷积缓存的原生流式 VAD。临时任务只保留最新修订；
最终句队列最多 4 条，过载会提示并丢弃最旧待处理句，避免无界延迟。

静音时部分设备不回调，程序会补静音来结束句尾。设备切换或缓冲溢出会清除上下文并丢弃过期结果。
不支持受 DRM 保护的不可回放采集声音。英文质量需要用更多自然语音验证；JFK 的 22 词样本仅作回归用。
数字静音门限和模型非语音标签用于减少误触发，音乐及系统提示音仍可能产生误识别。
8 秒的强制句界可能切断词语；当前不宣称无缝跨句拼接。

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
源代码来源和修改范围见 [vendor/NOTICE.md](vendor/NOTICE.md)。
