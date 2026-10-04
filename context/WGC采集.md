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
