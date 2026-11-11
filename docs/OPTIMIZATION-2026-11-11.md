# CPU 优化实测（2026-11-11）

本轮保留 SenseVoiceSmall Q8、FSMN-VAD、CPU 后端和识别阈值。收益主要体现在分段回放的重复工作减少；小样本中没有测出错误率改善。

## 实现

- 当前句前 4 秒每秒更新临时结果，之后每 2 秒更新；最终句仍重新识别。长句显示刷新频率因此降低。
- ASR/VAD 复用 CPU 工作线程池和 ggml 图分配器，空闲工作线程不忙轮询。计算图本身仍按输入重建。
- 缓存固定窗函数和 Mel 滤波器表，保留原特征提取/LFR 算法。尚未实现增量 fbank 或原生 FSMN 状态缓存。
- 强制切句后，已确认有语音的 200 ms 尾部直接延续，不再等待另一次起音；保留尾部静音计数，覆盖切点停声和短暂 VAD 漏检。全为非语音的尾部仍使用普通预缓冲。前后最终音频不重叠，仍可能切断词语。
- 新增 Unicode CER；保留原英文 WER 归一化规则。

## 方法与样本

- 本机 AMD Ryzen 7 7840HS，16 逻辑处理器；MSVC x64 Release、AVX2；ASR 8 线程、VAD 2 线程。
- 改代码前将原版二进制保存在 `build-baseline/Release/`，使用相同模型、音频和参考文本比较。
- 每个样例/模式先各预热一次，再进行 3 轮原版/优化版交替测试。表中为进程总耗时中位数，包含加载模型、读取音频及输出结果。
- `--stream` 是尽快处理文件的有限窗口 VAD + 分段识别回放，不按真实时间播放，临时任务同步执行；不是 WASAPI/GUI 端到端延迟测试，也未测实时 CPU 利用率。
- 整段模式每个进程连续识别 3 次，验证三次输出相同后计分；所有样例在同一版本三轮测量中的最终文本也一致。
- 下表为组合效果，没有分别量化每项优化的贡献。

| 样例 | 构成 | 参考长度 |
|---|---|---|
| english | whisper.cpp JFK，11 秒 | 22 词 |
| chinese | FunASR 官方中文音频，5.547 秒 | 19 字 |
| chinese_repeat | 中文外侧静音轻度裁剪后重复 5 次，26.239 秒；仍有自然停顿 | 95 字 |
| chinese_continuous | 20 ms 帧 RMS > 200 PCM16 单位，保留前 100 ms/后 40 ms；裁剪后重复 6 次，26.520 秒；确实触发 12 秒强制切句 | 114 字 |

重复/裁剪样例是合成压力输入，不是独立的新自然语音数据集。中文参考来自 [FunASR 官方示例](https://github.com/modelscope/FunASR/blob/main/README_zh.md)，英文参考为 `tests/english-reference.txt`。

## 三轮结果

| 样例 | 模式 | 原版(s) | 优化版(s) | 耗时下降 | 调用数 原→新 | 错误率 原→新 |
|---|---|---:|---:|---:|---:|---|
| english | 整段×3 | 1.247 | 1.208 | 3.14% | 3→3 | WER 0%→0% |
| english | 分段回放 | 1.589 | 1.337 | 15.86% | 11→10 | WER 4.55%→4.55% |
| chinese | 整段×3 | 0.774 | 0.753 | 2.69% | 3→3 | CER 0%→0% |
| chinese | 分段回放 | 1.123 | 0.902 | 19.67% | 6→5 | CER 0%→0% |
| chinese_repeat | 整段×3 | 3.097 | 3.011 | 2.75% | 3→3 | CER 0%→0% |
| chinese_repeat | 分段回放 | 4.517 | 3.442 | 23.80% | 30→25 | CER 3.16%→3.16% |
| chinese_continuous | 整段×3 | 3.024 | 2.982 | 1.38% | 3→3 | CER 0%→0% |
| chinese_continuous | 分段回放 | 6.504 | 4.142 | 36.31% | 28→20 | CER 0%→0% |

英文分段回放仍有 1/22 个词替换，重复中文分段仍有 3/95 个字替换；没有修正识别结果后计分。整段模式约 1%–3% 的小幅时间差，不宜当作稳定的模型内核加速结论。

## 验证与审查

- 原始 CTest 2/2；最终 CTest 3/3 通过：流水线、真实独立 GUI 窗口、指标脚本。Python 指标组内有 3 个用例。
- 对归档原始头文件，连续切句、VAD 短暂漏检、恰好切点停声、长句重复工作四项回归失败；优化后全部通过。另验证切点立即 reset 不泄漏旧音频。
- 在相同 GNU C++20/O2 编译条件下，对原始/优化前端比较 400、480、1600、16000、192000 样本的确定性波形，共 228240 个 fbank 数值逐位相同。MSVC 模型回放未见参考错误率退化；未声称所有输入下 MSVC logits 均逐位相同。
- 独立源码审查指出的切点尾部丢失已补回归并修复；样例连续性描述、整段三次输出一致性检查也已补正。
- 分配器/线程池生命周期经独立审查；模型回放覆盖同一实例中的输入长度增长、缩短和重复识别。

## 复现与原始数据

```powershell
python scripts/compare-benchmarks.py --baseline build-baseline/Release/subtitle_bench.exe --candidate build/Release/subtitle_bench.exe --runs 3 --output test-output/optimization/final-comparison.json
```

运行前需将原始音频放入 `test-output/optimization/`：

- [jfk.wav](https://raw.githubusercontent.com/ggml-org/whisper.cpp/master/samples/jfk.wav)，SHA256 `59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e`。
- [zh.wav](https://isv-data.oss-cn-hangzhou.aliyuncs.com/ics/MaaS/ASR/test_audio/asr_example_zh.wav)，SHA256 `a1bd32dc78493c123f9625a66deee562aed2895f53fbc39f2cca3be7e6f4f20f`。

本机保留完整 JSON、样本清单/哈希、模型和二进制哈希、每次 stdout/stderr、最终转写，均位于 Git 忽略的 `test-output/optimization/`。原版 EXE 保留在 `build-baseline/Release/`。

## 尚未验证与保留边界

- 未测真实声音到屏幕的端到端 P50/P95，以及 30 分钟以上内存/CPU/设备切换。缓存分配器会保留已用过的高水位缓冲区。
- 这些少量同源样例不能代表自然多人、口音、弱音、音乐噪声的总体准确率。
- 尚未引入真正的重叠解码与文本拼接、热词/束搜索或原生流式编码器。退出时尾段排空及静音补样时钟仍为原实现。
- 优化代码与验证报告按用户要求提交到本地优化分支；未合并或推送。
