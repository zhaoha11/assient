### 任务总结

**任务目的**：在现有 `GDISreenScapture` 基础上新增 WGC 桌面采集后端，提高桌面采集效率，降低 CPU 占用和不必要的内存拷贝，为后续 D3D11 GPU 图像处理、NV12 输出以及硬件编码做准备。现阶段先保证 WGC 能稳定采集并接入现有 H.264 编码/RTMP 推流链路，不要求一步做到完全零拷贝。

**实现方向**：抽象统一的视频采集接口，使 GDI 和 WGC 都能向上层提供统一的视频帧；新增 `WGCScreenCapture`，使用 Windows Graphics Capture 获取显示器画面，通过 D3D11 接收 GPU Texture；第一阶段允许将纹理读回 CPU，转换成当前编码器能够接受的格式，优先保证完整链路跑通；后续再考虑 GPU 侧 BGRA→NV12、共享纹理和硬件编码。

**注意事项**：不要直接删除或改坏现有 GDI，实现必须能够回退；保持现有分辨率、FPS、时间戳和编码接口行为一致；处理好 WGC/D3D11 对象生命周期、线程安全、窗口/显示器变化、分辨率变化、DPI、多屏、停止和重启；不要为了接入 WGC 顺便大规模修改编码器、RTMP 或播放器

### 第一阶段实施记录（2026-10-04）

- 新增 `ScreenCapture` 接口及统一的 `CaptureFrameView`：两种后端都提供紧凑 BGRA（`stride = width * 4`）、采集序号和 `steady_clock` 时间戳。WGC 帧视图持有读回缓冲，编码线程同步使用期间不会被采集线程覆盖；GDI 仍使用原有三缓冲。
- 新增 `WGCScreenCapture`：采集线程以 `RoInitialize` 初始化 WinRT，按 GDI 的选择方式获取主显示器 `HMONITOR`，用 `IGraphicsCaptureItemInterop::CreateForMonitor` 创建采集项；创建 D3D11 Device、`CreateFreeThreaded` FramePool 和 CaptureSession。每轮从帧池取最新的 `ID3D11Texture2D`，复制到 staging texture 并 `Map`，按 GPU 行跨度读出紧凑 BGRA。现阶段仍有 GPU→CPU 读回，以及现有编码器的 CPU 帧复制、BGRA→YUV420P 转换；尚未零拷贝。
- WGC 每 1/30 秒发布一帧；桌面静止无新纹理时复用上次读回缓冲，按采集时钟推进序号，保持当前 30 FPS/PTS 语义。新帧只做一次从 staging 到 CPU 缓冲的行复制；等待编码的旧缓冲由共享所有权保证有效。
- `RtmpPushManager` 使用接口指针。默认继续使用 GDI；远程页面“开始远程”按钮右侧的“采集方式”下拉框可选择 GDI/WGC。控制端发起远程会话时通过信令服务器把选择传给被控端；被控端在本次推流中采用该选择，并在成功启动后把实际采集方式同步到自己的 UI。切换下拉框不会中途改变正在推的流。WGC 不受支持、初始化失败或 3 秒内没有首帧时自动回退 GDI。
- WGC 的 WinRT/D3D11 对象只在采集线程创建和释放；`RequestStop()` 唤醒编码等待，`Close()` 再等待采集线程退出。Frame、Session、FramePool 显式关闭，重复 `Init`/`Close` 已做探针验证。内容尺寸变化时在释放当前帧后 `Recreate` 帧池，输入尺寸随新帧变化，现有 `VideoEncoder` 将其转换到本次推流开始时确定的编码尺寸；主显示器句柄变化时重建采集会话。当前仅采集主显示器，和 GDI 一致；WGC 以物理像素尺寸读帧，不使用 Qt 逻辑 DPI 尺寸。

验证：Qt 6.10.1 MinGW 13.1.0 Debug 重新生成 Makefile 后单线程编译链接通过；本机 WGC 两次启动/停止均取得 1920×1080、stride 7680 的 BGRA 帧；WGC→现有 H.264 编码器连续编码 31 帧；WGC→现有 `RtmpPushManager`→SRS 推流运行 20 秒，SRS 拉流端 `ffprobe` 识别 H.264 1920×1080、30/1 FPS 与 AAC；默认 GDI→SRS 路径另运行 5 秒，编码推流正常。

待验证：长时间运行、运行中分辨率/DPI/主显示器切换、停止后重新建立完整 RTMP 会话、画面质量和端到端延迟。尚未做同条件 CPU 占用和拷贝耗时对照，因此不能宣称 WGC 已降低 CPU。下一阶段再考虑 D3D11 纹理上进行 BGRA→NV12、GPU 纹理交给硬件编码器，届时可去掉 staging/Map 读回。

采集方式 UI 与信令调整：移除 `ECLOUD_CAPTURE_BACKEND` 环境变量判断。`OBTAINSTREAM` 请求和 `CREATESTREAM` 通知各追加一个字节（`0=GDI`、`1=WGC`），服务器把控制端选择转发给被控端；旧格式包按 GDI 处理。每次推流会话第一次取得采集帧时输出一次 `[CAPTURE] session first frame`，记录实际后端（包括 WGC 失败回退到 GDI 的情况）。Qt 6.10.1 MinGW 13.1.0 Debug 客户端编译链接通过，服务器协议头和改动源文件的语法检查通过。现有 `ENET/build` 属于另一套 Linux 路径，尚未完成服务器链接或两端联调；必须重新构建并部署修改后的信令服务器，控制端选择才会传到被控端。




第二阶段：请基于当前 `main` 分支继续开发视频采集与编码架构。

> 当前项目已经实现：
>
> ```
> GDIScreenCapture
>     ↓
> CPU BGRA
>
> WGCScreenCapture
>     ↓
> D3D11 Texture
>     ↓
> staging texture
>     ↓
> Map
>     ↓
> CPU BGRA
>
> 两者最终统一成 CaptureFrameView
>     ↓
> H264Encoder
>     ↓
> RtmpPublisher
> ```
>
> 目前的问题是 `CaptureFrameView` 本质上仍然假设所有视频帧最终都必须转换成 CPU 内存：
>
> ```
> const quint8* data;
> ```
>
> 这使得 WGC 即使已经获得 `ID3D11Texture2D`，仍必须执行：
>
> ```
> CopyResource
> → staging texture
> → Map
> → memcpy
> → CPU BGRA
> ```
>
> 从而失去了后续 GPU 零拷贝处理的基础。
>
> 本阶段目标是重构采集帧和编码入口，使系统能够同时支持：
>
> ```
> GDI → CPU BGRA
> WGC → D3D11 Texture
> ```
>
> 并为后续：
>
> ```
> D3D11 Texture
> → GPU BGRA→NV12
> → Hardware H264 Encoder
> ```
>
> 做准备。
>
> 目标架构：
>
> ```
>                     ScreenCapture
>                          │
>               ┌──────────┴──────────┐
>               │                     │
>       GDIScreenCapture       WGCScreenCapture
>               │                     │
>          CPU BGRA             D3D11 Texture
>               │                     │
>               │                GPU BGRA→NV12
>               │                     │
>       ┌───────┴──────────┐          │
>       │                  │          │
> Software Encoder   Upload to GPU    │
>       │                  └────┬─────┘
>       │                       │
>       │                Hardware Encoder
>       │                       │
>       └───────────┬───────────┘
>                   ↓
>                  H264
>                   ↓
>              RtmpPublisher
> ```
>
> 请按以下原则实现：
>
> 1. 保留现有 `GDIScreenCapture`，不要破坏当前稳定的 CPU BGRA → 软件 H264 路径。
> 2. 改造 `ScreenCapture` / `CaptureFrameView` 的抽象，不再默认所有帧一定存在 `const uint8_t* data`。
> 3. 设计能够表达两类视频帧的数据结构：
>
> ```
> CPU Frame
> - BGRA
> - data
> - stride
> - width/height
>
> GPU Frame
> - D3D11 Texture
> - width/height
> - pixel format
> ```
>
> 可以使用 enum + variant，也可以定义独立的 CPU/GPU frame 类型，但尽量不要把平台相关的 D3D11 类型污染到不必要的公共模块。
>
> 1. `GDIScreenCapture` 继续输出 CPU BGRA，不要求改成 GPU。
> 2. `WGCScreenCapture` 增加直接输出 `ID3D11Texture2D` 的能力。
> 3. 当前 WGC 的：
>
> ```
> staging
> Map
> memcpy
> ```
>
> 暂时不要删除。
>
> 保留 CPU readback 路径作为：
>
> ```
> fallback
> debug
> A/B comparison
> ```
>
> 1. 不要让 `RtmpPushManager` 出现大量类似：
>
> ```
> if(GDI) ...
> else if(WGC) ...
> ```
>
> 的业务分支。
>
> `RtmpPushManager` 应继续主要负责：
>
> ```
> 获取帧
> → 调用编码器
> → 推送 H264
> ```
>
> 采集格式差异和 GPU/CPU 差异应由 Capture / Encoder 层处理。
>
> 1. 编码层为后续支持两种路径做好结构：
>
> ```
> CPU BGRA
> → Software Encoder
>
> D3D11 Texture
> → Hardware Encoder
> ```
>
> 可以考虑增加：
>
> ```
> EncodeCpuFrame(...)
> EncodeGpuFrame(...)
> ```
>
> 或抽象：
>
> ```
> VideoEncoder
> ├── SoftwareVideoEncoder
> └── HardwareVideoEncoder
> ```
>
> 本阶段可以只完成接口和数据边界设计，不要求立即完成硬件编码。
>
> 1. 生命周期必须明确。
>
> 特别注意：
>
> ```
> ID3D11Texture2D
> ID3D11Device
> ID3D11DeviceContext
> FramePool
> CaptureSession
> ```
>
> 的线程归属和释放顺序。
>
> GPU Frame 在编码线程使用期间，其底层 Texture 必须保持有效，不能被下一帧覆盖或提前释放。
>
> 1. 不要在当前阶段一次性同时完成：
>
> ```
> GPU BGRA→NV12
> NVENC/QSV/AMF
> 零拷贝
> 多编码器适配
> ```
>
> 本阶段优先完成“数据结构与接口重构 + WGC GPU Texture 输出”。
>
> 完成后请给出：
>
> - 修改了哪些类和文件；
> - 新的视频帧抽象是什么；
> - GDI 如何继续使用 CPU Frame；
> - WGC 如何输出 D3D11 Texture；
> - Texture 生命周期如何保证；
> - 当前 CPU readback fallback 是否仍保留；
> - `RtmpPushManager` 是否仍保持与采集后端解耦；
> - 下一阶段如何接入 GPU BGRA→NV12；
> - 下一阶段如何接入硬件 H264 编码器；
> - 编译和运行验证结果。
>
> 本阶段完成标准：
>
> ```
> GDI
> → CPU BGRA
> → 原软件编码
> → RTMP
> 正常
>
> WGC
> → D3D11 Texture
> 能稳定获取
>
> WGC CPU fallback
> → 仍然能正常编码推流
>
> Capture / Encoder 边界
> → 不再强制所有采集后端输出 CPU buffer
>
> Stop / Restart
> → 无崩溃
> ```
>
> 本阶段暂时不要以“性能提升”为验收目标，先保证架构和资源生命周期正确。

这版做完以后，你下一阶段就可以很自然地继续：

```
WGC D3D11 Texture
→ GPU BGRA→NV12
→ NVENC/QSV/AMF/Media Foundation
→ H264
```

而不会再需要重新改一次采集层

### 第二阶段实施记录（2026-10-05）：采集帧抽象重构 + WGC GPU Texture 输出

范围：只做数据结构与接口重构 + WGC 直出 D3D11 纹理；不做 GPU BGRA→NV12、不做 NVENC/QSV/AMF、不做零拷贝。验收只考核架构与生命周期，不考核性能。

新增 `Codec/VideoFrame.h`（不包含任何平台头）：`VideoFrameKind{Cpu,Gpu}`、`VideoPixelFormat{Bgra8,Nv12,Unknown}`、抽象句柄 `IGpuVideoFrame`（`width/height/format/nativeTexture()/nativeDevice()`，原生资源一律 `void*`）、`CpuFrameView`（owner/data/stride）、`VideoFrame`（kind + cpu + gpu(shared_ptr) + width/height/sequence/capturedAt）。`Codec/Codec.pri` 登记该头。D3D11 类型只在 `WGCScreenCapture.cpp` 内解释，公共头不感知。

`ScreenCapture`：删除 `CaptureFrameView`，`WaitLatestFrame` 改收 `VideoFrame&`；新增 `CaptureOutput{CpuReadback,GpuTexture}` 与 `SupportsGpuOutput()/SetOutput()`（默认不支持、返回 false，GDI 沿用默认）。

GDI：`WaitLatestFrame` 只改为填 `VideoFrame` 的 CPU 字段（`kind=Cpu`、`cpu.data` 指向内部三缓冲 front、`owner` 留空）与顶层序号/时间戳，三缓冲索引交换逻辑一行未动。

WGC：`WgcState` 增输出模式（默认 `CpuReadback`）。GPU 模式下取到 pool 纹理后 `CopyResource` 进**每帧新建的自有** `D3D11_USAGE_DEFAULT` 纹理并包成 `WgcGpuFrame`，随即 `newest.Close()` 归还 pool；不直接持有 pool 纹理，因为 pool 只有 2 块缓冲，被编码线程占住会饿死 `TryGetNextFrame`。`WgcGpuFrame` 内部同时持 `ComPtr<ID3D11Texture2D>` 与 `ComPtr<ID3D11Device>`：纹理不是 pool 缓冲，`Recreate`/`Close` 不会使其失效；device 引用保证采集端先退出也不悬空。单槽位由 `latestPixels_` 泛化为 `latestFrame_`，`WaitLatestFrame` 复制 `cpu.owner` 或 `gpu`。**staging/Map/memcpy 读回路径原样保留**，作为 fallback/debug/A-B。

编码：`H264Encoder` 新增 `EncodeFrame`（按 `kind` 分发）、`EncodeCpuFrame`（复用现有软件实现）、`EncodeGpuFrame`（本阶段为只记录一次日志的 stub，返回 -1）。`RtmpPushManager::EncodeVideo` 改用 `VideoFrame` + `EncodeFrame`，**不出现任何采集后端分支**；新增 `SetCaptureOutput` 透传（默认 `CpuReadback`），在 `Init()` 内、`screen_Capture_->Init()` 之前调用（采集线程建立会话时读取该设置）。GPU 输出模式当前只在代码层可切，未接 UI/信令，也非默认。

验证：Qt 6.10.1 MSVC2022 Debug `qmake` + `jom /f Makefile.Debug -j4` 编译链接通过，exit 0，生成 `debug/ECloudAssistant.exe`，无 error（仅既有 warning）。exe 启动冒烟通过（启动、进程存活、可终止）。未验证：验收标准中的 GDI 推流、WGC GPU 纹理稳定获取、WGC CPU 推流、Stop/Restart 反复 20 次——均需信令服务器 + SRS + 双端实机，本机未执行；运行中分辨率/DPI/主显示器切换与长时间运行同样未做。

下一步（第三阶段）钩子：在 `WgcGpuFrame` 上挂着色器/计算把 BGRA→NV12，`format()` 返回 `Nv12`；实现 `EncodeGpuFrame`，把 `nativeTexture()/nativeDevice()` 交给硬件编码器；编码器选择在 `Open()` 时一次性决定（见下方讨论），避免每帧分支。

#### 第三阶段编码器选择：软编 / 硬编如何选（设计约定）

**结论：软/硬编码选择是会话级、一次决定，不用每帧 if/else；每帧只保留一个必然的帧类型分发点。**

否决"每帧 `if(软编)…else if(硬编)…`"的理由：

- 两套编码器的状态、初始化时机、SPS/PPS 获取会同时活在 `RtmpPushManager` 里，各写一遍。
- 每帧分支等于"运行中切编码器"，但硬件编码器无法中途替换——换成硬编后 SPS/PPS 变化，SRS 端要重新协商，实际只能重开推流。风险换不来灵活性。
- 与阶段二"`RtmpPushManager` 不出现采集后端分支"的边界冲突。

**决策点（一次性，`Open()`/`Init()` 内）**：由「采集后端 + 输出模式 + 硬件可用性」共同决定，产出唯一结果：

| 采集后端 | 输出模式 | 硬件编码可用 | 选定编码器 |
| --- | --- | --- | --- |
| GDI | CPU BGRA | —（不适用） | 软件编码 |
| WGC | `CpuReadback` | — | 软件编码（fallback / A-B 对照） |
| WGC | `GpuTexture` | 是 | 硬件编码（零拷贝目标路径） |
| WGC | `GpuTexture` | 否 | Open 期回退：软编（需走 readback）或直接报错，不做每帧切换 |

结构上用 `VideoEncoder` 基类 + `SoftwareVideoEncoder` / `HardwareVideoEncoder` 子类，`Open()` 里按上表 new 一个，`EncodeFrame` 为虚函数；`RtmpPushManager` 只持有基类指针，感知不到具体是哪个。

**每帧（唯一保留的分发）**：仍在 `H264Encoder::EncodeFrame` 内按 `VideoFrameKind` 分一次，这行阶段二已存在：

```
if(frame.kind == VideoFrameKind::Gpu) EncodeGpuFrame(...);
else                                  EncodeCpuFrame(...);
```

这个 `if` 保留是对的：它不是"软/硬"分支，而是"这帧数据在哪"——即使启用硬件编码，中间仍可能收到 CPU 帧（fallback、缩略图等），按帧数据类型分发天然成立。

**一句话**：软/硬 = 会话级、多态替换；帧类型 = 帧级、保留一处分发。第三阶段接入硬件编码的改动集中在 `HardwareVideoEncoder` 实现 + `Open()` 决策函数，`RtmpPushManager` 与采集层不动。

#### 第三阶段实施路径：分两步（软硬共存 → GPU 纹理直入）

**第一步：软编/硬编共存，输入仍是 CPU 帧（本期实现）**

把 `VideoEncoder` 升为基类，拆出 `SoftwareVideoEncoder`（现有 libx264 行为原样搬）与 `HardwareVideoEncoder`。决策一次，在 `H264Encoder::OPen` 时做，不每帧。两者真正的差异只有三处：

| | 软件 (x264) | 硬件 (nvenc/qsv/amf) |
| --- | --- | --- |
| 查找 | `find_encoder(AV_CODEC_ID_H264)` | `find_encoder_by_name("h264_nvenc")` 等 |
| `pix_fmt` | `YUV420P` | 一般是 `NV12` |
| `priv_data` 选项 | `tune=zerolatency/preset=ultrafast` | 各自的 `preset/tune` 等，不能照搬 x264 |

这一步输入仍是普通 CPU `AVFrame`，不需要 `AVHWFramesContext` 或 `hw_device_ctx`——nvenc/qsv/amf 都能吃软件 NV12 帧并自行上传。所以本质是"换 codec 名 + 换 pix_fmt + 换选项串"，`VideoConverter` 目标格式改为 NV12，其余（转换、send/receive、extradata、关键帧拼 SPS/PPS）全部共享放基类。回退：`OPen` 内先按 `h264_nvenc → h264_qsv → h264_amf` 逐个试，`avcodec_open2` 失败就换下一个，全失败落到软件并记日志。"FFmpeg 列出 h264_nvenc" ≠ "本机能打开"，唯一验证是真 open 一次。

**第二步：WGC GPU 纹理直入（下一阶段）**

这时才实现 `EncodeGpuFrame`：`Open` 时建一次 `AVHWFramesContext` + 硬件设备（`av_hwdevice_ctx_create` 选 D3D11VA/CUDA/QSV），把 `nativeTexture()/nativeDevice()` 包成硬件 `AVFrame`，或先在 GPU 上 BGRA→NV12 再喂硬编。设备与帧上下文归 `HardwareVideoEncoder` 实例持有，会话级建一次。纹理生命周期由阶段二的 `VideoFrame.gpu`（`shared_ptr`）保证，`EncodeGpuFrame` 只需"用完才释放引用"。

**注意**：音频保持 AAC 软编不动；`GetSequenceParams` 读 `codecContext->extradata`，软硬编通用，无需改动；每帧分发点仍只有 `EncodeFrame` 里按 `kind` 那一次，不在硬编里再套软/硬 if。

### 第三阶段实施记录（2026-10-05）：WGC GPU 纹理直入 NVENC

范围：只做 NVENC（本机 RTX 4060 Laptop）；AMF/QSV 不在本期。目标是**避免 GPU→CPU 回读**（允许 GPU→GPU 拷贝与 GPU 内格式转换），不要求严格零拷贝。音频 AAC 不变。

**数据路径（路径 A）**：WGC pool 纹理 →（采集侧 `CopyResource`）→ 每帧自有 BGRA 纹理 →（编码侧 `ID3D11VideoProcessor` `VideoProcessorBlt`，BT.709）→ FFmpeg NV12 硬件帧 →（`h264_nvenc`）→ 码流。全程不出现 staging/`Map`/`memcpy`。注意这不是严格零拷贝：有两次 GPU 内操作（拷贝 + 格式转换），省掉的是 GPU→CPU 回读。

**新增/改动**：

- 新增 `Codec/D3D11SharedContext`：按 vendor id 枚举 DXGI adapter（NVIDIA `0x10DE`）建 `D3D11CreateDevice`，flags = `BGRA_SUPPORT | VIDEO_SUPPORT`；持 device/context/videoDevice/videoContext 与一把 `ContextLock()`。采集与编码必须共用同一 device，NVENC 才能消费 WGC 的纹理。
- `HardwareVideoEncoder` 增 GPU 模式（`shared != nullptr && codecName == "h264_nvenc"`）：建 `AVHWFramesContext`（`format=D3D11`、`sw_format=NV12`、`BindFlags=D3D11_BIND_RENDER_TARGET`）并把 `hw_frames_ctx` 挂到编码器；`EncodeGpuFrame` 取硬件帧、`VideoProcessorBlt`、send/receive。D3D11 类型用 pimpl 挡在 `.h` 之外。
- `WGCScreenCapture` 支持 `SetExternalDevice`，复用注入的 device/立即上下文；GPU 分支的 `CopyResource` 与编码侧 blt 都用 `ContextLock()` 串起来（立即上下文非线程安全）。
- `RtmpPushManager` 拆出可重试的 `SetupPipeline`，`Init()` 按 A→B→C 依次尝试：A=WGC GPU+NVENC，B=WGC CPU 读回+软编，C=GDI+软编；任一档失败完整拆除再试下一档。会话级记录 `activePath_`。
- 运行中退化：`EncodeVideo` 检查 `HasFatalError()`，置位则 `emit videoPathFailed()`；`RemoteManager` 以 `Qt::QueuedConnection` 收到后停推、以软编路径重建一次（只降一级，不做无限重试）。

**崩溃根因与修复（2026-10-05）**：首次跑路径 A 崩在 WGC 首帧与任何 `[ENCODE]` 日志之间。原因是 `ConfigureGpuFrames` 注入外部 device 时把指针解释错了——`av_hwdevice_ctx_alloc` 返回的 `AVBufferRef::data` 是 `AVHWDeviceContext`，d3d11va 专属结构挂在它的 `hwctx` 字段；代码直接把 `data` 当成 `AVD3D11VADeviceContext` 并写 `device`，覆盖了 `AVHWDeviceContext::av_class/internal`，FFmpeg 随后任一次日志或初始化即崩溃。修复为经 `hwctx` 取结构（`hwcontext.h` 注释明确：`hwctx` 是 format-specific data，需 cast 后填，再 `av_hwdevice_ctx_init`）。重新编译链接通过，路径 A 待实测。

#### 端到端延迟与编码器输出延迟 D 的分析（2026-10-05）

**三段队列模型**：端到端延迟 = 采集侧排队 + 编码器内部延迟 + RTMP/播放器缓冲，三段是分开的。

- **采集侧**：WGC 帧池 + `latestFrame_` 单槽最新帧，编码线程永远拿最新、丢掉错过的 → 零排队。这是「通知+三缓冲」那套的功劳（交接等待 ~30μs，`dup=0/skip=0`）。
- **编码器内部 D**：编码器自己压着几帧才吐包。**与采集侧三缓冲无关**，三缓冲碰不到这一段。
- **RTMP 发送 + 播放器 jitter buffer**。

**零拷贝 ≠ 低延迟**：零拷贝管的是内存怎么走（省 GPU→CPU 回读），D 管的是时间怎么流（一帧的包要等几帧才出去）。两者正交。GPU 直通把帧直接塞进编码器，这段没有队列；但编码器收下之后不会立刻吐包，内部仍要压住几帧。

**软编/硬编选项对照**（软编 `SoftwareVideoEncoder.cpp:13-29`，硬编 `HardwareVideoEncoder` 的 `ConfigureCodec`+`SetEncoderOptions`，基础参数 `VideoEncoder.cpp:63-74`）：

| 项 | 软编 libx264 | 硬编 h264_nvenc（改前） |
| --- | --- | --- |
| preset | ultrafast | p1 |
| tune | zerolatency | ull |
| **zerolatency** | tune 内含 | **未设，默认 false** |
| **delay** | — | **未设，默认 INT_MAX** |
| profile/level | BASELINE / 40 | 未设 → nvenc 默认 main / auto |
| VBV（rc_buffer） | =12Mbps | 未设 |
| max_b_frames / gop / bit_rate | 0 / 30 / 12M | 相同 |

`ffmpeg -h encoder=h264_nvenc` 显示：nvenc 声明 `dr1 delay` 能力；`-delay` 默认 `INT_MAX`（auto）；`-zerolatency` 默认 `false`，文档原文 "no reordering delay"；`-rc-lookahead` 默认 0。软编靠 `tune=zerolatency` 得到 D≈0（`采集和编码的低延迟优化.md:768`），硬编的 `tune=ull` 只设 tuning info，`zerolatency`/`delay` 是独立选项、默认没开——NVENC 探针的「60 进 / 58 出」提示约 2 帧积压很可能出在这里。

**预期与保留**：若 D=2，30fps 下即 +66.7ms，与「WGC+NVENC 当前 ~120ms vs GDI 基线 ~59.33ms（`采集和编码的低延迟优化.md:736`）」的量级吻合；把 D 压到 0/1 才可能接近或追平 GDI。但 NVENC 不是 frame-in-frame，硬件流水线可能有 ≥1 帧地板，`zerolatency` 消的是重排延迟、未必能压平硬件深度。是否达到「比 GDI 低 5~15ms」需同条件端到端对照，不能预设。

**下一步**：① 给 nvenc 补 `zerolatency=1`（+`delay=0`）；② 逐帧量 D（已送帧数 − 已收包数，稳定值即 D），确认是固定流水线深度还是随运行时长递增的堆积。二者一次跑完。

### 第三阶段收尾（2026-10-05）：D 实测定案 + 延迟打平 GDI

前面①②一次跑完，结论落地如下。

**D 实测**：同一次运行里 C 与 A 各跑一段，`[LAT-D]` 给出

| 路径 | 编码器 | `[LAT-D] delay` |
| --- | --- | --- |
| C GDI+x264 | libx264 | **0** |
| A WGC+nvenc（默认 delay） | h264_nvenc | **2** |

即 nvenc **默认把 2 帧压在内部输出流水线**，30fps 下 ≈ **66.7ms**。这与早先「60 进 / 58 出」的探针、与「A ~120ms vs GDI ~59.33ms」的差额都吻合。x264 因为 `tune=zerolatency` 本来就 D=0，所以问题只在 nvenc。

**修复**：`HardwareVideoEncoder::SetEncoderOptions` 的 nvenc 分支补 `zerolatency=1` + `delay=0`，D 由 2 降到 0。

**一个判断纠错**：中途曾因这一改动让 `编码均值us` 从 ~0.4ms 涨到 ~8ms 而误判成回退并撤掉。那是把「编码耗时」当成了「延迟」——`delay=0` 让编码从异步转同步，耗时记法变了、净延迟反而降了。正确账是 **去掉 66ms 排队、只多花几 ms 单帧耗时，净赚 ~58ms**。已恢复。

**端到端结果**：WGC 端到端 **50-70ms**，与 GDI 基线（9 次、44-67ms、均值 59.33ms）**打平**。阶段目标达成——GPU 纹理直入 NVENC、全程无 GPU→CPU 回读（`转换均值us = 0`）、单帧 CPU 侧省 ~94%，延迟不再落后于 GDI。两条路径剩余的时间是共享地板（采集节拍 + 播放端 jitter + 网络），GPU 这条已无水分可挤。

**锁收窄（同批修复）**：`EncodeGpuFrame` 里 `ContextLock` 原本是函数级，把 `send_frame`/`receive_packet` 也罩住了。异步时（~0.4ms）无所谓，但 `delay=0` 后 `receive` 每帧阻塞数 ms，于是采集线程的 `CopyResource` 每帧都排在整段编码之后。现将锁收窄成一块，只罩 `ensure` + 建 input/output view + `VideoProcessorBlt`；send/receive 走 nvenc 自己的队列、不碰立即上下文，安全置于锁外。这是 `delay=0` 带出的新耦合，属我的改动引入，应修。

**为什么 nvenc 的 `编码均值us` 会 2↔9ms 摆而 x264 稳在 2ms**：两个数量的不是一回事。

- x264 那 2ms 是 **CPU 干活的时间**：纯 CPU 计算、确定性，且本机 CPU 线程多、余量大 → 稳定。（GDI 另有 `转换均值us ≈ 6.2ms` 的 swscale，每帧合计 ~8.2ms，又稳又贵。）
- nvenc（`delay=0` 同步）那 2-9ms 是 **等 GPU 把帧编完的时间**：主要成分是 GPU 调度 + 驱动往返 + 与同一块 GPU 上其他活儿（采集 `CopyResource`、`VideoProcessorBlt`、桌面合成、别的程序）抢队列，再叠笔记本 GPU 的 DVFS，故浮动大。它不是积压，不随运行时增长（`dup=0/跳帧=0`，30fps 稳定）。

| | 转换均值us | 编码均值us | 合计/帧 |
| --- | --- | --- | --- |
| GDI+x264 | 6200 | 2000（稳） | ~8200µs |
| WGC+nvenc | 0 | 2000-9000（摆） | 2000-9000µs |

nvenc 最坏与 GDI 打平，常态仅其 1/4，且省掉整个 swscale。

**本阶段结束**。未做/可续（若要进一步压端到端）：① WGC 采集节拍量化（`Run()` 取完帧 `wait_until(nextTick)` 睡到 30fps 边界，恰在睡眠开始到达的帧最坏等一整个周期），改按帧到达唤醒可削；② nvenc 码率对齐 x264 的 `rc=cbr` + VBV 12Mbps（实测尖峰 3.9→20.5Mbps，会撑大播放端 jitter）；③ AMF/QSV；④ 纹理池化（目前每帧新建纹理）。

### 踩坑记录：问题与解决（2026-09-16 ~ 2026-10-05）

把 WGC/NVENC 这条线上遇到过的问题汇总成一张表，按「现象 → 根因 → 解」记。前四条属编码/延迟，五~十条属 GPU/采集，十一条起属生命周期与实现细节。与上面各阶段重复的只保留一句并指明细节出处。

**编码 / 延迟**

1. **早期 NVENC 让端到端变差（2026-09-16，GDI + NVENC「CPU 输入」）** —— 现象：x264 基线 ~81-91ms，换 NVENC 后 ~179-180ms；强制 `delay=0`（含 `AV_CODEC_FLAG_LOW_DELAY`、单帧 VBV）复测仍 ~172ms。当时的结论是 NVENC 未能证明比软编低延迟，整体回滚软编，并记「FFmpeg 默认 NVENC 异步输出深度不是唯一延迟源」。**现在回看**：那一轮是 CPU 回读路径、并非 GPU 直通，172ms 里除编码器外还有未识别的延迟源（DWM / 播放端 jitter）。本轮在 GPU 直通路径上用 `[LAT-D]` 定位到 D=2 并压掉，端到端才降到 50-70ms。细节见 WORKLOG「Low-latency NVENC publishing」→「Conclude the NVENC latency experiment」。

2. **NVENC 默认输出延迟 D=2（本次）** —— 现象：WGC+nvenc 端到端比 GDI 多 80-170ms；`[LAT-D]` 读出 A=2 / C=0。根因：nvenc 默认把 2 帧压在内部输出流水线（30fps ≈ 66.7ms）。解：nvenc 补 `zerolatency=1` + `delay=0`，D→0。详见「第三阶段收尾」。

3. **把「编码耗时」误当「延迟」（本次）** —— 现象：上条修复后 `编码均值us` 从 ~0.4ms 涨到 ~8ms，一度判为回退并撤销。纠正：`delay=0` 让编码由异步转同步，耗时记法变了、净延迟反降 ~58ms。**结论：不要用 `编码均值us` 判断延迟。**

4. **`ContextLock` 持有过久（本次）** —— 现象：`EncodeGpuFrame` 的锁是函数级，把 `send_frame`/`receive_packet` 也罩住；`delay=0` 后 `receive` 每帧阻塞数 ms，采集线程 `CopyResource` 每帧排在整段编码之后。解：锁收窄成一块，只罩 `ensure` + 建 view + `VideoProcessorBlt`。

**GPU / 采集**

5. **路径 A 首次运行崩溃（本次）** —— 现象：WGC 首帧日志后、任何 `[ENCODE]` 日志前崩溃。根因：`av_hwdevice_ctx_alloc` 返回的 `AVBufferRef::data` 是 `AVHWDeviceContext`，d3d11va 专属结构在 `hwctx` 字段；代码直接把 `data` 当 `AVD3D11VADeviceContext` 写 `device`，覆盖了 `AVHWDeviceContext::av_class/internal`，FFmpeg 后续任一次日志或初始化即崩。解：经 `hwctx` 取结构再填。

6. **WGC 帧池被占死（阶段二）** —— 现象：pool 只有 2 块缓冲，编码线程直接持有 pool 纹理会饿死 `TryGetNextFrame`。解：每帧 `CopyResource` 进自有纹理，与 pool 回收解耦（代价：非严格零拷贝）。

7. **立即上下文跨线程（阶段三）** —— 现象：采集线程 `CopyResource` 与编码线程 `VideoProcessorBlt` 共用同一个立即上下文，非线程安全。解：`D3D11SharedContext::ContextLock()` 串起来。

8. **混合显卡选错 adapter（阶段三）** —— 现象：本机 iGPU(780M) + dGPU(4060)，NVENC 在 dGPU；默认 adapter 可能是 iGPU，则 WGC 纹理与编码器不在同一 device、无法 blt。解：`D3D11SharedContext::Create` 按 vendor id 枚举 DXGI adapter，优先 NVIDIA `0x10DE`。

9. **hw device 所有权（阶段三）** —— 现象：hw device ctx 的 uninit 会无条件 Release device。解：注入外部 device 前先 `AddRef` 转移一份所有权；**绝不**调 `av_hwdevice_ctx_create`（那会另建一个 device，与采集侧不共享）。

10. **帧池 `BindFlags` 必须含 RENDER_TARGET（阶段三）** —— 现象：留 0 会被 FFmpeg 默认成 `BIND_DECODER`，创建纹理失败。解：`AVD3D11VAFramesContext::BindFlags = D3D11_BIND_RENDER_TARGET`。

**生命周期 / 实现细节**

11. **线程与 device 生命周期顺序** —— 现象：GPU 帧与 hw frames 都依赖 device，采集线程与编码线程都借用 `sharedGpu_`。解：Stop 顺序固定为 `exit_ → RequestStop → StopEncoder(join 编码线程) → 关 pusher → StopCapture(join 采集线程) → 才 reset sharedGpu_`。**编码线程必须在 device 销毁前 join。**

12. **WGC 的 WinRT 对象线程归属（阶段一）** —— 解：对象只在采集线程创建/释放，`RoInitialize(RO_INIT_MULTITHREADED)`；`Frame/Session/FramePool` 显式 `Close`，`RequestStop()` 唤醒等待、`Close()` 再 join。

13. **`Open()` 重复调用泄漏 `codecContext`（阶段三）** —— 现象：候选编码器「依次尝试」会多次调 `Open`，旧代码只在析构释放 → 中间全部泄漏。解：`Open()` 开头先 `avcodec_free_context`。

14. **日志运算符优先级坑** —— `qInfo() << ... << framesRef->data ? "ok" : "null"` 里 `<<` 优先级高于 `?:`，整行会变成条件表达式。已在代码注释标记，写日志时别再套悬空三元。
