# ECloudAssistant / ENET Worklog

## Current handoff state

- Local branch: `main`
- Latest implementation commit: `b854c07 feat: isolate control sessions and enforce single controller`
- Working-tree expectation after this record is committed: clean.
- Client kit: Qt 6.10.1 MSVC 2022 x64 with Windows SDK 10.0.26100. Run from an MSVC x64 Developer Command Prompt in the client build directory:

  ```bat
  D:\Qt\6.10.1\msvc2022_64\bin\qmake.exe -o Makefile ..\..\ECloudAssistant.pro -spec win32-msvc CONFIG+=debug CONFIG+=qml_debug
  nmake /f Makefile.Debug
  ```

- Client build directory: `ECloudAssistant/build/Desktop_Qt_6_10_1_MSVC2022_64bit-Debug`
- Client result: MSVC Debug qmake plus jom compiled and linked `debug/ECloudAssistant.exe` on 2026-10-05.
- Server build must run in the VMware Linux environment:

  ```bash
  cd /mnt/hgfs/shared/assient/ENET/build
  make LoginSvr -j4
  make SigSvr -j4
  ```

## Login and device identity

Commit: `a44bb29 feat: propagate authenticated user code to signaling`

The login request still carries `code + account + passwd + timestamp`, but the
LoginServer authenticates with `account + passwd`. On success it returns the
database `USER_CODE` in `LoginResult`.

Data flow:

```text
database USER_CODE
  -> LoginServer LoginResult.code
  -> LoginWgt::sig_logined(ip, port, code)
  -> RemoteWgt::handleLogined(...)
  -> RemoteManager::Init(..., code)
  -> controlled SigConnection JOIN(code)
```

Relevant files:

- `ENET/LoginServer/define.h`
- `ENET/LoginServer/LoginConnection.cpp`
- `ECloudAssistant/UI/tool/defin.h`
- `ECloudAssistant/UI/center/LoginWgt.{h,cpp}`
- `ECloudAssistant/UI/center/RemoteWgt.{h,cpp}`

The LoginResult packet changed from 26 to 46 bytes in that commit. Client and
LoginSvr must be rebuilt together when checking out that change.

## Control-session isolation and single-controller policy

Commit: `b854c07 feat: isolate control sessions and enforce single controller`

Design decisions:

- A device registers with its database `USER_CODE`.
- Each controlling connection generates a temporary session code in the form
  `C` plus eight random hexadecimal characters.
- A control-session JOIN collision retries up to three times.
- `USER_CODE`, JOIN IDs, and target IDs are currently limited to 1-9 bytes.
- One target device permits only one active control session. A second request
  is rejected to prevent mouse and keyboard input from interleaving.

Client behavior:

```text
controlled connection: JOIN(device USER_CODE)
controlling connection: JOIN(control session code)
                       -> OBTAINSTREAM(target device USER_CODE)
```

Server behavior:

- `ConnectionManager::AddConn` atomically inserts a JOIN code and reports a
  duplicate.
- A target with an existing control relationship rejects another
  `OBTAINSTREAM`, including the short interval before RTMP publishing changes
  the target state to PUSHER.
- Removing the last controller restores the target server state to IDLE.

Relevant files:

- `ECloudAssistant/Net/SigConnection.{h,cpp}`
- `ECloudAssistant/UI/center/LoginWgt.cpp`
- `ECloudAssistant/UI/center/RemoteWgt.cpp`
- `ECloudAssistant/UI/tool/defin.h`
- `ENET/SigServer/ConnectionManager.{h,cpp}`
- `ENET/SigServer/SigConnection.cpp`
- `ENET/SigServer/define.h`

Expected logs:

```text
[Sig] send JOIN, role = controlling joinCode = CXXXXXXXX
[SigSvr] JOIN accepted: code=CXXXXXXXX, count=N
[SigSvr] obtain rejected: target=456 already has a controller
[Sig] obtain stream rejected: target is offline, invalid, or already controlled, targetCode = 456
```

## Verification scenarios

1. Login `zzh / 123456` with database code `123`; its first signal connection
   must JOIN `123`.
2. Login `zzh2 / 123456` with database code `456`; its first signal connection
   must JOIN `456`.
3. From client A, control `456`; its control connection must JOIN a generated
   `CXXXXXXXX` code and request `OBTAINSTREAM(456)`.
4. From client B, control `123`; the reverse direction must work concurrently.
5. While A controls `456`, attempt another control session for `456`; SigSvr
   must reject it and the second client must print the rejection.

## Known limits and next work

- The current signaling protocol stores JOIN and target IDs in `char[10]`; do
  not use database USER_CODE values longer than 9 bytes until both client and
  server protocol structs are enlarged together.
- SigServer is the signaling/control plane only. RTMP carries the media stream.
- The current server source was statically checked in this workspace, but the
  actual Linux `SigSvr` binary still needs the VMware build command above.
- A production version should authenticate SigServer connections with a login
  token. The current prototype trusts a client-provided JOIN identity.

## Device list shows the current logged-in client

Goal: replace the empty device-list placeholder with an interface that displays the account and `USER_CODE` returned by a successful login.

Affected files:

- `ECloudAssistant/UI/center/DeviceListWgt.{h,cpp}`
- `ECloudAssistant/UI/center/LoginWgt.{h,cpp}`
- `ECloudAssistant/UI/center/MainWgt.{h,cpp}`
- `ECloudAssistant/UI/UI.pri`
- `ECloudAssistant/UI/brown/main.css`

Behavior: `LoginWgt` emits the server endpoint, `USER_CODE`, and login account after a successful `LoginResult`; `MainWgt` initializes `RemoteWgt`, updates `DeviceListWgt`, then opens the remote-control page. The device-list page shows that one current logged-in device.

The page intentionally does not claim to show every online device: the current client/server protocol has no command that returns an online-device collection.

Verification: ran qmake and `mingw32-make.exe -f Makefile.Debug -j4` in `ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug`; compiled and linked `debug/ECloudAssistant.exe` successfully. Existing `QMouseEvent::globalPos()` deprecation warnings remain.

Rollback point: revert the files listed above; no packet layout or server behavior changed.

## Device registration state and label-background hardening

Goal: remove opaque label backgrounds on the device page and distinguish LoginServer authentication from SigServer device registration.

Affected files:

- `ECloudAssistant/UI/center/DeviceListWgt.{h,cpp}`
- `ECloudAssistant/UI/center/RemoteWgt.{h,cpp}`
- `ECloudAssistant/UI/center/RemoteManager.{h,cpp}`
- `ECloudAssistant/UI/center/MainWgt.cpp`
- `ECloudAssistant/Net/SigConnection.{h,cpp}`
- `ECloudAssistant/UI/brown/main.css`

Behavior: after authentication the device page enters `正在注册设备`; the controlled `SigConnection` reports its JOIN reply through a client-only callback. A successful reply changes the UI to `可被远程连接`; TCP/JOIN failure or a later signal disconnect changes it to an explanatory error. UI updates from the signal thread are marshalled onto the Qt UI thread with `QMetaObject::invokeMethod`.

The device-page QLabel styles now explicitly use transparent backgrounds and no border. Login/remote button hover and pressed selectors now apply pseudo-states to both buttons.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. Runtime JOIN-state verification still requires the VMware-built SigSvr and a successful login.

Rollback point: revert the affected files above; no packet layout or server behavior changed.

## Temporary PLAYSTREAM diagnosis trace

Goal: expose the exact boundary that turns a successful `OBTAINSTREAM` into `PLAYSTREAM(ERROR)`.

Affected files:

- `ECloudAssistant/Net/SigConnection.cpp`
- `ECloudAssistant/UI/center/RemoteManager.cpp`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`
- `ENET/SigServer/SigConnection.cpp`

Behavior: temporary `[TRACE-PLAY-20260814]` logs now record the controlled CREATESTREAM URL, `RtmpPushManager::Open` result, GDI/H264/audio/AAC/SPS-PPS initialization failures, RTMP `OpenUrl` result, controller PLAYSTREAM result, and SigServer CREATESTREAM reply result/address. SigServer now treats an explicit failed `CreateStreamReply_body::result` as a failed play response instead of relying only on an empty address.

Verification: client Debug build completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. SigServer must be rebuilt in VMware before its trace can appear:

```bash
cd /mnt/hgfs/shared/assient/ENET/build
make SigSvr -j4
```

Rollback point: remove only lines containing `[TRACE-PLAY-20260814]`; no protocol layout changed.

## Dynamic desktop capture dimensions

Goal: fix controlled-client streaming failures caused by a hard-coded `2560x1440` gdigrab capture area.

Affected files:

- `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`
- `ECloudAssistant/Codec/VideoEncoder.cpp`

Behavior: `GDIScreenCapture` no longer sets a fixed capture rectangle, so `gdigrab` uses the complete virtual desktop. After FFmpeg discovers the input stream, its reported dimensions are passed to the H.264 encoder. Encoder dimensions are rounded down to even values when necessary for YUV420/H.264; the source frame allocation continues to use the actual input dimensions.

Root-cause evidence: on 2026-08-14, the prior gdigrab command failed with `Capture area (0,0),(2560,1440) extends outside window area ...`, proving that the fixed rectangle was invalid on the controlled client.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. Runtime verification remains a controlled-client playback retry on a non-2560x1440 desktop.

Rollback point: restore the three source files above; no signaling packet layout or server behavior changed.
## Puller window and playback trace

Goal: remove the invalid `QMainWindow::setLayout` usage reported during playback and distinguish demux failure from first-frame decode/render failure.

Affected files:

- `ECloudAssistant/Puller/UI/PullerWgt.cpp`
- `ECloudAssistant/Puller/UI/AVPlayer.cpp`
- `ECloudAssistant/Codec/AVDEMuxer.cpp`
- `ECloudAssistant/Codec/H264_Decoder.cpp`

Behavior: `PullerWgt` now uses a central child widget for its layout. Temporary `[TRACE-PULL-20260814]` logs identify the RTMP demux open result, discovered stream indices, and first decoded video frame.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. Playback retry is pending to identify the final boundary.

Rollback point: restore the files above; remove only lines containing `[TRACE-PULL-20260814]` when diagnostics are complete.
## RTMP AAC sequence-header message type

Goal: fix FFmpeg puller failure (`avformat_open_input` returning `AVERROR(EIO)`) immediately after RTMP `Play.start`.

Affected files:

- `ECloudAssistant/Pusher/rtmp/RtmpPublisher.cpp`

Behavior: when the first H.264 keyframe is published, the AAC AudioSpecificConfig header is now sent through `SendAudioData` (RTMP type `0x08`) instead of `SendVideoData` (type `0x09`). The prior video-tag misclassification sent an AAC payload beginning with `0xAF` as video data, which makes an FFmpeg RTMP puller reject the stream before demux setup.

Root-cause evidence: `RtmpPublisher::PushVideoFrame` sent `avc_sequence_header_` as video and then incorrectly sent `aac_sequence_header_` through the same video method. Runtime puller trace reached RTMP `Play.start` then `avformat_open_input` failed with `-5` (`AVERROR(EIO)`).

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. Controlled-client playback retry remains pending.

Rollback point: restore the one `SendAudioData` call to the former `SendVideoData` call; no signaling packet layout changed.
## AAC sequence-header ownership and silent-frame logging

Goal: prevent the controlled client from terminating abnormally after RTMP publishing begins and keep audio-capture output readable.

Affected files:

- `ECloudAssistant/Pusher/rtmp/RtmpPublisher.cpp`
- `ECloudAssistant/Pusher/capture/WASAPICapture.cpp`

Behavior: the heap array used by `aac_sequence_header_` now has an explicit `std::default_delete<char[]>`, matching its `new char[]` allocation. `PushVideoFrame` now returns its declared success value. Repeated `AUDCLNT_BUFFERFLAGS_SILENT` output is removed; silent input is normal and continues to be filled with zero PCM samples.

Root-cause evidence: the supplied controlled-client log contained only normal silent-capture messages after publishing began, then ended with `ECloudAssistant.exe terminated abnormally`. The prior `shared_ptr<char>::reset(new char[...])` used scalar deletion for an array allocation, which is undefined behavior on release.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` on 2026-08-14. A two-client retry remains pending.

Rollback point: restore the explicit array deleter and the removed debug line; no protocol layout changed.
## Primary-monitor GDI capture range

Goal: stop streaming the complete multi-monitor virtual desktop and capture only the controlled machine's primary monitor.

Affected files:

- `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`

Behavior: before opening `gdigrab`, the client reads the primary monitor's `RECT` with the Windows monitor API, then passes that rectangle as `offset_x`, `offset_y`, and `video_size`. The capture size and offset are logged as `gdigrab primary monitor capture ...`; the discovered FFmpeg stream dimensions are logged as `gdigrab active capture size ...`. This replaces the former complete-virtual-desktop capture that produced the 4480x1440 multi-screen image.

The source file also contained damaged comments joined with adjacent C++ statements; those statements have been restored as normal C++ so the changed file can compile.

Verification: `GDISreenScapture.cpp` compiled successfully in the Debug build on 2026-08-14. The final link is currently blocked because `debug/ECloudAssistant.exe` is running and Windows returned `Permission denied` when replacing it. Runtime verification is pending after the application is closed and rebuilt.

Rollback point: restore `GDISreenScapture.cpp` to its prior valid version; no signaling or media protocol layout changed.

## Puller window closes its control session

Goal: prevent a closed puller window from leaving its controlling signal connection alive on SigServer.

Affected files:

- `ECloudAssistant/Puller/UI/PullerWgt.{h,cpp}`
- `ECloudAssistant/Puller/UI/AVPlayer.{h,cpp}`
- `ECloudAssistant/Net/TcpConnection.cpp`

Behavior: `PullerWgt::closeEvent()` now calls `AVPlayer::StopRemote()` before the window is hidden. The player stops its playback resources, disconnects the controlling `SigConnection`, and releases it so the socket closes and SigServer can clear the old controller-target relationship. `TcpConnection` now closes its Windows Winsock handle with `closesocket()` instead of the POSIX file-descriptor `close()` call; runtime evidence showed that the old controller remained registered on SigServer when the wrong close API was used. `RemoteManager` continues to own the hidden window through its existing `unique_ptr`; `WA_DeleteOnClose` is intentionally not used.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` also passed. Runtime verification requires two clients and the VMware SigServer: close the first puller window, confirm SigServer reports the controller disconnect, then start a second control session.

Commit ID: not created in this change.

Remaining limitations: this change does not remove the initial three-second push delay and does not address the controlled client's `PUSHER`-to-`IDLE` or `RtmpPushManager::exit_` restart state.

Rollback point: remove `PullerWgt::closeEvent()` and `AVPlayer::StopRemote()`, and restore the former socket cleanup call; no packet layout or server behavior changed.

## Idempotent SigServer connection cleanup

Goal: avoid running control-session cleanup twice when a client TCP connection closes.

Affected files:

- `ENET/SigServer/SigConnection.cpp`

Behavior: `SigConnection::Clear()` now returns immediately when the connection is already in `CLOSE` state. A normal disconnect first calls `Clear()` through `closeCb_`; `disconnectCb_` then removes the final server-owned connection reference, which invokes `SigConnection`'s destructor and calls `Clear()` again. The second call is now harmless and silent, so it cannot duplicate the `con size` log or cleanup side effects.

Verification: source inspection confirms the guard precedes all cleanup side effects. The Linux SigSvr must be rebuilt in VMware and tested by closing a puller window; the expected sequence has exactly one `con size: 2` for that control-session disconnect.

Commit ID: not created in this change.

Rollback point: remove the `state_ == CLOSE` early return; no packet layout changed.

## Controlled client can restart RTMP publishing

Goal: allow a controlled client to stop its current RTMP publisher after `DELETESTREAM` and start a later control session without restarting the application.

Affected files:

- `ECloudAssistant/Net/SigConnection.cpp`
- `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`

Behavior: when a controlled connection receives `DELETESTREAM` with `streamCount == 0`, it synchronously stops the old publisher and changes its local signal state from `PUSHER` back to `IDLE`. A later `CREATESTREAM` can therefore pass the existing IDLE check. `RtmpPushManager::Open()` resets its stop flags for every new publishing cycle. The cross-thread stop flags are now atomic, and `Close()` waits for the audio/video workers before releasing their publisher, encoder, and capture dependencies.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. Existing signedness, unused-parameter, and `INPUT` initializer warnings remain. End-to-end verification still requires two clients and the VMware servers: establish a stream, close the controller window, confirm the controlled client logs `[Sig] controlled stream stopped, state = IDLE`, then establish and render a second stream.

Commit ID: not created in this change.

Remaining limitation: this change does not add a release acknowledgement, so an immediate new control request can still reach SigSvr before the old TCP FIN is processed and be rejected once as already controlled.

Rollback point: remove the controlled-side IDLE transition, restore the non-atomic publisher flags, and restore the former `Open()`/`Close()` flag and resource order; no packet layout changed.

## Windows Select scheduler removes closed RTMP channels

Goal: allow a later RTMP connection to register a fresh `Channel` after the prior connection closes, including when Windows reuses the same socket handle.

Affected files:

- `ECloudAssistant/Net/SelectTaskScheduler.{h,cpp}`

Behavior: `SelectTaskScheduler` now overrides the base virtual `RmoveChannel(ChannelPtr&)`, which is the function called by `TcpConnection::Close()`. The old derived name `RemoveChannel()` did not override that function, so the base no-op could leave the old `SOCKET -> Channel` map entry and select fd sets behind. `UpdateChannel()` and `RmoveChannel()` are explicitly marked `override` so a future name or signature mismatch fails at compile time.

Verification: `git diff --check` passed. A forced Windows Debug rebuild with `mingw32-make.exe -B -f Makefile.Debug -j4` compiled `SelectTaskScheduler.cpp` and completed successfully on 2026-09-16; pre-existing compiler warnings remain. Runtime verification still requires the two-client/VMware scenario: establish a stream, close it, then establish and render a second stream repeatedly.

Commit ID: not created in this change.

Remaining limitation: this static fix proves the virtual dispatch path, but it cannot by itself prove the reported second-session RTMP symptom without a live RTMP/SigSvr run.

Rollback point: restore `RemoveChannel()` and remove the two `override` specifiers; no packet layout or server behavior changed.

## LoginResult USER_CODE diagnostic trace

Goal: make a login test distinguish an old LoginSvr packet layout, an incomplete TCP read, and an empty USER_CODE returned by the running server.

Affected files:

- `ENET/LoginServer/LoginConnection.cpp`
- `ECloudAssistant/UI/center/LoginWgt.cpp`

Behavior: the temporary `[TRACE-LOGIN-CODE-20260917]` logs were used to prove that the old running LoginSvr sent a 26-byte LoginResult while the rebuilt server sends the current 46-byte layout. The temporary client and server trace output was removed after that verification. No login decision, packet layout, or buffering behavior changed.

Verification: the diagnostic traces proved the old LoginSvr sent a 26-byte response. After the rebuilt server sent its 46-byte response and the stale database online flag was reset, the user confirmed login succeeded. The client must be rebuilt once more after removing the temporary logs.

Commit ID: not created in this change.

Remaining limitation: the client login parser still does not buffer partial TCP packets; this change only exposes that condition for a follow-up minimal framing fix.

## Windows select scheduler removes closed channels

Goal: allow a second RTMP connection to register a fresh `Channel` even when Windows reuses the socket handle from the first connection.

Affected files:

- `ECloudAssistant/Net/SelectTaskScheduler.{h,cpp}`

Root cause: `TcpConnection::Close()` calls `RmoveChannel()` through a `TaskScheduler*`, but `SelectTaskScheduler` declared and implemented `RemoveChannel()` instead. Because the names did not match, the call dispatched to the empty base implementation and left the old entry in `SelectTaskScheduler::channels_`. If a later socket reused that handle, `UpdateChannel()` found the stale key and did not replace its old `Channel` or callbacks.

Behavior: the Windows select scheduler now overrides the existing base-class `RmoveChannel()` spelling and erases the closed socket entry. Both `RmoveChannel()` and `UpdateChannel()` are marked `override` so a future signature mismatch fails at compile time. The base spelling is intentionally unchanged to avoid touching the Linux epoll scheduler or other modules. No FFmpeg option, RTMP packet, or signaling protocol changed.

Verification: source inspection confirms `EventLoop::Loop()` creates `SelectTaskScheduler`, `TcpConnection::Close()` calls `RmoveChannel()` through the base pointer, and the derived method now overrides that exact signature. `git diff --check` passed. The complete Windows Debug client build recompiled `SelectTaskScheduler.cpp` and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16.

Commit ID: not created in this change.

Remaining limitation: end-to-end verification still requires two clients and the running servers. Repeat the connect-close cycle three times; each close should produce `[Sig] controlled stream stopped, state = IDLE`, each reconnect should reach `OpenUrl -> Connect -> START_CONNECT -> CreateStream -> START_CREATE_STREAM -> publish`, and the controller should reach `decoded first video frame` without `avformat_open_input failed -5`, `std::bad_weak_ptr`, or a stale control session.

Rollback point: restore the former `RemoveChannel()` declaration and definition and remove the two `override` specifiers; no server rebuild or protocol rollback is required.

## RTMP publishing readiness replaces the fixed startup delay

Goal: remove the unconditional three-second pause before capture starts while still preventing audio/video data from being sent before the RTMP server accepts the publisher.

Affected files:

- `ECloudAssistant/Pusher/RtmpPushManager.cpp`
- `ECloudAssistant/Pusher/rtmp/RtmpConnection.{h,cpp}`
- `ECloudAssistant/Pusher/rtmp/RtmpPublisher.{h,cpp}`

Behavior: `RtmpConnection::is_publishing_` is now atomic and becomes true only after `HandleOnStatus()` receives `NetStream.Publish.Start`. `RtmpPublisher::IsPublishing()` exposes that state to `RtmpPushManager::Open()`. The manager replaces the fixed three-second sleep with a 10 ms polling interval and a five-second maximum timeout; it starts the audio/video capture threads only after publishing is confirmed. A disconnect before confirmation or a timeout closes the partially initialized publisher and returns failure. `OpenUrl() == 0` still means that TCP connected and the asynchronous RTMP handshake started; no RTMP or signaling packet changed.

Verification: source inspection confirms `isConnect` is set and the capture threads are created only after `IsPublishing()` succeeds, and no fixed three-second sleep remains. `git diff --check` passed after the code and worklog updates. `mingw32-make.exe -f Makefile.Debug -j4` recompiled the modified RTMP sources and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. Existing unused-parameter, member-initialization-order, and signedness warnings remain.

Commit ID: not created in this change.

Remaining limitation: end-to-end timing still requires two clients and the running servers. Confirm that `[TRACE-PLAY-20260814] RTMP publish ready` appears before capture produces media, startup no longer has a fixed three-second pause, a failed publisher returns within five seconds, and three connect-close-reconnect cycles still decode the first video frame.

Rollback point: restore `is_publishing_` to a plain boolean, remove the two `IsPublishing()` accessors, and restore the former fixed three-second sleep; no server rollback is required.

## Low-latency NVENC publishing with software fallback

Goal: reduce Windows client H.264 encoding latency and CPU usage by using the NVIDIA hardware encoder when available, while retaining a working software path on machines where NVENC cannot be opened.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.{h,cpp}`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`

Behavior: `VideoEncoder` now tries `h264_nvenc` first and falls back to `libx264` if the encoder is missing, its low-latency options are unsupported, or `avcodec_open2()` fails. Both paths use 25 FPS, GOP 25, no B-frames, YUV420P, global headers, and an 8 Mbps CBR target. NVENC uses `preset=p1`, `tune=ull`, `rc=cbr`, `zerolatency=1`, and `rc-lookahead=0`; libx264 retains `preset=ultrafast` and `tune=zerolatency`. The video loop is paced at 40 ms to match 25 FPS. Encoded frame PTS is now generated internally with a monotonic `pts_++`; the unused optional PTS argument that previously defaulted every frame to zero was removed. BGRA-to-YUV420P conversion remains on the CPU, and no RTMP, signaling, server, capture, or decoder protocol behavior changed.

Verification: the installed FFmpeg 6 build lists `h264_nvenc`, and a direct 1280x720/25 FPS NVENC smoke test on the NVIDIA GeForce RTX 4060 Laptop GPU encoded 25 frames successfully with the selected low-latency settings. `mingw32-make.exe -f Makefile.Debug -j4` recompiled `H264Encoder.cpp`, `VideoEncoder.cpp`, and `RtmpPushManager.cpp`, then linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` passed; only line-ending conversion warnings were reported. Existing unrelated unused-parameter and signedness warnings remain.

Commit ID: not created in this change.

Remaining limitations: a two-client runtime test is still required to confirm the application logs `H264 encoder selected: h264_nvenc`, publishes valid H.264 sequence parameters, reaches `decoded first video frame`, and maintains acceptable GPU Video Encode utilization and end-to-end latency over three connect-close-reconnect cycles. The `libx264` fallback path also requires a runtime test on a system where NVENC is unavailable. This change does not implement D3D/CUDA zero-copy, so screen capture and color conversion still consume CPU and copy memory.

Rollback point: restore generic `AV_CODEC_ID_H264` encoder selection, the former x264-only options and rate settings, the prior video pacing values, and the optional PTS argument; no server or protocol rollback is required.

## Roll back the NVENC publishing experiment

Goal: return to the previously measured software-encoding behavior after the NVENC version increased the user's local Windows-to-Linux test latency from about 91 ms to 179 ms.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.{h,cpp}`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`

Behavior: removed explicit `h264_nvenc` selection and its fallback wrapper, restored generic H.264 encoder discovery and the former `ultrafast`/`zerolatency` options, restored the prior 30-frame GOP, zero B-frames, 30 ms video-loop pacing, and the former 80000 kbps call-site value. The previous optional PTS parameter and its behavior were also restored. Earlier RTMP reconnect, publish-readiness, dynamic capture-size, and source-frame-dimension fixes remain unchanged.

Runtime evidence: the user measured 179 ms after the NVENC change in a local Windows-client/Linux-server test, compared with about 91 ms before that experiment. This establishes a regression in that tested end-to-end path, but it does not by itself isolate whether the extra delay came from NVENC buffering, frame pacing, bitrate, timestamping, or player buffering.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` recompiled the restored encoder sources and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` passed with only line-ending conversion warnings. Existing warnings remain, including the restored unsigned PTS comparison that is always true.

Commit ID: not created in this change.

Remaining limitation: end-to-end latency must be measured again with the same scene, network, server, and player settings to confirm the rollback returns to the prior baseline. The restored generic encoder choice depends on FFmpeg's registered H.264 encoder order.

Rollback point: reapply the preceding NVENC section if a controlled comparison later identifies and removes the added latency source; no server or protocol change is involved.

## NVENC retry with zero output delay

Goal: perform a controlled hardware-encoding latency comparison after the earlier NVENC test measured about 179 ms and FFmpeg 6 source inspection identified its default asynchronous output depth as the likely added two-frame delay.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.cpp`

Behavior: H.264 encoding is now forced to `h264_nvenc`; there is intentionally no software fallback so the runtime test cannot silently use libx264. The existing external test conditions remain 30 FPS scheduling, GOP 30, zero B-frames, the 80000 kbps call-site value, and the restored PTS behavior. NVENC is opened with `preset=p1`, `tune=ull`, CBR, disabled multipass, `zerolatency=1`, `rc-lookahead=0`, and the critical `delay=0`. `AV_CODEC_FLAG_LOW_DELAY` is enabled and the VBV buffer is limited to one frame of the configured bitrate. Initialization fails visibly if NVENC or any required option is unavailable. Successful initialization logs `H264 encoder selected: h264_nvenc, delay=0`. BGRA-to-YUV420P conversion remains on the CPU; RTMP, signaling, capture, and decoder behavior are unchanged.

Verification: the installed FFmpeg 6 executable encoded a 30-frame 1280x720 test source successfully with the same NVENC options, including `delay=0` and the one-frame VBV buffer. `mingw32-make.exe -f Makefile.Debug -j4` recompiled `VideoEncoder.cpp` and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` passed with only line-ending conversion warnings. The pre-existing unsigned PTS comparison warning remains.

Commit ID: not created in this change.

Remaining limitation: the exact Windows-client/Linux-server end-to-end latency cannot be automated in the current workspace. The user must repeat the same visual latency measurement used for the 81 ms software baseline and 179 ms prior NVENC result, confirm the NVENC selection log, and report several samples rather than one value. This test does not add GPU zero-copy, so CPU color conversion and CPU-to-GPU upload remain.

Rollback point: restore `avcodec_find_encoder(AV_CODEC_ID_H264)`, the former x264 `ultrafast`/`zerolatency` options, the former rate-control buffer size, and the global-header-only flag. No server or protocol rollback is required.

## Conclude the NVENC latency experiment and restore software encoding

Goal: end the current hardware-encoding experiment after it did not improve measured end-to-end latency, and return the client to the lower-latency software configuration while preserving all unrelated RTMP and restart fixes.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.cpp`

Final behavior: explicit `h264_nvenc` selection and all NVENC-only options were removed. The encoder again uses FFmpeg's generic H.264 encoder selection with `preset=ultrafast`, `tune=zerolatency`, global headers, GOP 30, zero B-frames, the existing 30 ms send-loop pacing, and the existing 80000 kbps call-site value. The RTMP publish-ready wait, Windows channel removal, controlled-client restart, dynamic capture size, and source-frame-dimension fixes remain unchanged.

Experiment environment and observations:

- Test path: Windows client to Linux server on the local test network, using the visual difference between a real clock and the received stream image.
- Software configuration: generic H.264 encoder with `ultrafast`/`zerolatency`; the user reported about 91 ms earlier and later measured about 81 ms.
- First NVENC configuration: `h264_nvenc`, `p1`, `ull`, CBR, zero B-frames, lookahead disabled, 25 FPS/GOP 25, 8 Mbps, and monotonic encoder PTS. The user measured approximately 179-180 ms. Because several parameters changed together, this run was not a clean encoder-only comparison.
- Controlled NVENC retry: retained the software version's external 30 FPS/GOP 30/80000 kbps/PTS conditions, forced NVENC with no fallback, and added `delay=0`, `AV_CODEC_FLAG_LOW_DELAY`, disabled multipass/lookahead, zero-latency tuning, and a one-frame VBV buffer. The user measured `1.756 s - 1.584 s = 0.172 s`, or 172 ms, and also observed values including 81, 144, and 172 ms across tests.

Conclusion: NVENC support and the `delay=0` option were proven functional by a direct FFmpeg smoke test, but neither NVENC experiment demonstrated lower end-to-end latency than the software baseline. The 172 ms result also shows that FFmpeg's default NVENC asynchronous output depth was not the only active source of delay. The available measurements are not sufficient to attribute the remaining variation specifically to a jitter buffer or Windows DWM. Static source inspection identified unbounded decoder packet/frame queues and a queued Qt repaint path as candidates, but no queue-depth runtime measurement was collected, so they remain hypotheses rather than confirmed causes.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` recompiled the restored `VideoEncoder.cpp` and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` passed with only line-ending conversion warnings. The pre-existing unsigned PTS comparison warning remains.

Commit ID: not created in this change.

Remaining limitation: a future latency investigation should keep one configuration variable per run, collect multiple samples, and timestamp or measure the capture, encoded-packet, demux, decoded-frame, queued-repaint, and actual-paint boundaries. Queue-depth instrumentation is needed before implementing frame dropping or claiming a player jitter-buffer root cause.

Rollback point: reapply the explicit NVENC selection and its tested low-latency options only when a new controlled experiment is ready; no server or protocol rollback is involved.

## Temporary software-encoder API timing log

Goal: measure the current software encoder call cost independently from capture, BGRA-to-YUV conversion, RTMP transport, decoding, and rendering.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.cpp`

Behavior: immediately before `avcodec_send_frame()` the encoder records a `std::chrono::steady_clock` timestamp, then records the end timestamp immediately after `avcodec_receive_packet()`. Each frame prints one concise line in the form `[ENCODE-TIME] <encoder-name> cost_us = <microseconds> ret = <receive-result>`. The log statement executes after the end timestamp. No encoder, rate-control, GOP, PTS, RTMP, or playback parameter changed.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` recompiled `VideoEncoder.cpp` and linked `debug/ECloudAssistant.exe` successfully on 2026-09-16. `git diff --check` passed with only line-ending conversion warnings. Runtime samples are pending the user's software-encoding test.

Commit ID: not created in this change.

Remaining limitation: for the current zero-latency software encoder, the interval closely represents synchronous encoding API cost. For a future asynchronous NVENC run, an `EAGAIN` result or a packet belonging to an earlier submitted frame means the same interval must not be interpreted as complete per-frame hardware latency without frame correlation.

Rollback point: remove the `<chrono>` include and the `[ENCODE-TIME]` timing block after the experiment.

## Disable the completed software-encoder timing log

Goal: stop the per-frame `[ENCODE-TIME]` output after the software-encoder timing test completed.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.cpp`

Behavior: the temporary `<chrono>` include, timestamp collection, elapsed-time calculation, and `qInfo()` output are commented out. The actual `avcodec_send_frame()` and `avcodec_receive_packet()` calls remain active, so video encoding behavior and parameters are unchanged.

Verification: `mingw32-make.exe -f Makefile.Debug -j4` recompiled `VideoEncoder.cpp` and linked `debug/ECloudAssistant.exe` successfully on 2026-09-17. The pre-existing unsigned PTS comparison warning remains.

Commit ID: not created in this change.

Remaining limitation: the commented timing code is intentionally retained for a possible repeat measurement and must be uncommented together with the `<chrono>` include.

Rollback point: uncomment the four timing statements, the log statement, and the `<chrono>` include. **Superseded**: that commented block was removed by the phase-one pipeline statistics change below; the same measurement now exists as live code in `VideoEncoder::Encode()`.

## Capture-to-encode pipeline statistics (low-latency phase one)

Goal: build a measurable baseline of the capture-to-encode path before changing any media behavior, following `context/采集和编码的低延迟优化.md` phase one. This change adds low-frequency statistics only; frame rate, pacing, encoder parameters, timestamps, RTMP, and playback are unchanged.

Affected files:

- `ECloudAssistant/Pusher/VideoPipelineStats.{h,cpp}` (new)
- `ECloudAssistant/Pusher/Pusher.pri`
- `ECloudAssistant/Pusher/capture/GDISreenScapture.{h,cpp}`
- `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`
- `ECloudAssistant/Codec/AV_Common.h`
- `ECloudAssistant/Codec/VideoEncoder.{h,cpp}`
- `ECloudAssistant/Codec/H264Encoder.{h,cpp}`

Behavior:

- `GDIScreenCapture` maintains a monotonic `capture_sequence_` and a `captured_at_` timestamp, both updated at the end of the whole-frame copy in `Decode()`. `CaptureFrame()` now fills a `CaptureFrameMeta` out-parameter (width, height, sequence, capturedAt) taken under the same mutex as the pixel copy, so the sequence always belongs to the returned picture. `GetCaptureSequence()` exposes the total captured-frame count for cross-thread sampling.
- `VideoEncodeTiming` (declared in `AV_Common.h`) carries `convertUs` and `encodeUs`. `VideoEncoder::Encode()` measures the BGRA-to-YUV420P conversion and the `avcodec_send_frame()`/`avcodec_receive_packet()` pair separately and reports them through an optional out-parameter; `H264Encoder::Encode()` forwards it.
- `RtmpPushManager` owns a `VideoPipelineStats` member. The encode loop computes `waitUs` from `meta.capturedAt` to the start of encoding, reports each successful encode, and calls `ReportIfDue()` once per iteration. Capture FPS is derived from the capture-side sequence delta, so frames the encode thread never saw are still counted. `VideoPipelineStats::kEnabled` is a single compile-time switch; `Reset()` re-baselines it for every new publishing cycle.
- Output is one line per second, tagged `[PIPE-STATS]`, with `captureFps`, `encodeFps`, `captured`, `encoded`, `dup` (same capture sequence encoded twice), `skip` (capture frames the encode thread missed), `waitAvgUs`, `waitMaxUs`, `convertAvgUs`, `encodeAvgUs`, and the last encoded sequence.
- The intentionally commented `[ENCODE-TIME]` block in `VideoEncoder.cpp` and its commented `<chrono>` include were removed, because this change reintroduces the same timing as live code.

No media behavior changed: gdigrab still runs at 25 FPS with the existing 30 ms encode-loop pacing, the encoder still uses 30 FPS / GOP 30 / `ultrafast` / `zerolatency` / 80000 kbps, and every frame still allocates and copies a full BGRA image under `mutex_`.

Verification: `qmake ECloudAssistant.pro -spec win32-g++ CONFIG+=debug` was re-run in `ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug` because source files were added to `Pusher.pri`. `mingw32-make.exe -f Makefile.Debug -j4` then compiled every target and linked `debug/ECloudAssistant.exe` successfully on 2026-09-17. The only warnings are the pre-existing ones: the always-true unsigned PTS comparison, unused `H264Encoder::GetSequenceParams` and `RtmpPushManager::IsKeyFrame` parameters, and the `EncodeAudio` signedness comparison. A direct check of `QString::arg` with `%1`..`%11` confirmed the report line substitutes in the correct order.

Runtime result on 2026-09-17 at 1920x1080: capture stayed near 12.5 FPS while encoding stayed near 25 FPS, with 11-14 duplicate encodes per second and no observed sequence skip. Capture-to-encode wait averaged about 33-39 ms and peaked at 66-81 ms; BGRA-to-YUV conversion averaged 6.7-7.5 ms and software H.264 encoding about 2.1-2.5 ms. This is strongly consistent with gdigrab's 25 FPS pacing being combined with the capture thread's 40 ms sleep. Stream shutdown returned the controlled client to `IDLE`, and the process exited normally. The first report line has a known interval-initialization bias and is excluded from the stable range above.

Commit ID: not created in this change.

Remaining limitation: fix the first-report interval bias and collect several matching end-to-end visual-latency samples. The AAC 64 bps/64 kbps unit issue is separate. Phase two is explicitly paused; no triple buffering, new-frame notification, or media-behavior change starts yet.

Rollback point: delete `VideoPipelineStats.{h,cpp}` and their `Pusher.pri` entries, restore the `CaptureFrame(FrameContainer&,quint32&,quint32&)` signature and `CaptureFrameMeta` removals, restore the `VideoEncoder::Encode`/`H264Encoder::Encode` signatures, remove `VideoEncodeTiming`, and drop the `stats_` member and its two call sites in `RtmpPushManager::EncodeVideo()`. No server or protocol change is involved.

## Single capture clock experiment (low-latency phase 1.5)

Goal: test whether the capture thread's own `sleep_for()` is what holds actual capture rate below the configured gdigrab rate, following `context/采集和编码的低延迟优化.md` section 7.2 and the phase 1.5 plan. Exactly one media-behavior variable changes.

Affected files:

- `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`
- `ECloudAssistant/Pusher/VideoPipelineStats.{h,cpp}`

Behavior:

- `GDIScreenCapture::run()` no longer sleeps between `GetOneFrame()` calls, so `av_read_frame()` is the only capture clock. `framerate_` is retained and still drives the gdigrab `framerate` option, so the configured 25 FPS source limit is unchanged. The `<thread>` include was dropped because that sleep was its only user.
- Every other media parameter is deliberately untouched, so the result stays a single-variable comparison: gdigrab 25 FPS, encoder 30 FPS / GOP 30 / `ultrafast` / `zerolatency` / 80000 kbps, the 30 ms `EncodeVideo()` polling loop, the mutex-protected whole-frame copy, and the existing PTS handling.
- `VideoPipelineStats::kEnabled` is set back to `true`, because the phase 1.5 acceptance criteria are read from `[PIPE-STATS]` output. It had been switched to `false` after the phase-one baseline was captured.
- First-report interval bias fixed (phase-one remaining task 1). `ReportIfDue()` previously left `encodedFrames_` holding the frame that the first loop iteration had already encoded, while `intervalBegin_` and `lastCapturedSequence_` were only set afterwards. Window one therefore over-counted encoded frames by one and under-counted captured frames by one, which made the first line show a high `encodeFps` and a low `captureFps`. A new private `ResetInterval()` clears only the per-interval accumulators, and is now called from `Reset()` and from the first `ReportIfDue()` call; the sequence-comparison state is intentionally preserved so duplicate and skip detection stays continuous. This changes report output only, not media timing.

Verification: full clean rebuild with the Qt toolchain, `mingw32-make.exe -f Makefile.Debug clean` followed by `-j4`, linked `debug/ECloudAssistant.exe` (30,737,664 bytes) successfully on 2026-09-17, and a follow-up run reported "Nothing to be done". The remaining warnings are all pre-existing.

Runtime result at 1920x1080: six complete `[PIPE-STATS]` periods averaged about 24.9 capture FPS and 23.9 encode FPS. Duplicate encodes fell to 0-3 per second, sequence skips were 1-4 per second, average capture-to-encode wait fell to 17-23 ms, and the maximum fell to 40-52 ms. BGRA-to-YUV conversion remained at 6.7-7.4 ms and software H.264 encoding at 2.5-3.2 ms. This confirms that the removed 40 ms sleep was the main reason phase one captured only about 12.5 FPS. The first-report bias was fixed before this run, so the first line is valid. Stream startup succeeded.

Commit ID: not created in this change.

Remaining limitation: matching end-to-end visual-latency samples are still required. `GetOneFrame()` returns immediately when `av_read_frame()` fails, so a persistent capture error can now spin the capture thread at full CPU instead of being capped at 25 iterations per second. Phase two remains paused; no triple buffering or new-frame notification starts yet.

Rollback point: restore the `sleep_for(1000 / framerate_)` call in `GDIScreenCapture::run()` and the `<thread>` include. The statistics fix is independently revertible by inlining `ResetInterval()` back into `Reset()` and the report tail and removing the call in the first-`ReportIfDue` branch. No server or protocol change is involved.

## Build environment: Qt MinGW must precede msys2 on PATH

Goal: record a build-environment trap that cost a full debugging cycle and can silently produce a mismatched binary.

Behavior: this shell's `PATH` resolves both `g++` and `mingw32-make` to `C:\msys64\ucrt64\bin`, which is GCC 15.2.0. Qt 6.10.1 here is built with MinGW 13.1.0 from `D:\Qt\Tools\mingw1310_64`, and the generated `Makefile.Debug` only injects that compiler's *include* directory, never its `g++`. Compiling with the msys2 compiler therefore mixes toolchains: the compile step appears to succeed, and the link then fails with a bare `collect2.exe: error: ld returned 1 exit status` and no diagnostic line. Worse, the failed link still leaves `debug/ECloudAssistant.exe` newer than every object file, so the next `make` reports "Nothing to be done for 'first'" and hides the failure.

A second, smaller trap in the same Makefile: qmake hard-codes the moc tool as `D:\Qt\6.10.1\mingw_64\bin\moc.exe` with backslashes, which msys `sh` strips when it parses the recipe, giving `D:Qt6.10.1mingw_64binmoc.exe: command not found`. `SHELL=cmd.exe` avoids it, and is consistent with the Makefile already using `del` and `move`.

Verification: `mingw32-make.exe -f Makefile.Debug clean` and then `-j4` both succeeded on 2026-09-17 with `PATH` set to `D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.10.1\mingw_64\bin;%SystemRoot%\system32;%SystemRoot%`. Building from Qt Creator is unaffected, because that kit already exports the correct environment.

Commit ID: not created in this change.

Remaining limitation: the correct `PATH` lives only in the shell invocation, not in the repository. Any scripted build must set it explicitly.

Rollback point: not applicable; this is a note, no file changed.

## Triple buffering and new-frame notification (low-latency phase 2)

Goal: implement phase two of `context/采集和编码的低延迟优化.md` — remove the remaining fixed polling wait between capture and encode, the mutex-protected whole-frame copy, and the per-frame heap allocation of the BGRA buffer. Capture and encode no longer keep independent clocks: a condition variable wakes the encode thread the moment a new frame lands, and a frame the encoder cannot keep up with is overwritten rather than queued.

Affected files:

- `ECloudAssistant/Pusher/capture/GDISreenScapture.{h,cpp}`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`
- `ECloudAssistant/Codec/VideoEncoder.{h,cpp}`
- `ECloudAssistant/Codec/H264Encoder.{h,cpp}`
- `ECloudAssistant/Codec/AV_Common.h`

Behavior:

- `CaptureFrameMeta`, `CaptureFrame(FrameContainer&, CaptureFrameMeta&)`, `rgba_frame_`, `frame_size_`, the old `mutex_`, and the `FrameContainer` alias are gone. Whole frames now live in three fixed `CaptureFrameBuffer` members, allocated once in `Init()`.
- New public `CaptureFrameView` is the encode thread's read-only view of the front buffer, valid until the next `WaitLatestFrame()` or `Close()`. New public `WaitLatestFrame(CaptureFrameView&)` blocks on the condition variable, and new public `RequestStop()` is idempotent: it only sets the stop flag and notifies, never joining or freeing. `Close()` calls `RequestStop()` first, so calling `Close()` directly cannot deadlock.
- `Decode()` copies rows from the decoded frame into `back` entirely outside the lock, stamps sequence and timestamp, then takes a short critical section that only swaps `back`/`middle` and sets `hasNewFrame_`, and notifies after unlocking. Target row stride is the pool's compact stride; source row stride is `av_frame->linesize[0]`.
- `WaitLatestFrame()` waits on the predicate `hasNewFrame_ || stopped_`. The `stopped_` test happens before the index swap, so a stop never perturbs the indices.
- Pool sizing uses `av_image_get_linesize(kCapturePixelFormat, width, 0)` rather than a hardcoded `width * 4`, so the pool and the encoder share one format source of truth. A non-positive result fails `Init()` instead of continuing with a wrong stride.
- Ownership invariant, maintained under `frameMutex_`: the three indices are pairwise distinct and cover {0,1,2}. Capture only touches {back, middle} and encode only touches {front, middle}, and both preserve distinctness. The `back` a capture thread snapshots outside the lock can therefore never be the `front` the encode thread just swapped in, so the lock-free write target is always private and three buffers suffice.
- `stop_` and `is_initialzed_` are now `std::atomic<bool>`, removing a pre-existing cross-thread data race. `stopped_`, the frame-handover stop signal, is deliberately kept separate from `stop_`, the capture thread's exit signal.
- `RtmpPushManager::Close()` ordering is the deadlock-critical part: `exit_ = true` and `isConnect = false`, then `screen_Capture_->RequestStop()` **before** `StopEncoder()` joins the encode thread, then the pusher close, then `StopCapture()` which calls `screen_Capture_->Close()`. Without the early `RequestStop()`, the encode thread would already be blocked on the condition variable when `StopEncoder()` joined it and `Close()` would hang forever. The buffer pool is freed in `StopCapture()`, so it must outlive the video thread join. `RequestStop()` depends only on the capture class's own `stopped_`, not on `exit_`, so the capture class does not gain a reverse dependency on the pusher.
- `EncodeVideo()` lost `static Timestamp`, the local `fameRate`, and the `sleep_for()`. It now loops on `WaitLatestFrame()` and is driven purely by new frames. It logs `[PIPE-STATS] encode thread exit, captured = ...` on exit so a stall can be told apart from a deadlock. BGRA-to-YUV420P conversion stays on the encode thread, so the thread boundary is unchanged.
- Three latent encoder-side defects were fixed while the code was open, all dormant at 1920 width. First, the converter and `rgba_frame_` guard compared the input size against the encoder size, but `RtmpPushManager::Init()` rounds the encoder size down to even while the input keeps the raw capture size, so an odd width made that branch true on every frame — rebuilding the sws context each frame and calling `av_frame_get_buffer` again on an already-allocated frame, which FFmpeg documents as a leak plus undefined behavior. The guard now compares new `sourceWidth_`/`sourceHeight_` members and calls `av_frame_unref()` before reallocating. Second, the single whole-frame `memcpy` was replaced by a row-wise copy using `rgba_frame_->linesize[0]` as the destination stride, because `av_frame_get_buffer(..., 32)` aligns that stride to 32 and the old code was only correct when `width * 4` happened to divide by 32. Third, `codec_context_->pix_fmt = AV_PIX_FMT_RGBA` was dead code, unconditionally overwritten by the next line's `avcodec_parameters_to_context()`, so neither side's declared format was actually in force; the format now comes from one `constexpr AVPixelFormat kCapturePixelFormat = AV_PIX_FMT_BGRA` in `AV_Common.h`, derived from by both the capture pool stride and `H264Encoder::OPen()`, and the first decoded frame logs its measured format and `linesize[0]`.
- `H264Encoder::Encode` and `VideoEncoder::Encode` take `const quint8*` (so `view.data` passes without a `const_cast`), and their now-redundant `size` parameter was removed — the row-wise copy derives the source length from `width` alone, and leaving a parameter that must equal `width * height * 4` would invite a caller to pass a value that is silently ignored.
- Encoding settings are deliberately unchanged this phase, so the comparison against phase 1.5 stays clean: gdigrab 25 FPS, encoder 30 FPS / GOP 30 / `ultrafast` / `zerolatency` / 80000 kbps, and the existing PTS handling that phase 3B will address.

Verification: built with the Qt MinGW toolchain (see the PATH note above). The four touched sources were force-recompiled to surface their warnings; the only four warnings are pre-existing ones in `H264Encoder::GetSequenceParams`, the deferred phase 3B `pts >= 0` test, and `EncodeAudio`/`IsKeyFrame`. The prior link had reported nothing after the link line, so the binary was confirmed genuine by checking the executable timestamp and by a follow-up `make` reporting "Nothing to be done for 'first'".

Runtime result at 1920x1080: capture and encode both stayed near 25 FPS with equal per-period counts, `dup = 0`, and `skip = 0`. Capture-publish to encode-thread wake averaged 27-39 us and peaked at 37-230 us, compared with 17-23 ms average and 40-52 ms maximum in phase 1.5. BGRA-to-YUV conversion stayed at 6.8-7.2 ms and software H.264 encoding at 1.7-2.5 ms. The core triple-buffer handoff and new-frame notification acceptance passed.

Remaining runtime checks: close and reconnect repeatedly, observe long-run CPU and memory stability, confirm the decoded capture format and colors, and collect matching visual end-to-end latency samples. `waitUs` now reflects only condition-variable wake latency; it does not include capture, conversion, encoding, transport, decoding, or rendering. `ReportIfDue()` remains the last call in each iteration, so statistics stop when capture stops producing; this is acceptable for the temporary module.

Commit ID: this local commit; resolve with `git log -1 --oneline` after creation.

Rollback point: revert the whole `git diff` across the five changed units above; nothing else is needed. No server, protocol or message-structure change is involved. The `VideoPipelineStats` module is untouched by this change and stays in place until phase 3 acceptance.

## Unified 30 FPS target (low-latency phase 2.5)

Goal: raise the whole capture-to-encode path from about 25 FPS to 30 FPS, as step two of phase 2.5 in `context/采集和编码的低延迟优化.md`. Only the framerate changes; bitrate, GOP, B-frames, resolution, the triple-buffer implementation and PTS behaviour are all deliberately left alone so the CPU, skip/dup and latency effects of the higher frame count can be read on their own.

Affected files:

- `ECloudAssistant/Codec/AV_Common.h`
- `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`
- `ECloudAssistant/Pusher/RtmpPushManager.cpp`

Behavior: the encoder side was already configured for 30 FPS, so nothing about it needed changing — `RtmpPushManager::Init()` passed 30, and `VideoEncoder` sets `time_base = 1/30`, `framerate = 30/1`, `gop_size = 30` and `max_b_frames = 0`. The only thing capping the pipeline was the capture side: `GDIScreenCapture::framerate_` was 25 and feeds gdigrab's `framerate` option. Since phase two removed the encode thread's own polling clock, the encode rate has followed the arrival rate ever since, so lifting gdigrab to 30 lifts both ends together. A single `constexpr qint32 kTargetFramerate = 30` in `AV_Common.h` now supplies both the capture-side gdigrab rate and the framerate argument to `H264Encoder::OPen()`, which is the same single-source-of-truth treatment the pixel format already got via `kCapturePixelFormat`, and is what the document's section 7.1 asks for. The constant's comment notes that GOP is intentionally excluded, because `VideoEncoder` hardcodes `gop_size = 30` and it does not follow the framerate. `H264Encoder::OPen()` was already receiving 30, so the encoder's effective configuration is numerically unchanged.

Verification: rebuilt with the Qt MinGW toolchain; the link succeeded, the executable timestamp advanced, and a follow-up `make` reported "Nothing to be done for 'first'". No new warnings.

Runtime result: five reported periods averaged about 30.0 capture FPS and 30.0 encode FPS. All 153 frames stayed 1:1 with `dup = 0` and `skip = 0`. Average handoff wait was about 34 microseconds; maximum wait was normally below 0.1 ms with one 0.5 ms sample. BGRA-to-YUV conversion averaged about 6.93 ms and software H.264 encoding about 2.08 ms, for about 9.01 ms combined against the 33.3 ms frame budget. The phase 2.5 core pipeline result therefore passes.

Remaining runtime checks: whole-machine CPU, actual bitrate, end-to-end visual latency, long-run stability, and repeated stop/reconnect behavior.

Commit ID: not created in this change.

Rollback point: restore `kTargetFramerate` to 25 in `AV_Common.h`, or revert the three files above. The 25 FPS baseline blocks in the document remain valid as the comparison arm. No server, protocol or message-structure change is involved.

## 60 FPS stress test (low-latency phase 2.6)

Goal: phase 2.6 in `context/采集和编码的低延迟优化.md` raises the whole path to 60 FPS to find the ceiling of the 1920x1080 software encode chain. The previously planned 30 FPS memory baseline was cancelled by user instruction; the test now goes directly to 60 FPS and judges sustainability primarily from actual capture rate, capture/encode 1:1 behavior, and `skip`.

Affected files:

- `ECloudAssistant/Codec/VideoEncoder.cpp`
- `ECloudAssistant/Codec/AV_Common.h`
- `build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug/Makefile{,.Debug,.Release}` (regenerated)

Behavior: `kTargetFramerate` is now 60 and continues to drive both gdigrab capture and the encoder time base. `VideoEncoder::Open()` previously set `codecContext_->gop_size = 30` as a literal; it now reads `config_.video.gop`. `H264Encoder::OPen()` sets that field from the target framerate, so the 60 FPS run also uses GOP 60 and retains an approximately 1-second keyframe interval. Bitrate (80000, which `H264Encoder::OPen` scales to 80 Mbps), B-frames, resolution, PTS semantics and the triple-buffer implementation are untouched.

Verification: after switching the target to 60, a full Debug rebuild with the Qt MinGW toolchain recompiled the `AV_Common.h` dependents and linked `debug/ECloudAssistant.exe` successfully. The reported warnings are pre-existing deprecation, signedness and unused-parameter warnings; no new build error was introduced.

### Discovered: the generated Makefile had incomplete header dependencies

While flipping `kTargetFramerate` to 60 to check the build, `RtmpPushManager.o` recompiled but `debug/GDISreenScapture.o` did not, even though `GDISreenScapture.cpp` includes `Codec/AV_Common.h` and reads the constant. The generated `Makefile.Debug` listed 62 object rules but only 27 of them had `Codec/AV_Common.h` as a prerequisite; the rule for `debug/GDISreenScapture.o` had been generated before phase 2 added that include.

Because a `constexpr` is baked into each object at compile time, that produced a mixed binary — gdigrab still at 30 FPS while the encoder's `time_base` said 1/60. It built cleanly, linked, and carried a normal filename and timestamp, so it would have been run and produced meaningless framerate data. This is a live hazard for the phase 2.6 comparison and for any future single-constant experiment.

Fixed by regenerating the makefiles in the standard layout from the build directory:

```bash
qmake.exe -o Makefile ..\..\ECloudAssistant.pro -spec win32-g++ CONFIG+=debug CONFIG+=qml_debug
```

`debug/GDISreenScapture.o`'s rule now lists `AV_Common.h`, and a rebuild recompiled that one file and relinked, confirming the fix. Two traps found along the way: `qmake_all` is an empty target in the sub-makefile and does nothing; and passing `-o Makefile.Debug` makes qmake treat that as the top-level makefile name, writing the real rules to `Makefile.Debug.Debug` and clobbering the working layout. Only command-line builds are affected — Qt Creator regenerates makefiles itself.

Runtime result: five periods averaged about 40.1 capture FPS and 40.1 encode FPS although the source and encoder were configured for 60 FPS / GOP 60. All 202 frames stayed 1:1 with `dup = 0` and `skip = 0`. Average handoff wait was about 42 microseconds, maximum wait ranged from about 0.15 to 0.55 ms, BGRA-to-YUV conversion averaged about 6.71 ms, and software H.264 encoding about 2.11 ms, for about 8.81 ms combined.

Conclusion: the 60 FPS target was not reached. Because capture itself stayed near 40 FPS while encode matched it exactly without skips, the observed limit is before the encode thread; the current measurements do not distinguish gdigrab, desktop capture, or OS scheduling as the specific cause. The triple-buffer handoff and encoder remained stable at the delivered rate.

Commit ID: not created in this change.

Rollback point: restore `kTargetFramerate` to 30 and, if required, revert `VideoEncoder.cpp`'s `gop_size` line to the literal `30`. The regenerated makefiles are build output and need no rollback.

## 640x480 gdigrab area experiment (low-latency phase 2.7)

Goal: distinguish pixel-volume cost from fixed per-frame cost after the 60 FPS / 1920x1080 run levelled off near 40 FPS.

Affected files:

- `ECloudAssistant/Codec/AV_Common.h`
- `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`
- `context/采集和编码的低延迟优化.md`

Behavior: during the experiment, gdigrab captured a temporary 640x480 rectangle at the primary monitor's top-left corner and the encoder automatically opened at the same size. After collecting the result, the temporary rectangle was removed: capture once again uses the full primary-monitor rectangle, which is 1920x1080 on the test machine. `kTargetFramerate` was restored from 60 to 30, so capture, encoder time base and configured GOP are again unified at 30. Bitrate, PTS, triple buffering and thread behavior remain unchanged. The abandoned timing instrumentation is not present.

Verification: the Qt MinGW Debug build recompiled the capture and related units and linked `debug/ECloudAssistant.exe` successfully. Only the two pre-existing `RtmpPushManager.cpp` signedness and unused-parameter warnings appeared. Across four complete runtime periods, capture and encode averaged about 54.8 FPS (51.0-56.8 FPS), and all 222 frames stayed 1:1 with `dup = 0` and `skip = 0`. Weighted averages were about 28 us handoff wait, 1.10 ms BGRA-to-YUV conversion and 0.52 ms H.264 encoding. The incomplete fifth line reported 55.3 FPS and was excluded from the averages.

Conclusion: reducing the area from 1920x1080 to 640x480 raised throughput from about 40.1 to 54.8 FPS, a 36.5% increase. Pixel-volume work is therefore a material part of the limit, but not the only part because the small-area run still did not sustain 60 FPS. The encoder is not the limiting stage at the delivered rate; fixed GDI call cost, OS/VM scheduling, and same-machine contention remain possible contributors and are not distinguished by this experiment.

Final disposition: the Qt MinGW Debug build after restoring full-primary-monitor capture and 30 FPS completed and linked `debug/ECloudAssistant.exe` successfully. The stable configuration is therefore the phase 2.5 setting: 1920x1080 on the current monitor, 30 FPS and GOP 30. Existing FFmpeg deprecation, signedness and unused-parameter warnings remain; no new build error was introduced.

Commit ID: not created in this change.

Remaining limitation: the restored binary has been built but not rerun end to end in this step. Phase 2.5 previously validated the same 1920x1080/30 FPS configuration.

Rollback point: the temporary 640x480 experiment is intentionally not retained. Reproducing it requires setting the local capture dimensions back to 640x480 and the target framerate to 60.

Documentation update: phase 2.7 now contains source-grounded tables for the restored video and audio settings. The important bitrate distinction is recorded explicitly: video passes 80000 kbps and stores 80,000,000 bit/s in FFmpeg, while the AAC path currently passes `64` straight into `AVCodecContext::bit_rate`, so its effective setting is 64 bit/s rather than the likely intended 64 kbps. This update documents the issue only and does not change encoder behavior.

## AAC bitrate unit conversion

Goal: make the existing `AACEncoder::Open(..., bitrate_kbps)` interface apply its documented kbps unit.

Affected files: `ECloudAssistant/Codec/AAC_Encoder.cpp` and the phase 2.7 parameter table.

Behavior: `bitrate_kbps` is multiplied by 1000 before reaching FFmpeg, so the existing caller value `64` now configures 64,000 bit/s instead of 64 bit/s. No video, capture, RTMP or thread setting changed.

Verification: the Qt MinGW Debug build recompiled `AAC_Encoder.cpp` and linked `debug/ECloudAssistant.exe` successfully. Runtime confirmation should show that `[aac] Bitrate 64 is extremely low` no longer appears.

Commit ID: not created in this change. Remaining limitation: runtime audio quality and warning removal have not yet been observed. Rollback point: remove the `* 1000` conversion in `AAC_Encoder.cpp`.

Documentation plan: added low-latency phase 2.8 for a future H.264 compatibility change. The proposed target is 1920x1080 at 30 FPS, 12 Mbps, Baseline profile, Level 4.0, GOP 30 and no B-frames. No encoder code or runtime behavior was changed by this documentation-only update.

## H.264 bitrate and Level compatibility convergence (low-latency phase 2.8)

Goal: stop the encoder from being pushed to H.264 Level 5.0 by the 80 Mbps target, and pin the profile explicitly, so that hardware decoders that only accept lower levels can still play the stream. Resolution, framerate, GOP, PTS, capture and triple buffering are unchanged.

Affected files: `ECloudAssistant/Pusher/RtmpPushManager.cpp` (video bitrate 80000 -> 12000 kbps), `ECloudAssistant/Codec/VideoEncoder.cpp` (`profile` and `level` set before `avcodec_open2`).

Behavior: `H264Encoder::OPen` still multiplies the kbps argument by 1000, so `bit_rate`, `rc_min_rate`, `rc_max_rate` and `rc_buffer_size` all become 12,000,000 bit/s, keeping the one-second VBV duration. `codecContext_->profile` is `FF_PROFILE_H264_BASELINE` and `codecContext_->level` is 40 (Level 4.0). No other call site sets a video bitrate, and `MediaInfo` carries no bitrate field, so nothing on the signaling or RTMP path needed a change.

Verification: the Qt MinGW Debug build recompiled exactly the two edited translation units and linked `debug/ECloudAssistant.exe`; `make -f Makefile.Debug -q` reports the tree up to date. Runtime logs now confirm `profile Constrained Baseline, level 4.0`, `bitrate=12000`, `vbv_maxrate=12000`, `vbv_bufsize=12000`, GOP 30 and no B-frames. Three complete pipeline periods totalled 92 captured and encoded frames at about 30.0 FPS with `dup = 0` and `skip = 0`; weighted averages were about 30 us handoff wait, 7.00 ms conversion and 2.42 ms encoding. The encode thread exited and the controlled side returned to `IDLE` normally. The short-run x264 summary reported about 12.53 Mbps.

Two implementation details were checked against FFmpeg n6.0 `libavcodec/libx264.c` before writing the change. First, `FF_PROFILE_H264_CONSTRAINED_BASELINE` must not be used: the libx264 wrapper maps only `FF_PROFILE_H264_BASELINE`, `MAIN`, `HIGH`, `HIGH_10`, `HIGH_422` and `HIGH_444`, and Constrained Baseline falls through to an empty `default`, so the profile string is never set and the setting is silently ineffective. `FF_PROFILE_H264_BASELINE` combined with the existing `max_b_frames = 0` and `ultrafast` (which disables CABAC) still produces a Constrained Baseline bitstream, which is why the log string is expected to be unchanged and only the level differs. Second, Level 4.0 limits 1920x1080 to 30 FPS: `MaxMBPS` is 245760 and 120x68 = 8160 macroblocks per frame times 30 gives 244800, leaving 0.4% headroom, while `MaxFS`, `MaxBR` and `MaxCPB` are all satisfied. Raising the framerate above 30 would exceed the declared level, so the constant carries that note at its assignment site.

Commit ID: not created in this change.

Remaining limitation: the profile part is a pin rather than an observable change, because the previous parameter set already produced a Constrained Baseline bitstream; the real deltas are the bitrate and the level. Picture quality at 12 Mbps and actual hardware-decoder selection on the target device have not been observed yet, and the 16 Mbps fallback recorded in the phase plan has not been tried. The lower bitrate also removes an unrelated risk: 80 Mbps is close to the ceiling of a 100 Mbps LAN, so the change is expected to reduce serialization delay and congestion jitter at the same time.

Rollback point: restore `12000` to `80000` in `RtmpPushManager.cpp` and delete the two `codecContext_->profile` / `codecContext_->level` assignments in `VideoEncoder.cpp`.

## Profile switch from Baseline to Main (low-latency phase 2.8 revision)

Goal: replace the declared H.264 profile with Main, which Level 4.0 decoders also support broadly and which permits CABAC.

Affected file: `ECloudAssistant/Codec/VideoEncoder.cpp` only. `codecContext_->profile` is now `FF_PROFILE_H264_MAIN`; the bitrate, level, GOP, B-frame and preset/tune settings from the first phase 2.8 pass are unchanged.

Behavior: no change to the coded bitstream beyond the SPS `profile_idc`, which moves from 66 to 77. The x264 preset in use (`ultrafast`) sets `b_cabac = 0`, `b_deblocking_filter = 0`, `b_transform_8x8 = 0`, `i_subpel_refine = 0`, `i_frame_reference = 1`, `analyse.inter = 0` and `i_aq_mode = 0`, so every tool that Main adds over Baseline is switched off anyway. The profile declaration therefore changes compatibility signalling, not picture quality or encode cost, and the earlier measured timings (7.00 ms convert plus 2.42 ms encode) are expected to carry over.

Two related findings are recorded in the phase 2.8 document. First, if the objective is only compatibility, Constrained Baseline is the more conservative declaration, so declaring Main without enabling CABAC overstates capability instead of using it. Second, the quality lever is CABAC, not the profile name: there is no AVOption named `cabac` in the FFmpeg libx264 wrapper, CABAC is controlled by the `coder` option (`cavlc` / `ac`) or by passing `cabac=1` through `x264-params`, and a naive `av_opt_set(priv_data, "cabac", "1", 0)` fails on a missing option, which is the same class of silent failure as the unhandled `FF_PROFILE_H264_CONSTRAINED_BASELINE` case. Main is also the safer base for that later step, because if `x264_param_apply_profile` runs after the coder option, Baseline would force CABAC back off.

Verification: the Qt MinGW Debug build recompiled `VideoEncoder.o` alone and linked `debug/ECloudAssistant.exe`. Runtime re-verification is outstanding: the encoder log should now read `Main` with Level 4.0, and capture/encode should remain at 30 FPS with 1:1 frames and `skip = 0`.

Commit ID: not created in this change.

Remaining limitation: the profile switch has not been run end to end, and hardware decode plus subjective quality at 12 Mbps are still unconfirmed. If quality proves insufficient, CABAC should be tried before raising the bitrate to 16 Mbps.

Rollback point: set `codecContext_->profile` back to `FF_PROFILE_H264_BASELINE`, or delete the assignment to return to the encoder default of the previous phase.

## Profile reverted to Baseline (low-latency phase 2.8 final state)

Goal: settle the declared H.264 profile. Main was tried and then reverted, because the comparison preparation showed it changes nothing under the current preset.

Affected file: `ECloudAssistant/Codec/VideoEncoder.cpp` only. `codecContext_->profile` is `FF_PROFILE_H264_BASELINE` again, with `level = 40` and the 12 Mbps bitrate unchanged. The code is now byte-for-byte the state that phase 2.8 already ran and measured, so no re-run is required to reuse those results.

Behavior: the encoder log line stays `profile Constrained Baseline, level 4.0`. Keeping Baseline is the more conservative and more truthful declaration, because Main adds no capability that the `ultrafast` preset actually enables: x264 sets `b_cabac = 0`, `b_deblocking_filter = 0`, `b_transform_8x8 = 0`, `i_subpel_refine = 0`, `i_frame_reference = 1`, `analyse.inter = 0` and `i_aq_mode = 0` for that preset, so switching the declaration would only widen the advertised compatibility envelope from `profile_idc` 66 to 77.

Verification: the Qt MinGW Debug build recompiled `VideoEncoder.o` alone and linked `debug/ECloudAssistant.exe`. The earlier phase 2.8 runtime results remain valid since the encoder configuration is unchanged.

Commit ID: not created in this change.

Remaining limitation: hardware decode on the target device and subjective quality at 12 Mbps are still unconfirmed. If quality turns out to be insufficient, the next single-variable step is CABAC rather than a higher bitrate, which requires naming the parameter through the `coder` option or `x264-params` because no AVOption called `cabac` exists, and would also require moving the profile to Main so that `x264_param_apply_profile` does not force CABAC back off.

Rollback point: none needed; this change restores the previously verified state.

## Phase 2 end-to-end latency baseline

Goal: record the first-layer preview latency in the final phase 2 summary before phase 3 changes PTS behavior.

Affected file: `context/采集和编码的低延迟优化.md` only; no source code changed.

Verification: nine measurements ranged from 44 to 67 ms and averaged about 59.33 ms. Most samples were between 63 and 67 ms, for a 23 ms overall spread. One recorded pair was 22.554 s at the reference page and 22.491 s at the preview, which equals 63 ms.

Commit ID: not created in this change. Remaining limitation: shutdown/reconnect repetition and long-duration stability remain supplementary phase 2 checks.

## Phase 3 PTS semantics fix (low-latency phase 3)

Goal: remove the unsigned optional-PTS sentinel that made every encoded frame carry PTS 0, and make the encoder PTS a required, caller-supplied, monotonic frame index derived from the capture sequence. No change to framerate, GOP, bitrate, encoder options or the RTMP send timestamps.

Affected files: `ECloudAssistant/Pusher/RtmpPushManager.cpp` (`EncodeVideo` computes `framePts`), `ECloudAssistant/Codec/H264Encoder.{h,cpp}` (required `qint64 pts` before the optional `timing`), `ECloudAssistant/Codec/VideoEncoder.{h,cpp}` (required `qint64 pts`, `out_frame->pts = pts`, removed the `pts_` member), `ECloudAssistant/Codec/AudioEncoder.cpp` (removed one dead assignment).

Behavior: `VideoEncoder::Encode` was declared `..., quint64 pts = 0` and assigned `out_frame->pts = pts >= 0 ? pts : pts_++`. An unsigned parameter is never negative, so the condition was always true and every frame received PTS 0 while `pts_++` never ran; `H264Encoder::Encode` did not expose a PTS argument at all, so callers could not supply one. The parameter is now a required `qint64` on both layers with no default, which closes the "caller forgets, encoder silently receives 0" path by construction. `framePts` is `view.sequence - firstSequence`, where `firstSequence` is latched from the first frame of each encode-thread run, so PTS starts at 0 on every (re)start rather than inheriting the previous stream's timeline. A capture-sequence difference is used instead of an encoded-frame counter so that when the encoder falls behind and stale frames are dropped, PTS jumps with the dropped frames and keeps pointing at the moment the picture actually occurred; with `time_base = 1/framerate` the difference is directly the frame interval. `force_idr_` was a member initialized in the constructor and referenced nowhere else, and the audio encoder had `in_frame->pts = pts_;` immediately overwritten by the `av_rescale_q` call on the next line; both were removed.

Verification: the Qt MinGW Debug build recompiled exactly the four edited translation units (`VideoEncoder`, `H264Encoder`, `AudioEncoder`, `RtmpPushManager`) and linked `debug/ECloudAssistant.exe` with no new warnings. Runtime verification is outstanding and must not rely on the encoder start banner or on puller-side warnings. A temporary `[PTS-CHECK]` log in `H264Encoder::Encode` prints when input PTS is nonconsecutive or the output packet's PTS/DTS differs from input. Its static `lastPts = -1` means a normal first frame at PTS 0 is silent on a fresh process, while a restarted stream's first frame usually prints; packet duration is displayed but not checked. The remaining criteria are that stop and re-stream work normally and that the phase 2 end-to-end latency baseline (nine samples, 44-67 ms, about 59.33 ms mean) is not degraded. The temporary log is to be deleted after acceptance.

Commit ID: not created in this change.

Remaining limitation: the corrected PTS still does not reach RTMP. `AVPacket::pts/dts` are not forwarded and `RtmpPublisher` continues to timestamp both audio and video from its own single `Timestamp` instance, so no end-to-end latency change is expected from this change alone; and nothing under `Puller/` reads or validates timestamps, so the phase plan's "non-monotonic timestamp warning at the player" criterion is not observable with this project's own player and should be replaced by the `[PTS-CHECK]` log. The skipped-frame branch of the new PTS is also still unexercised, because every run so far has reported `skip = 0`. The encoder now consumes an explicit PTS but all PTS-dependent encoder features remain disabled by `ultrafast`/`zerolatency`/`max_b_frames = 0`, so the change is currently a semantics fix rather than a latency or quality improvement.

Rollback point: restore the `quint64 pts = 0` default-argument form, the `pts_` member and the `pts >= 0 ? pts : pts_++` assignment in `VideoEncoder.{h,cpp}`, drop the `pts` argument from `H264Encoder::{h,cpp}` and from the `EncodeVideo` call site, and re-add `in_frame->pts = pts_;` in `AudioEncoder.cpp` if the original line is wanted for reference.

## RTMP startup log cleanup

Goal: reduce repetitive startup and handshake output while retaining the events needed to distinguish a publish timeout from a successful stream.

Affected files: `ECloudAssistant/Net/SigConnection.cpp`, `ECloudAssistant/UI/center/RemoteManager.cpp`, `ECloudAssistant/Pusher/RtmpPushManager.cpp`, `ECloudAssistant/Pusher/rtmp/RtmpConnection.cpp`, and `ECloudAssistant/Pusher/capture/GDISreenScapture.cpp`.

Behavior: removed dated `TRACE-PLAY` wrappers, duplicate callback-result and stream-address prints, intermediate RTMP handshake debug prints, five successful initialization-stage prints, and the redundant active-capture-size print. Kept one CREATESTREAM request, the primary capture geometry and decoded pixel format, RTMP publish-ready and stop events, pipeline statistics, and temporary PTS verification. Failure logs now identify the failed initialization stage; OpenUrl failure, pre-publish disconnect and five-second publish timeout retain the target URL; publish rejection includes the server status code. No protocol, startup ordering, timeout, or media behavior changed.

Verification: Qt MinGW Debug rebuilt the five affected translation units and linked `debug/ECloudAssistant.exe` successfully. Only existing signedness, unused-parameter, initializer and member-order warnings appeared. `git diff --check` passed for the edited source files. Runtime publish/retry has not yet been rerun after this log-only change, and the reason for the observed first-attempt timeout remains undiagnosed.

Commit ID: not created. Rollback point: restore only this entry's log statements in the five named files; retain the earlier PTS and pipeline edits.

## Publish capture/encode/PTS update

Goal: upload the current capture/encode and PTS changes to `https://github.com/zhaoha11/assient.git` with the Chinese commit message `优化采集编码pts`.

Affected files: the ECloudAssistant capture, codec, RTMP startup and signaling source files listed in the preceding phase entries, plus this worklog and `context/采集和编码的低延迟优化.md`. The unrelated working-tree formatting change in `ENET/RtmpServer/RtmpConnection.cpp` and untracked personal/tool files are excluded.

Behavior: the code retains the validated phase-2 triple-buffer/30 FPS/12 Mbps Baseline configuration and the phase-3 sequence-derived encoder PTS. This entry also corrects the documentation for the temporary PTS log's first-frame condition. No additional media behavior is changed for the upload.

Verification: the Qt MinGW Debug build is up to date (`mingw32-make -q` returned 0), and `git diff --check` passed. The destination `main` initially pointed to `86c673ae19a48aae509d4be2127c8e441e91f52e` and has no shared history with local `main`; replacing it requires a lease-guarded update. Post-PTS latency remeasurement, repeated stream reconnect, and removal of temporary `[PTS-CHECK]` remain outstanding.

Commit ID: this entry's containing commit (`git log -1 --oneline`); report its resolved hash after committing. Rollback point: the previous remote tip is `86c673ae19a48aae509d4be2127c8e441e91f52e`.

## Disable completed-stage test traces, retain pipeline statistics

Goal: stop printing temporary PTS, puller and SigServer playback traces while retaining `[PIPE-STATS]` for later queue/backpressure comparisons.

Affected files: `ECloudAssistant/Codec/H264Encoder.cpp`, `ECloudAssistant/Codec/AVDEMuxer.cpp`, `ECloudAssistant/Codec/H264_Decoder.cpp`, `ECloudAssistant/Puller/UI/AVPlayer.cpp`, and `ENET/SigServer/SigConnection.cpp`.

Behavior: the temporary `[PTS-CHECK]`, `[TRACE-PULL-20260814]` informational output and `[TRACE-PLAY-20260814]` server output are commented out. Puller failure warnings remain active without the dated trace prefix. `AVPlayer` retains a silent `SetStreamCallBack([](bool){})`, because the current `AVDEMuxer::FetchStream()` calls `FetchStreamInfo()` only when a callback exists; removing the callback together with its log would silently prevent demux initialization. `[PIPE-STATS]`, RTMP publish status and media/protocol behavior are unchanged. The PTS check can be re-enabled for future restart/drop-frame tests.

Verification: forced recompilation of the four affected client translation units and a client Debug link succeeded; only existing unused-parameter and signedness warnings appeared. `git diff --check` passed. The Linux SigServer change has not been rebuilt or run in VMware; client push/pull runtime was not rerun for this log-only change.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitation: the historical phase-3 worklog still lists post-PTS latency, restart and skipped-frame verification as outstanding; disabling the temporary log is not evidence that those checks passed. Rollback point: uncomment the marked temporary output and restore the dated puller warning labels if needed.

## Audio capture safety and lifecycle (问题优化二 phase 1)

Goal: remove the crash, undefined-data and stale-residual risks in the WASAPI audio capture path before touching anything else. No change to encode settings, RTMP, capture geometry, framerate or PTS.

Affected files: `ECloudAssistant/Pusher/capture/WASAPICapture.h`, `ECloudAssistant/Pusher/capture/WASAPICapture.cpp`, `ECloudAssistant/Pusher/capture/AudioCapture.cpp`.

### Silent-packet data path

`capture()` zeroed `m_pcmBuf` when `AUDCLNT_BUFFERFLAGS_SILENT` was set but still handed `pData` to the callback, and the memset covered the whole 4096-byte allocation rather than the packet's byte count. `pData` has no defined contents under the silent flag, so the callback was reading undefined memory while the buffer that had just been zeroed was never used. Both branches now hand the callback `m_pcmBuf` with `packetBytes = numFramesAvailable * nBlockAlign`; `ReleaseBuffer` is paired on every branch including the new error branch that rejects a null `pData` without the silent flag; `m_pcmBufSize` moved from `uint32_t` to `size_t` so the capacity comparison cannot overflow.

### init/exit rollback

`GetAddressOf()` does not release an existing pointer, so a second `init()` after a partial failure overwrote and leaked whatever had already been acquired, and every failure return left the client half-initialized. `exit()` called `CoUninitialize()` unconditionally and never released the interfaces or the `CoTaskMemAlloc`-allocated `m_mixFormat`. `init()` now calls a new `releaseResources()` before starting and on every failure return, uses `ReleaseAndGetAddressOf()`, and tracks `CoInitialize` in `m_comInitialized` so `CoUninitialize` is paired exactly once (`S_OK` and `S_FALSE` both require a pairing call). `exit()` is idempotent and now stops the thread first, then releases everything, so a later `init()` restarts from a clean state.

### start/stop lifecycle

`m_isEnabeld` is now `std::atomic<bool>` because the capture thread both reads it and writes it on self-exit. `stop()` is idempotent, joins only joinable threads, and no longer dereferences a null `m_threadPtr`. `start()` joins a thread that already exited before creating a new one, catches thread-creation failure and rolls back `Start()`. The defect this closes: when `capture()` returned -1 the thread broke out of its loop but left the flag set, so a later `start()` returned success without ever starting a thread, and audio stayed silently dead for the rest of the process.

### Stale audio across restart

`stopInternal()` now calls `IAudioClient::Reset()` after `Stop()` (guarded by a new `m_started` flag) to flush the capture buffer. `Reset()` is documented to flush pending data and to require a stopped stream; the thread is already joined at that point, so the call is legal. Measured before the fix: restarting the same client re-delivered exactly 2 packets (about 20 ms) of pre-stop audio at the head of the new window, deterministically in 5 of 5 runs, even after 1.5 s of guaranteed silence from the source.

### AudioCapture lifecycle

`Close()` is idempotent and now always calls `capture_->exit()`; the original never did, so `CoUninitialize` was never reached and each stream leaked a COM apartment reference on the signaling scheduler thread. `~AudioCapture()` calls `Close()`, `Init()` rolls back with `capture_->exit()` when `StartCapture()` fails, `GetSamples()` is null-safe, and the capture callback drops zero-length writes and checks `audio_buffer_`.

### Verification

Build: Qt MinGW 13.1.0 Debug recompiled `WASAPICapture.o` and `AudioCapture.o` and relinked `debug/ECloudAssistant.exe`; `mingw32-make -f Makefile.Debug -q` returns 0. The only new-compile warning is the pre-existing sign-compare in `AudioCapture::Read`.

Real-device probe: a temporary console probe that links `WASAPICapture.cpp` directly and drives this machine's default render endpoint was kept at `build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug/probe/wasapi_probe.cpp` (gitignored, disposable). Compile command:

```bash
g++ -std=gnu++17 -Wall -Wextra -o wasapi_probe.exe wasapi_probe.cpp \
  ../../../Pusher/capture/WASAPICapture.cpp -I../../../Pusher/capture \
  -ID:/Qt/6.10.1/mingw_64/include -ID:/Qt/6.10.1/mingw_64/include/QtCore \
  -ID:/Qt/6.10.1/mingw_64/mkspecs/win32-g++ -LD:/Qt/6.10.1/mingw_64/lib \
  -lQt6Core -lole32 -lksuser -lwinmm -lmingw32 -mthreads
```

Probe design note: loopback capture delivers no packets at all while no render stream is active, so the first probe version reported 0 packets for every restart and looked like a restart bug. Every capture window now holds a render stream open with a looped in-memory WAV (a -66 dBFS tone, or pure silence), which makes silent and non-silent windows deterministic and audible-free. Device format observed: `tag=65534` (EXTENSIBLE), 2 channels, 48000 Hz, 16-bit, blockAlign 4, so `adjustFormatTo16Bits` took the EXTENSIBLE branch and shared-mode `Initialize` accepted the rewritten format.

Results on the final code: non-silent window 196-198 packets, all of them non-zero, `null=0`, `badBytes=0`, `maxFrames=480` (480 frames x 4 bytes = 1920 bytes per packet, and 198 x 1920 = 380160 bytes, matching the byte total); silent window after a restart 200-201 packets, every one of them all-zero, zero non-zero packets in 5 of 5 runs; the same window before the `Reset()` flush contained exactly 2 non-zero packets at the very start in 5 of 5 runs; re-init after `exit()` twice plus `stop()` after `exit()` still captured real audio; 30 consecutive init/start/stop/exit cycles all succeeded.

Comparison against the pre-change code: the probe built against `git show HEAD:...WASAPICapture.{h,cpp}` segfaulted (exit code 139) on the destructor path where a capturing object is destroyed without `stop()`/`exit()`, because the shared_ptr destroyed a joinable thread. The fixed version completes that step and the 30 cycles. The silent-branch defect did not reproduce as wrong output in this environment: the driver happened to return zero-filled `pData` for silent packets, so the fix removes reliance on undocumented data rather than changing observed audio, and the `[run2]` zero-packet counts are the same in both versions.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). The unrelated pre-existing working-tree modification in `ENET/RtmpServer/RtmpConnection.cpp` was not touched.

Remaining limitations: no end-to-end push/pull or listening verification was performed. Driving one needs the GUI login against `192.168.3.130:8523` and a controller client to send CREATESTREAM, and publishing a test stream to the shared server at `192.168.3.130:1935` was deliberately skipped, so the user will run the real push/pull check. The recovery path for `start()` after the capture thread self-exited is reasoned from the code, not reproduced: forcing `capture()` to fail needs a real endpoint change such as unplugging or disabling the device. COM apartments were left as they are: `init()` runs on a signaling scheduler thread and the capture thread calls the WASAPI interfaces without its own `CoInitializeEx`, so the STA-created interfaces are used unmarshaled. Start and stop both execute on that same scheduler thread, which is why the `CoInitialize`/`CoUninitialize` pairing is sound, and switching the capture thread to the MTA would mean moving device ownership onto that thread, which is a separate change. `adjustFormatTo16Bits` still has its -1 return ignored and is unchanged. `Reset()` discards up to one period (10 ms) of audio captured before `Stop()`, which is intended on the stop/close paths where the encoder thread has already been joined.

Rollback point: revert the three files under `ECloudAssistant/Pusher/capture/` to the state at `d238b8d`.

## H.264 multi-NAL FLV packaging (问题优化二 phase 2)

Date: 2026-09-26. Goal: replace the fixed-four-byte removal and single whole-frame NAL length with per-NAL AVCC framing, without changing capture, encoder settings, audio, PTS or queue policy.

Affected files: `ECloudAssistant/Pusher/RtmpPushManager.cpp`, `ECloudAssistant/Pusher/rtmp/RtmpPublisher.{h,cpp}`, new `ECloudAssistant/Pusher/rtmp/FlvAvcPacket.{h,cpp}`, `ECloudAssistant/Pusher/Pusher.pri`, new `ECloudAssistant/tests/FlvAvcPacketTest.cpp`, and `context/问题优化二.md`.

Behavior: `PushVideo()` passes the complete Annex-B access unit synchronously, removing the old temporary copy and fixed 4-byte skip. `PushVideoFrame()` accepts `const uint8_t*` (read-only input; its only project caller is updated), converts the entire input before any send/state mutation, then copies the final FLV payload into shared owned storage for asynchronous RTMP sending. The converter accepts mixed 3/4-byte start codes, strips Annex-B leading/trailing zero bytes, preserves emulation-prevention bytes, and writes a separate 4-byte big-endian size for every non-empty NAL. A type-5 IDR anywhere in the access unit determines the `0x17/0x27` frame tag and initial keyframe gate; SPS alone is no longer a keyframe. AVC/AAC sequence headers still precede the first IDR, and `lengthSizeMinusOne` stays 3 (four-byte lengths). Null/empty input, missing start codes, empty NALs, or uint32 payload-size overflow return -1 without emitting partial output or changing the first-keyframe state. Encoder output/extradata and existing SPS/PPS extraction are unchanged.

Verification: the deterministic test's `--legacy` path reproduces the old packaging algorithm and exits 1 with `FAIL: legacy multi-NAL conversion`; the fixed path exits 0. Golden byte comparisons and independent length walking cover single 3/4-byte-start-code NALs, mixed SPS/PPS/SEI/IDR, SEI before IDR, multiple IDR slices, P frames, SPS without IDR, escaped `00 00 03 01`, leading/trailing Annex-B zeros, a 65537-byte NAL, and null/missing/empty/truncated delimiter inputs. Test compilation with `-Wall -Wextra` has no warnings. Reproduction from repository root:

```powershell
& 'D:/Qt/Tools/mingw1310_64/bin/g++.exe' -std=c++17 -Wall -Wextra -I ECloudAssistant/Pusher/rtmp ECloudAssistant/tests/FlvAvcPacketTest.cpp ECloudAssistant/Pusher/rtmp/FlvAvcPacket.cpp -o ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug/probe/flv_avc_test.exe
& 'ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug/probe/flv_avc_test.exe' --legacy # expected exit 1
& 'ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug/probe/flv_avc_test.exe' # expected exit 0
```

Build: regenerated Makefiles with Qt 6.10.1 qmake (`../../ECloudAssistant.pro -spec win32-g++ CONFIG+=debug`) in the existing Debug build directory; MinGW 13.1.0 `mingw32-make -f Makefile.Debug -j4` compiled the new converter and changed/dependent sources and linked `debug/ECloudAssistant.exe`, exit 0. Existing RTMP constructor-order/unused-parameter and manager signedness warnings remain. `git diff --check` passed.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`); all unrelated pre-existing working-tree changes were preserved. Rollback point: revert only this phase's manager/publisher/build-list hunks, remove the three new converter/test files, and restore the phase status; do not revert the audio or trace work recorded above.

Remaining limitations at implementation time: no actual RTMP push/pull, listening, reconnection, repaired-wire capture or target hardware decode was performed by the agent. Local byte correctness and a successful link are not evidence of those runtime results. A subsequently supplied user screenshot is recorded below. Complete `17 01`/`27 01` message length walking, initial playback/reconnect and target decode remain outstanding before phase 3. No post-fix latency measurement is available. Overflow is guarded by code but not tested with multi-gigabyte allocations. Existing SPS/PPS extraction through `H264Paraser::findNal()` was not changed or independently runtime-validated.

### Phase 2 acceptance evidence update (2026-09-26)

Goal: record the user's post-change packet-byte screenshot and summarize phase 2 acceptance without treating partial packet evidence as a complete runtime test. Affected files: `context/问题优化二.md`, `context/WORKLOG.md`, and the unchanged screenshot copied to `context/evidence/phase2-rtmp-20260926-221835.png`. No media or protocol code changed in this update.

Evidence: user supplied `C:/Users/95892/Pictures/Screenshots/屏幕截图 2026-09-26 221835.png`. After the visible five-byte `17 01 00 00 00` FLV AVC header, the screenshot shows SPS length `00 00 00 18` (24), SPS bytes `67 42 c0 28 da 01 e0 08 9f 96 10 00 00 03 00 10 00 00 03 03 c8 f1 83 2a`, PPS length `00 00 00 04` (4), PPS bytes `68 ce 3c 80`, and SEI length `00 00 02 b8` (696) followed by NAL header `06`. The complete visible SPS and PPS terminate exactly at the following length prefixes. Emulation-prevention `00 00 03` sequences inside SPS are retained. This confirms separate NAL lengths in the visible packet portion; SEI's declared length is visible but its entire content/end, following IDR and packet end are outside the screenshot.

Acceptance summary: code modification complete; deterministic byte tests, Debug build/link and the user's screenshot partial-boundary check passed. No raw capture or complete reassembled message was analyzed in this update. Full IDR/P-frame length walking, sequence-header verification, actual playback/reconnection, and hardware-decoder compatibility remain unverified. The `0x17` tag alone is not proof of the unseen IDR. See the phase-2 acceptance table in `context/问题优化二.md`.

Verification for this documentation update: checked the copied evidence file exists, cross-checked the recorded SPS byte count and PPS/SEI boundaries, and ran `git diff --check`. No rebuild was needed for documentation-only changes. Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Rollback: remove this evidence-update subsection, the corresponding phase-2 summary/status edits, and the copied screenshot; retain the implemented phase-2 code and earlier worklog entries.

## Cross-screen DPI render viewport (问题优化二 phase 2.5)

Date: 2026-09-27. Goal: keep the pulled video centered, aspect-correct and mouse-mapped when the playback window is moved between monitors that use different scale factors.

Affected files: `ECloudAssistant/Puller/Render/OpenGLRender.cpp`, `ECloudAssistant/Puller/UI/PullerWgt.{h,cpp}`.

Behavior: `OpenGLRender::resizeGL()` computes `m_pos`, `m_zoomSize` and `m_rect` from Qt logical pixels, but `paintGL()` passed `m_pos` and `m_zoomSize` straight to `glViewport()`, whose arguments are device pixels of the widget's framebuffer. On a monitor whose `devicePixelRatioF()` differs from the one the geometry was computed for, the viewport was therefore too small and offset, which is exactly the reported "video shrinks and shifts after dragging to the second screen". `paintGL()` now reads `devicePixelRatioF()` on every paint and multiplies `m_pos` and `m_zoomSize` by the current DPR before `glViewport()`, rounding to integer device pixels. The DPR is read per paint rather than cached, so a move to another screen takes effect on the next frame without depending on a fresh resize event. `m_rect` keeps Qt logical coordinates and is not scaled, so `GetPosRation()` (mouse position, remote-control ratios, hit area) is unchanged. Aspect-ratio scaling, texture upload, shaders, decoding and frame dimensions are untouched.

`PullerWgt::resizeEvent()` and its header declaration are removed. `PullerWgt` already hosts `player_` through the central widget's `QVBoxLayout`, so the manual `player_->resize(event->size())` was redundant with the layout and only duplicated sizing; the top-level `resize(800,500)` is retained. The now-unused `<QResizeEvent>` include was dropped.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` recompiled `OpenGLRender.o`, `PullerWgt.o`, `moc_PullerWgt.o` and relinked `debug/ECloudAssistant.exe` (30,978,387 bytes, 2026-09-27 22:11:59); a follow-up `make -f Makefile.Debug -q` returns 0. The only warning is the pre-existing unused `showEvent` parameter in `OpenGLRender.cpp`. `git diff --check` passed with only line-ending conversion warnings.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitation: the cross-monitor behavior is reasoned from the logical-vs-device mismatch that matches the reported symptom; it has not been observed on the user's two-monitor setup. The acceptance run still needs a bidirectional drag between monitors with different scale factors, plus window resize/maximize on each, confirming the video stays centered and aspect-correct and that clicks at the video corners and center keep the same remote ratios. Single-screen and same-scale setups must not regress.

Rollback point: restore the direct `glViewport(m_pos.x(),m_pos.y(),m_zoomSize.width(),m_zoomSize.height())` call, re-add `PullerWgt::resizeEvent()` and its declaration and the `<QResizeEvent>` include; no capture, encoder, RTMP, signaling or server change is involved.

## Local playback entry and page placeholder (本地播放 phase 1)

Date: 2026-09-28. Goal: add a "本地播放" navigation entry between "设备列表" and "高级设置" and a placeholder `LocalPlayerWgt` page, per `context/本地播放.md` phase 1. No media capability is implemented in this phase.

Affected files: `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}` (new), `ECloudAssistant/UI/center/MainWgt.{h,cpp}`, `ECloudAssistant/UI/list/ListInfoWgt.cpp`, `ECloudAssistant/UI/UI.pri`, `ECloudAssistant/res.qrc`, `ECloudAssistant/UI/brown/main.css`, plus two new icons `ECloudAssistant/UI/brown/list/local.png` and `local_press.png`.

Behavior: `ListInfoWgt` now creates four `CustomWgt` items in the order 远程控制 → 设备列表 → 本地播放 → 高级设置; `HandleItemSelect()` is unchanged and still emits `index + 1`. `MainWgt` inserts `localPlayerWgt_` after `deviceWgt_`, so the `QStackedWidget` order is 0 登录页, 1 远程控制, 2 设备列表, 3 本地播放, 4 高级设置; `slot_ItemCliked()` keeps the generic index-based switch. `LocalPlayerWgt` is a fixed 600x510 placeholder page (title + description, object names `LocalPlayerWgt`/`localPageTitle`/`localPageDescription` styled in `main.css` like the device page); it performs no file selection, decoding or playback. `CustomWgt` highlight behavior is reused via `customWgts_` with no per-item branch. The two 200x200 single-color icons follow the existing icon palette (#8A8A8A normal, #D4237A pressed).

Verification: qmake re-run (`qmake ..\..\ECloudAssistant.pro -spec win32-g++ "CONFIG+=debug"`, exit 0) then Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0: regenerated `qrc_res.cpp`, `moc_LocalPlayerWgt.o`, `LocalPlayerWgt.o`, `moc_MainWgt.o`, relinked `debug/ECloudAssistant.exe` (31,979,289 bytes, 2026-09-28 15:12:08). Only pre-existing warnings remain (deprecated `QMouseEvent::globalPos`, `ListInfoWgt` sign-compare). Manual four-item navigation switching and login/regression checks are still pending on the user's machine (验收标准 2/5/6 的手动部分).

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitation: the page is a visual placeholder only; phase 2 must add file selection and the actual media pipeline. The two generated icons are drawn programmatically and can be replaced by designed assets without code changes.

Rollback point: remove the `LocalPlayerWgt` entry from `UI.pri`, the `stackWgt_->addWidget(localPlayerWgt_)` call and member in `MainWgt`, the fourth `CustomWgt` in `ListInfoWgt.cpp`, the `LocalPlayerWgt` blocks in `main.css`, the two `local*.png` entries in `res.qrc`, and delete `UI/center/LocalPlayerWgt.{h,cpp}` and the two icons; no remote control, device list, settings, signaling, RTMP or server behavior is touched.

## Local playback initial player UI (本地播放 phase 2.1)

Date: 2026-09-28. Goal: replace the phase-1 placeholder with the initial local-player surface while keeping `LocalPlayerWgt` independent from the remote-monitor window and all media behavior.

Affected files: `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`, `ECloudAssistant/UI/brown/main.css`, and `context/本地播放.md`.

Behavior: `LocalPlayerWgt` now uses one clear hierarchy: title and short description, a large dark video surface with an unopened-file empty state, and one compact bottom control bar. The control bar contains open-file, play/pause, stop, progress, current/total time, volume and speed controls. In the initial empty state, open-file remains visually available while playback-dependent controls are disabled. Buttons, sliders and the speed selector have dark-theme focus/disabled styling. No file dialog, signal connection, demuxing, decoding, OpenGL rendering, audio output or playback-state handling was added. `PullerWgt`, `AVPlayer` and all remote-control paths are unchanged.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` recompiled `LocalPlayerWgt.o`, regenerated the stylesheet resource and linked `debug/ECloudAssistant.exe`, exit 0. A disposable Qt preview program rendered `LocalPlayerWgt` at its 600x510 logical size; the title, video surface, empty state and complete control bar fit without clipping or overlap. `git diff --check` passed. Manual verification inside the full client, including keyboard focus appearance at runtime, is still pending.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitations: all controls are UI-only; the open button intentionally has no file-dialog behavior, and the disabled controls do not affect media state. Recent items, playlists and detailed file metadata are outside phase 2.1. The local media pipeline and the exact reuse boundary for `AVDEMuxer`, decoders, `OpenGLRender` and `AudioRender` remain future work.

Rollback point: restore the former placeholder-only `LocalPlayerWgt.{h,cpp}` and its two original `main.css` selectors, then remove this phase-2.1 document/worklog update. No remote monitoring, signaling, RTMP or media implementation needs to be reverted.

## Full-size player layout (本地播放 phase 2.2)

Date: 2026-09-28. Goal: turn `LocalPlayerWgt` from an intro-style feature page into a full-size player surface per `context/本地播放.md` phase 2.2: remove the page title/description, let the video area start at the page top and fill all space above the progress row, hug the control bar to the page bottom, and drop residual page margins. Layout-only change; no media or file-selection logic.

Affected files: `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`, `ECloudAssistant/UI/brown/main.css`.

Behavior: the title and description labels are deleted together with their layout items, and their now-orphaned QSS rules (`localPageTitle`, `localPageDescription`) are removed from `main.css`. `LocalPlayerWgt` no longer calls `setFixedSize(600,510)`; it uses an Expanding/Expanding size policy so the fixed 600x510 `QStackedWidget` in `MainWgt` decides the page size. The root `QVBoxLayout` uses zero margins and zero spacing with `videoSurface` at stretch factor 1, so the video area occupies everything above the control bar and the bar (progress row + action row, still one unified bar per the user's decision) hugs the page bottom. Per the user's decision the full-bleed flat style replaces the card look: `localVideoSurface` and `localControlBar` keep their dark backgrounds but lose their 1px borders and 12px corner radii. Control bar internal padding, empty state, object names, disabled and focus styles from phase 2.1 are unchanged.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; rebuilt `LocalPlayerWgt.o`, `moc_LocalPlayerWgt.o`, regenerated `qrc_res.cpp` (embedded css changed) and relinked `debug/ECloudAssistant.exe` (32,128,364 bytes, 2026-09-28 17:49 local time as reported by Explorer). No new warnings. Manual checks still pending in the running client: video area starts at page top and resizes with the window, no title/description remains, control bar hugs the bottom, phase 2.1 controls and empty state intact, other pages unaffected.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitation: static layout only — no media pipeline; the two layout decisions (progress row inside the unified bar, flat full-bleed styling) were confirmed with the user.

Rollback point: restore the title/description widgets and `setFixedSize(600,510)` with the 28/24/28/22 root margins and 12px spacing in `LocalPlayerWgt.cpp`, and restore the card-style `localVideoSurface`/`localControlBar` QSS rules plus the `localPageTitle`/`localPageDescription` rules in `main.css`; no other component is affected.

## Frameless main-window resize (本地播放 phase 2.3)

Date: 2026-09-28. Goal: per `context/本地播放.md` phase 2.3, keep `Qt::FramelessWindowHint` but make the fixed-size main window resizable from all four edges and corners with correct directional cursors, and let the right-side content (including the local player video area) fill the extra space. Layout/window-shell change only; no media, signaling or RTMP behavior.

Affected files: `ECloudAssistant/ECloudAssistant.{h,cpp}`, `ECloudAssistant/UI/title/TitleWgt.cpp`, `ECloudAssistant/UI/list/ListInfoWgt.cpp`, `ECloudAssistant/UI/center/MainWgt.cpp`, `ECloudAssistant/UI/center/LoginWgt.cpp`, `ECloudAssistant/UI/center/RemoteWgt.cpp`, `ECloudAssistant/UI/center/DeviceListWgt.cpp`.

Behavior: the top-level window replaces `setFixedSize(800,540)` with `resize(800,540)` plus `setMinimumSize(800,540)` (minimum equals the previous fixed size so no page layout collapses). Resize hit-testing lives in an application-level `eventFilter` in `ECloudAssistant`: mouse tracking is enabled recursively on the window subtree so edge hover produces `MouseMove` events; a 6-logical-px border band maps the cursor position to `Qt::Edges` (diagonal cursors on corners), and a left-button press inside the band calls `QWindow::startSystemResize()` for native smooth resizing, consuming the event so the existing title-drag handlers cannot also fire. Button widgets (minimize/close and others) keep priority — hits over `QPushButton` children skip resize logic; while maximized or full screen, hit-testing is disabled; `Leave` events reset the cursor. Existing drag/minimize/close behavior is otherwise untouched. On the layout side, `TitleWgt` becomes `setFixedHeight(30)`, `ListInfoWgt` and its nav list become `setFixedWidth(200)`, `MainWgt` gains a zero-margin `QVBoxLayout` around `stackWgt_` and drops both fixed sizes, `settingWgt_` drops its fixed size, and `LoginWgt`/`RemoteWgt`/`DeviceListWgt` replace `setFixedSize(600,510)` with `QSizePolicy::Expanding` (their internal `QVBoxLayout`s already use stretch factors). The local player page keeps its phase-2.2 expanding video area, so it absorbs new space while the control bar hugs the bottom.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; rebuilt `ECloudAssistant.o`, `moc_ECloudAssistant.o`, `TitleWgt.o`, `ListInfoWgt.o`, `MainWgt.o`, `LoginWgt.o`, `RemoteWgt.o`, `DeviceListWgt.o` and relinked `debug/ECloudAssistant.exe` (32,241,840 bytes, 2026-09-28 19:52:16). `git diff --check` exit 0 (CRLF conversion notices only). Only pre-existing warnings remain (deprecated `globalPos` in the untouched drag handlers, `ListInfoWgt` sign-compare). Manual verification pending in the running client: smooth resize from all edges/corners with correct cursors, minimum-size clamp at 800x540, nav width unchanged, pages filling in real time, title drag and page switching unaffected, remote-monitor window unaffected.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitation: pure Qt hit-testing (no native `WM_NCHITTEST`), so resize is Qt-driven; hit band is 6px and is skipped over buttons, so the exact corner pixels over the close button do not resize. Minimum size equals the old fixed size, so the window cannot shrink below 800x540.

Rollback point: restore `setFixedSize(800,540)` and remove the event-filter/edge-hit helpers in `ECloudAssistant.{h,cpp}`; restore `setFixedSize` on `TitleWgt`, `ListInfoWgt`, `listWgt_`, `MainWgt`, `stackWgt_`, `settingWgt_`, `LoginWgt`, `RemoteWgt`, `DeviceListWgt`; no media or remote-monitor code is involved.

## Empty-state title color tweak (本地播放 UI)

Date: 2026-09-28. Goal: user request — the "尚未打开文件" empty-state title should be pure white instead of the near-white cool tint, and the `LocalPlayerWgt` page background should be white instead of the purple-teal gradient. Affected files: `ECloudAssistant/UI/brown/main.css` (`QLabel#localVideoEmptyTitle`, `QWidget#LocalPlayerWgt`). Behavior: empty-state title color `#f5f8f8` → `#ffffff`; page background gradient (`#3f2c49→#193f46`) → flat `#ffffff`. The dark video surface (`#101517`) and translucent control bar are unchanged, so the white background shows through the translucent bar only; all other local-player styles unchanged. Verification: `rcc` regenerated `qrc_res.cpp`, Debug build exit 0, exe relinked (2026-09-28 20:05 local). Rollback point: restore `#f5f8f8` on `localVideoEmptyTitle` and the original gradient on `LocalPlayerWgt`.

## Local player background matches remote page

Date: 2026-09-28. Goal: make the empty `LocalPlayerWgt` page use the same red-brown-to-teal background shown by `RemoteWgt` instead of the black video surface and gray control area.

Affected file: `ECloudAssistant/UI/brown/main.css`.

Behavior: `LocalPlayerWgt` now shares the exact qlineargradient selector used by `RemoteWgt`. The empty `localVideoSurface` and `localControlBar` backgrounds are transparent, allowing that one page gradient to remain continuous behind the empty-state text, progress row and controls. Control styling and all playback behavior are unchanged.

Verification: regenerated the Qt resource object, rendered the page with the disposable preview program, and confirmed the gradient spans both the video and control regions. Qt 6.10.1 MinGW 13.1.0 Debug linked `debug/ECloudAssistant.exe` successfully, exit 0. Manual confirmation in the full client remains pending.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Rollback point: remove `QWidget#LocalPlayerWgt` from the shared `Loginer`/`RemoteWgt` gradient selector, restore its former white rule, and restore the black `localVideoSurface` plus dark translucent `localControlBar` backgrounds.

## Local playback state bridge (本地播放 phase 3.1)

Date: 2026-09-29. Goal: establish `LocalPlayerWgt → LocalPlayer` as the local-playback control boundary while keeping `AVPlayer` exclusively responsible for remote playback and remote input.

Affected files: new `ECloudAssistant/Player/LocalPlayer.{h,cpp}` and `Player.pri`; `ECloudAssistant/ECloudAssistant.pro`; `ECloudAssistant/Puller/UI/AVPlayer.{h,cpp}`; `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`; `context/本地播放.md`; and `context/WORKLOG.md`.

Behavior: `LocalPlayer` is a non-visual `QObject` with six states (`Idle`, `Opening`, `Playing`, `Paused`, `Stopped`, `Error`), one state-change signal, and play/pause/stop commands. `LocalPlayerWgt` owns this controller and derives its existing control enabled/text state from the controller state. The temporary local constructor, local state and local commands have been removed from `AVPlayer`; its remote constructor, signaling, RTMP startup, input handling and stop path remain its only responsibilities. `PlayerCore` is documented as the future shared media layer but is not created as an empty abstraction in this phase.

Verification: regenerated the qmake project so `Player.pri`, `LocalPlayer.cpp` and `moc_LocalPlayer.cpp` entered the build; Qt 6.10.1 MinGW 13.1.0 Debug then compiled and linked `debug/ECloudAssistant.exe`, exit 0. A disposable `QCoreApplication` state probe passed (exit 0): play from `Idle` produced no transition, followed by `Stopped → Playing → Paused → Stopped`. `git diff --check` is recorded after the final edits. No remote RTMP runtime regression test was performed.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Remaining limitations: `LocalPlayer` is currently a state-only controller. The open-file button, local source, `PlayerCore`, demuxing, decoding, OpenGL rendering, audio output, seeking, volume and speed behavior are not implemented. `Opening` and `Error` are reserved for future asynchronous backend results; starting a demux thread alone will not count as successful playback.

Rollback point: remove the `Player` module include and files, remove the `LocalPlayer` member/connections from `LocalPlayerWgt`, and revert the phase-3.1 plan. `AVPlayer` needs no rollback because the final structure adds no local responsibility to it.

## Local playback backend decision: Qt Multimedia

Date: 2026-09-29. Goal: replace the planned local `PlayerCore + FFmpeg` playback implementation with a Qt Multimedia backend while preserving the completed `LocalPlayerWgt → LocalPlayer` boundary and the existing remote `AVPlayer` chain.

Affected files: `context/本地播放.md` and `context/WORKLOG.md`. No source or build file changed in this decision update.

Decision: local files will use `LocalPlayer → QMediaPlayer + QAudioOutput + QVideoWidget`. `LocalPlayer` remains the only business-facing wrapper for state, commands, progress and errors; `LocalPlayerWgt` must not scatter direct Qt Multimedia calls. The former plans for a shared `PlayerCore`, local `AVDEMuxer` adaptation, custom local decoders, A/V synchronization and playback clock are cancelled. Remote monitoring keeps its existing `AVPlayer`, signaling, RTMP, decoder and custom render/audio path unchanged.

Roadmap: phase 3.3 adds Multimedia Widgets dependencies and establishes `LocalPlayer → QMediaPlayer`; phase 3.4 opens files and displays video; phase 3.5 implements play/pause/stop plus position/duration; phase 3.6 implements seek, volume, playback rate, errors and lifecycle acceptance.

Verification: documentation was reviewed for the new architecture and `git diff --check` was run. No build was required because this update changes plans only. Commit ID: not created in this change.

## Qt Multimedia backend bootstrap (本地播放 phase 3.3)

Date: 2026-09-29. Goal: add the Qt Multimedia Widgets dependency and establish `LocalPlayer → QMediaPlayer + QAudioOutput` without opening files or changing the remote player.

Affected files: `ECloudAssistant/ECloudAssistant.pro`, `ECloudAssistant/Player/LocalPlayer.{h,cpp}`, `context/本地播放.md`, and `context/WORKLOG.md`.

Behavior: the qmake project now links `multimediawidgets`. `LocalPlayer` creates `QMediaPlayer` and `QAudioOutput` as QObject children and connects the audio output to the media player. `Play()`, `Pause()` and `Stop()` forward directly to Qt Multimedia. `QMediaPlayer::playbackStateChanged` maps real `PlayingState`, `PausedState` and non-idle `StoppedState` values back to the existing business state signal, replacing the former command-driven simulated transitions. No source, video output, progress, seek, volume, playback rate or error mapping is implemented in this phase. `AVPlayer`, RTMP, signaling and remote input code are untouched.

Verification: reran qmake, rebuilt `LocalPlayer`, its moc object and dependent UI objects, and linked `debug/ECloudAssistant.exe` with Qt 6.10.1 MinGW 13.1.0, exit 0. A disposable offscreen probe successfully created `QMediaPlayer`, `QAudioOutput` and `QVideoWidget`, verified that the player's audio output points to the owned `QAudioOutput`, and verified the initial business state is `Idle`; exit 0. Final `make -q` and `git diff --check` are recorded after documentation edits.

Commit ID: not created in this change. Remaining limitations: the local player has no media source and no `QVideoWidget` attachment yet, so it cannot open or play a file. Media status, duration, position and errors are intentionally deferred to phases 3.4～3.6.

Rollback point: remove `multimediawidgets` from the qmake modules, remove the `QMediaPlayer`/`QAudioOutput` members and signal connection, and restore command-driven state transitions in `LocalPlayer`. No remote-player rollback is required.

## Open file and video output (本地播放 phase 3.4)

Date: 2026-09-29. Goal: connect `QFileDialog → LocalPlayer::Open(path) → QMediaPlayer::setSource()` and display real frames by placing `QVideoWidget` into the existing video area through `setVideoOutput()`, per `context/本地播放.md` phase 3.4.

Affected files: `ECloudAssistant/Player/LocalPlayer.{h,cpp}` and `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`. No `.pro`/`.pri` change (`multimediawidgets` was already linked in 3.3); remote `AVPlayer`, RTMP, signaling and remote input untouched.

Behavior: `LocalPlayer` now owns a `QVideoWidget`, wires it with `mediaPlayer_->setVideoOutput()`, and exposes it via `videoWidget()` so the UI can place it without touching `QMediaPlayer`. `Open(path)` validates the path first — a missing/non-file path enters `Error` and emits `sig_errorOccurred("文件不存在：…")`; otherwise it enters `Opening` and calls `setSource(QUrl::fromLocalFile(...))`. The state machine is now backend-driven: `mediaStatusChanged == LoadedMedia` promotes `Opening → Stopped`; `errorOccurred` moves to `Error` and maps `ResourceError/FormatError/NetworkError/AccessDeniedError` to Chinese messages via the new `sig_errorOccurred` signal. The `StoppedState` branch was narrowed to converge only from `Playing`/`Paused`, so a `StoppedState` callback can no longer clobber `Opening` or `Error`. On the UI side the video area is now a `QStackedWidget` with two pages: the empty-state page (default, also used to show the Chinese error text on failure) and the `QVideoWidget` page (shown once `Opening`/`Stopped`/`Playing`/`Paused`), toggled by `updateVideoArea()` from `applyLocalPlaybackState()`. The "打开文件" button is now a member connected to `openLocalFile()`, which runs `QFileDialog::getOpenFileName` with a common audio/video extension filter and returns early on cancel.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; rebuilt `LocalPlayer.o`, `LocalPlayerWgt.o`, `moc_LocalPlayer.o`, `moc_LocalPlayerWgt.o` and relinked `debug/ECloudAssistant.exe` (33,592,238 bytes, 2026-09-29 19:43:26). No new warnings from the changed files. `git diff --check` exit 0 (CRLF notices only). Manual verification pending in the running client: open a real file, confirm frames render in the video area, empty state hides on load, and a bad/nonexistent path shows the Chinese error in the empty state.

Commit ID: not created in this change. Remaining limitations: playback controls still only issue commands — `position`/`duration`/time text/progress bar are not synced (phase 3.5); seek, volume, playback rate, full error UX and reopen lifecycle are not implemented (phase 3.6). The video widget has no parent until the UI adds it to the stack, so it relies on `LocalPlayerWgt` to take ownership.

Rollback point: restore the single empty-state video layout in `LocalPlayerWgt` and make "打开文件" a local; remove `videoWidget()`, `Open()`, `sig_errorOccurred`, the `QVideoWidget` member and the `mediaStatusChanged`/`errorOccurred` connections from `LocalPlayer`, and restore the former non-idle `StoppedState` guard. No remote-player change is involved.

## Empty video area inherits top-level dark background (本地播放 phase 3.4 follow-up)

Date: 2026-09-29. Goal: after phase 3.4 the empty local-player video area rendered black instead of the shared red→teal page gradient; restore the previous background while real frames are absent.

Root cause: `ECloudAssistant` sets a selector-less `setStyleSheet("background-color: #121212")` (ECloudAssistant.cpp line 59). A selector-less rule propagates to descendant widgets, so any new descendant without its own background rule paints `#121212`. Before 3.4 every widget in the video area had an explicit rule (`QFrame#localVideoSurface{background:transparent}` plus the two labels), so the page gradient showed through; phase 3.4 introduced two new containers — `QStackedWidget#localVideoStack` and the empty-state `QWidget` — that had no rule and therefore inherited the top-level black.

Affected files: `ECloudAssistant/UI/center/LocalPlayerWgt.cpp` and `ECloudAssistant/UI/brown/main.css`.

Behavior: the empty-state page now carries `objectName` `localVideoEmptyPage`, and `main.css` gains `QStackedWidget#localVideoStack,QWidget#localVideoEmptyPage{background:transparent;}` right after the existing `localVideoSurface` rule. The empty state therefore shows the same `Loginer`/`RemoteWgt`/`LocalPlayerWgt` red→teal gradient as before phase 3.4. When a real file is open the `QVideoWidget` page covers the surface, so this rule affects only the empty and error states; letterbox bars around a playing video remain the `QVideoWidget`'s own black, which is expected. No playback or state-machine behavior changed.

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0, `qrc_res.cpp` regenerated for the css edit; `LocalPlayerWgt.o` rebuilt. Manual confirmation of the gradient in the running client pending.

Commit ID: not created in this change. Rollback point: remove the `localVideoEmptyPage` objectName and the added `QStackedWidget#localVideoStack,QWidget#localVideoEmptyPage` rule.

## Basic playback and progress (本地播放 phase 3.5)

Date: 2026-09-29. Goal: make play/pause/stop driven by the real player state and synchronise `position`, `duration`, the time text and the progress bar, per `context/本地播放.md` phase 3.5.

Affected files: `ECloudAssistant/Player/LocalPlayer.{h,cpp}` and `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`. No `.pro`/`.pri` change; remote `AVPlayer`, RTMP, signaling and remote input untouched; no CSS change.

Behavior: `LocalPlayer` now forwards `QMediaPlayer::positionChanged`/`durationChanged` as its own `sig_positionChanged`/`sig_durationChanged`, and caches the values behind new `position()`/`duration()` getters so the UI still never reads `QMediaPlayer` directly. `LocalPlayer::Open(path)` resets the timeline first (`position_ = duration_ = 0`) and re-emits both signals, so a newly opened file cannot briefly show the previous file's position or duration. On the UI side `timeLabel` became the member `timeLabel_`; a new `updateTimeline()` converts `position/duration` into the progress bar's per-mille value (`position * 1000 / duration`, guarded by `duration > 0`) and renders the time text via a file-local `formatDuration()` helper (`mm:ss`, or `h:mm:ss` past an hour) as `position / duration`. `applyLocalPlaybackState()` now also calls `updateTimeline()`, so state and timeline refresh in the same pass; the play/pause button text and all enabled states remain derived from `player_->playbackState()` rather than any UI-side booleans.

Scope note: the progress bar is a pure indicator in phase 3.5 — `setAttribute(Qt::WA_TransparentForMouseEvents)` blocks dragging while its enabled styling still follows `hasMedia`. Dragging and `Seek()` remain phase 3.6 work.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; rebuilt `LocalPlayer.o`, `LocalPlayerWgt.o`, `moc_LocalPlayer.o`, `moc_LocalPlayerWgt.o` and relinked `debug/ECloudAssistant.exe` (33,651,415 bytes, 2026-09-29 20:08:00). No new compiler warnings. `git diff --check` exit 0 (CRLF notices only). Manual verification pending: play/pause/stop a real file and confirm the time text and progress bar advance and reset on stop or on opening another file.

Commit ID: not created in this change. Remaining limitations: no seek (progress bar not draggable), volume and playback rate are still inert, and full error/lifecycle handling is phase 3.6; `position`/`duration` are not reset when a mid-playback error occurs.

Rollback point: remove the two `positionChanged`/`durationChanged` connections, the `position()`/`duration()` getters and the `position_`/`duration_` members from `LocalPlayer`, drop the timeline reset in `Open()`, and remove `updateTimeline()`, `formatDuration()`, the `timeLabel_` member and its two connections from `LocalPlayerWgt`. No remote-player change is involved.

## Progress bar reset to zero on pause (本地播放 phase 3.5 fix)

Date: 2026-09-29. Goal: fix the progress bar snapping back to zero when playback is paused.

Symptom reported in the running client: the progress bar advanced normally while playing and reset to zero on "打开文件" (intended), but also reset to zero on pause. Manual check showed the time text kept its total duration (e.g. `00:12 / 03:45`), so `duration_` stayed valid and only the reported position was cleared.

Root cause: nothing in `LocalPlayerWgt` or `LocalPlayer` resets the timeline on pause — the only zero paths are the deliberate reset in `LocalPlayer::Open()` and the `duration > 0 ? … : 0` guard in `updateTimeline()`. The zero therefore came from the Qt media backend emitting a transient `positionChanged(0)` when the pipeline switches to paused, which `updateTimeline()` faithfully rendered.

Affected file: `ECloudAssistant/Player/LocalPlayer.cpp`. The fix lives in the media layer, not the UI, because deciding whether a reported position is meaningful is a media-layer judgement.

Behavior: the `positionChanged` handler now ignores a reported position of `0` unless the underlying player is actually in `QMediaPlayer::StoppedState`. The check reads `mediaPlayer_->playbackState()` rather than the cached business state so it holds for either signal ordering (before or after `playbackStateChanged(PausedState)`). Stop and opening a new file still reset the bar because those run in `StoppedState`; the explicit timeline reset in `Open()` is unaffected. A transient `positionChanged(0)` while playing is likewise ignored, which is harmless because the cached position is already zero at the start of playback; loop-to-start is not implemented yet, so no visible behaviour regresses. `updateTimeline()` is still called from `applyLocalPlaybackState()` — with the guard in place that render is harmless and still covers duration-only changes.

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; `LocalPlayer.o` rebuilt and `debug/ECloudAssistant.exe` relinked (33,652,131 bytes, 2026-09-29 20:34:16). Manual verification pending: pause should hold the bar at the current position, resume should continue from the same point rather than restart, and stop / open-another-file should still reset the bar and time text to zero.

Commit ID: not created in this change. Remaining limitations: the same transient-zero class of problem is not guarded for `durationChanged`; seek, volume and playback rate remain phase 3.6 work.

Rollback point: restore the unconditional `position_ = position;` assignment in the `positionChanged` handler.

## Show first frame on open (本地播放 phase 3.5 enhancement)

Date: 2026-09-29. Goal: when a file is opened, display its first frame immediately and stay paused, so playback only starts after the user presses "播放".

Affected files: `ECloudAssistant/Player/LocalPlayer.{h,cpp}`. No UI or CSS change.

Behavior: `LocalPlayer` now connects to `videoWidget_->videoSink()`'s `videoFrameChanged`. On `mediaStatusChanged == LoadedMedia` it sets `firstFramePending_` and calls `mediaPlayer_->play()`; as soon as the first decoded frame arrives the handler clears the flag and calls `mediaPlayer_->pause()`, leaving the player paused on that frame. The `playbackStateChanged(PlayingState)` branch is suppressed while `firstFramePending_` is set, so the transient start-up does not leak a "暂停" flash into the play/pause button. `Play()`, `Pause()`, `Stop()` and `Open()` all clear `firstFramePending_`, so an explicit user command always overrides the preview. The player ends in `Paused` rather than `Stopped`, which the UI already treats as "media available", so the play button is enabled and reads "播放".

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; `LocalPlayer.o` rebuilt and `debug/ECloudAssistant.exe` relinked (33,819,011 bytes, 2026-09-29 20:42:28). Manual verification pending: opening a file should paint its first frame with the play button showing "播放", pressing "播放" should start from the beginning, and the progress bar should stay at zero while the preview is held.

Commit ID: not created in this change. Remaining limitations: the preview start-up may produce a very short audio blip on some backends since `play()` runs with the audio output unmuted; letterbox black bars around a playing video are still controlled by the `QVideoWidget` aspect-ratio mode (unchanged in this step).

Rollback point: remove the `videoSink()`/`videoFrameChanged` connection, the `firstFramePending_` member and the `play()` call in the `LoadedMedia` handler, restore the unconditional `UpdatePlaybackState(PlaybackState::Playing)`, and drop the flag clearing in `Play`/`Pause`/`Stop`/`Open`.

## Crop-to-fill video output (本地播放 phase 3.5 enhancement 2)

Date: 2026-09-29. Goal: remove the black letterbox/pillarbox bars around the playing video so the frame fills the whole video area.

Background: the bars are not a defect. `QVideoWidget` defaults to `Qt::KeepAspectRatio`, which fits the source aspect ratio (e.g. 1920x1080) inside a differently shaped control (e.g. 900x600) and paints the leftover region black. The earlier claim that the bars "cannot be changed" was wrong — `QVideoWidget::setAspectRatioMode()` exposes the choice, so all four options (stretch, crop, keep-with-bars, custom painting) were available. The user chose crop-to-fill.

Affected file: `ECloudAssistant/Player/LocalPlayer.cpp`. One added call in the constructor, right after `setVideoOutput()`:

```cpp
//裁剪铺满：视频按比例放大到铺满整个控件，超出部分裁掉，避免上下或左右出现黑边
videoWidget_->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
```

Behavior: the frame is now scaled up until it covers the control in both dimensions and the overflow is clipped, so no black area remains and the aspect ratio is still correct (no stretching). The trade-off is deliberate: content outside the control is cut off, so a source whose edges carry information — an OSD timestamp, a burned-in caption, a screen-recording taskbar — will lose that edge. The setting lives in the media layer rather than the UI, consistent with `LocalPlayer` owning all `QVideoWidget`/`QMediaPlayer` configuration, and it is applied once in the constructor because `videoWidget_` is created there and `setVideoOutput()` is the only other place it is touched.

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0; `LocalPlayer.o` rebuilt and `debug/ECloudAssistant.exe` relinked (33,819,265 bytes, 2026-09-29 20:46:31). `git diff --check` clean (only pre-existing CRLF warnings). Manual verification pending: play a 16:9 file in the non-16:9 video area and confirm the black bars are gone, the image is not stretched, and the cropped edge content is acceptable.

Build note: this session's shell had `C:\msys64\ucrt64\bin` ahead of the Qt MinGW directory, so `g++` first resolved to the msys2 13.1.0 compiler and the build failed with no diagnostic. Prepending `D:\Qt\Tools\mingw1310_64\bin` to `PATH` fixed it — see the "Build environment: Qt MinGW must precede msys2 on PATH" entry above.

Commit ID: not created in this change. Remaining limitations: the fill mode is fixed at compile time with no UI switch; `Qt::IgnoreAspectRatio` (stretch, distorted) and a custom-painted fill mode with a runtime selector remain the alternatives if this trade-off proves wrong. Phase 3.6 (seek, volume, playback rate) is still unimplemented.

Rollback point: delete the `videoWidget_->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);` call (the default `Qt::KeepAspectRatio` is restored automatically) or change the argument to `Qt::IgnoreAspectRatio` to switch from crop to stretch. No UI, CSS, protocol or server change is involved.

## Seek, volume, playback rate, error fallback and media release (本地播放 phase 3.6)

Date: 2026-09-29. Goal: complete `context/本地播放.md` phase 3.6 — wire the progress slider to real seeking, the volume slider to the audio output, and the speed combo to the playback rate, and close the remaining error and lifecycle gaps (a load failure that never reports, releasing the media source on teardown, and opening a second file while the first is playing).

Affected files: `ECloudAssistant/Player/LocalPlayer.{h,cpp}`, `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`. No UI structure, CSS, protocol or server change — the existing widgets are only reconnected, and the layout is untouched.

Behavior:

- Four new `LocalPlayer` methods, each a thin validated forwarder so the UI still never touches `QMediaPlayer` or `QAudioOutput`: `Seek(qint64)`, `SetVolume(float)`, `SetPlaybackRate(qreal)`, and `Close()`.
- `Seek()` bounds the target against the cached `duration_`, writes it into `position_`, emits `sig_positionChanged` immediately, and only then calls `setPosition()`. The early emit gives the UI instant feedback and, more importantly, keeps the zero-guard from the previous fix from swallowing a real seek-to-zero: without it the backend's `positionChanged(0)` is dropped, `position_` keeps its old value, and the handle snaps back on the next `updateTimeline()`. A duration of zero means no loaded media, so the call is ignored.
- `SetVolume()` clamps to `0.0–1.0` before writing to `QAudioOutput::setVolume()`; the linear scale is deliberate (not `QAudio::convertVolume()`), matching the existing 0–100 slider.
- `SetPlaybackRate()` forwards directly. The speed items are now created with `addItem(text, rate)` and read back through `currentData().toReal()`, so the UI never parses `"1.5x"`; `setCurrentIndex(1)` replaces the former `setCurrentText("1.0x")`.
- `Close()` switches to `Idle` **first**, then `stop()`, `setSource(QUrl())`, and a timeline reset. The ordering matters: `stop()` emits `StoppedState`, and the existing handler converges `Playing`/`Paused` into `Stopped`, which would re-label an already-closed player as playable if the state had not moved to `Idle` beforehand. The empty source is what actually makes Qt Multimedia release the file and its decode resources, so no audio from the previous file can linger. `~LocalPlayerWgt()` calls it, which covers app exit and page destruction; `player_` is still alive at that point because children are destroyed after the owning widget's destructor body runs.
- Error fallback: `mediaStatusChanged == InvalidMedia` while still in `Opening` now enters `Error` and reports a generic "无法识别该媒体文件". Qt normally emits `errorOccurred()` as well, so this branch exists only so a failure that reports nothing cannot leave the UI stuck in "loading"; if the specific error arrives afterwards it is still emitted, and because `UpdatePlaybackState(Error)` is already a no-op, the specific Chinese message simply overwrites the generic one in `lastError_`.
- The progress slider's phase-3.5 `WA_TransparentForMouseEvents` opt-out is gone. Mouse dragging uses `sliderPressed`/`sliderMoved`/`sliderReleased`: the press sets `seeking_`, during which `updateTimeline()` treats the slider value as the single source of truth for the position text so the handle is not yanked back by backend updates; the release clears the flag and submits exactly one `Seek()`, so a drag does not fire a decode per pixel of travel.
- Keyboard arrows and groove clicks change the slider value without ever raising `sliderMoved`, so a second handler on `valueChanged` covers them. It is gated by a new `updatingSlider_` flag that `updateTimeline()` sets around its programmatic `setValue()`. Without that gate every position update during normal playback would write back into the slider and immediately be reinterpreted as a user seek, turning each ~100 ms tick into a `setPosition()` call.
- Volume is connected with `valueChanged` (`value / 100.0f`) and then synchronised once after construction so the backend starts at the slider's displayed 80 rather than Qt's default 1.0. Speed is connected the same way on `currentIndexChanged`.

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0 with no warnings; `LocalPlayerWgt.o`, `moc_LocalPlayerWgt.o` and `LocalPlayer.o` all rebuilt and `debug/ECloudAssistant.exe` relinked (33,867,556 bytes, 2026-09-29 21:00:24). `git diff --check` clean (only the pre-existing CRLF warnings). The `moc_LocalPlayerWgt.o` rebuild confirms the new destructor was seen by moc.

Manual verification pending, one scenario per acceptance item: drag the slider to seek while playing and while paused (including to the far left); change volume and confirm it survives opening another file; change speed and confirm it applies immediately; open a corrupt or unsupported file and confirm the Chinese error appears instead of a permanent "loading"; play A and open B mid-playback and confirm only B's audio is heard with the timeline reset; exit while playing and confirm no lingering audio.

Commit ID: not created in this change. Remaining limitations: volume is linear rather than perceptual, so the low end is coarse; opening a file does not reset volume or speed, because both belong to the player rather than to the file; the fill mode is still a compile-time constant; and the phase 3.6 error mapping still covers only the four `QMediaPlayer::Error` values plus the `InvalidMedia` fallback.

Rollback point: revert the four added `LocalPlayer` methods and their declarations, the `InvalidMedia` branch in `mediaStatusChanged`, the destructor and the two new private methods in `LocalPlayerWgt`, the `seeking_`/`updatingSlider_` members, and the `speedCombo_` `addItem`/`currentData` change; then restore `progressSlider_->setAttribute(Qt::WA_TransparentForMouseEvents)` to return to the phase 3.5 display-only slider. No other component is affected.

## Split the local player page constructor into three builders (本地播放, structure only)

Date: 2026-09-29. Goal: make `LocalPlayerWgt` readable without changing any behavior. The constructor had grown to about 180 lines that built the video area, built the whole control bar, wired every signal, and synchronised the initial volume and speed in one uninterrupted block, so reviewing any single concern meant reading the other three.

Decision: no new class, no new file, no new abstraction layer. The player is ~540 lines across the two units, has exactly one consumer, and the pipeline it drives is provided wholesale by Qt Multimedia, so a `PlayerCore`/controller split would only add indirection. `context/本地播放.md` already cancels that plan explicitly, and this change keeps it cancelled. Files stay as they are; only the method boundaries move.

Affected files: `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}` only. Nothing in `Player/`, no QSS, no protocol or server change.

Behavior:

- Three new private methods, all pure code movement. `QWidget* buildVideoArea()` creates the video surface frame, the empty-state page, the stacked widget and the player's video widget, and returns the surface for the root layout. `QWidget* buildControlBar()` creates the progress row, buttons, volume and speed controls and returns the control bar. `void bindPlayerSignals()` holds every `connect` plus the one-shot `SetVolume`/`setPlaybackRateFromCombo` initial synchronisation.
- The constructor now reads as four steps: create the backend, assemble the root layout from the two returned widgets, connect, then run the first `applyLocalPlaybackState()` and load the stylesheet. The `videoSurface`/`controlBar` locals were previously declared inline; they are now the return values of the builders.
- Builders return `QWidget*` rather than `QFrame*` so the header needs no new forward declaration for a type the caller never uses.
- The working tree already carried a separate review pass, recorded in the "Phase 3.6 review fixes before initial playback commit" entry below, which added the `hideEvent()` media release, the `hasVideo()` first-frame gate, invalid-path source clearing, Opening-time Stop cancellation and the slider-state reset. This change is structure only and preserves all of it; the two entries touch overlapping files but no overlapping lines.

Verification: Debug `mingw32-make.exe -f Makefile.Debug -j4` exit 0 with no warnings; `LocalPlayerWgt.o` and `moc_LocalPlayerWgt.o` rebuilt and `debug/ECloudAssistant.exe` relinked (33,873,665 bytes, 2026-09-29 21:41:01). `git diff --check` clean. Widget construction order is unchanged (the empty-state page and the player's video widget are added to the stack in the same order, and the root layout still adds the video surface with stretch 1 and the control bar without), so no visual or layout difference is expected. Manual spot check pending: open the page and confirm the layout, the empty state, and the controls look and behave exactly as before.

Commit ID: not created in this change. Remaining limitations: `LocalPlayerWgt.cpp` is now ~370 lines holding layout, interaction and presentation state in one class; that stays acceptable while the player has one consumer, and the next natural split — if one is ever needed — is on the interaction side (slider/seek state), not on the media side.

Rollback point: inline the three methods back into the constructor body in their original order and drop their declarations from `LocalPlayerWgt.h`. No other file is involved.

## Phase 3.6 review fixes before initial playback commit

Date: 2026-09-29. Goal: close lifecycle and state gaps found while reviewing the phase 3.6 implementation before committing it.

Affected files: `ECloudAssistant/Player/LocalPlayer.cpp`, `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`, `context/本地播放.md`, and `context/WORKLOG.md`.

Behavior: leaving the local page now calls `Close()` from `hideEvent()`, since switching `QStackedWidget` pages does not destroy them. `LoadedMedia` starts first-frame preview only when `QMediaPlayer::hasVideo()` is true, so an audio-only file waits for an explicit Play command. An invalid path also stops and clears the previous source. Stop during Opening cancels the load through `Close()`; `InvalidMedia` after loading but before/during preview can still enter Error. Disabling the progress slider on Opening/Idle/Error clears its dragging state, and no seek is sent from a disabled slider. The documentation now distinguishes implemented code from the still-pending full-client runtime acceptance.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` rebuilt `LocalPlayerWgt.o`, `moc_LocalPlayerWgt.o`, `LocalPlayer.o` and linked `debug/ECloudAssistant.exe`, exit 0, with no new warnings. Full-client manual checks remain: audio-only opening should stay silent until Play; navigation away should stop audio and reset the timeline; drag during load/error should not poison the next file; opening another file or a removed path while playing should not leave the old audio; seek, volume, speed, first-frame preview and error messaging should be confirmed with real files.

Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Rollback point: remove this review's `hasVideo()` gate, page `hideEvent()`, invalid-path source release, Opening Stop cancellation, broadened `InvalidMedia` fallback and slider-state reset; the pre-review phase 3.6 behavior then returns.

## SRS 与自研 RTMP Server 延迟 A/B 方案

Date: 2026-09-29. Goal: prepare a same-host, same-address A/B experiment to test whether replacing the existing RTMP relay with SRS materially reduces end-to-end preview latency.

Affected files: `context/更换推拉流服务器.md` and this worklog. No client, signaling, encoder, puller or server source changed.

Behavior: the plan switches the two RTMP servers sequentially on the existing `192.168.3.130:1935` endpoint, so both clients continue using the unchanged `CREATESTREAM → PLAYSTREAM` URL path. It fixes the runtime conditions, requires a project-publisher/project-puller compatibility check, specifies A→B→B→A visual-delay sampling, and separates stable delay from startup time. The historical 59.33 ms phase-two figure is background context, not this experiment's A baseline. Official SRS realtime configuration is linked; the installed version and exact configuration still need recording before deployment.

Verification: checked the address-generation, signal-forwarding, publish-ready and FFmpeg pull call paths against current source; checked the official SRS realtime example; `git diff --check` passed. No SRS command was found on this Windows PATH, and WSL distro enumeration returned access denied. No SRS deployment, RTMP interoperability run, latency data or bottleneck conclusion is claimed. Commit ID: not created in this change.

Remaining limitation: the actual RTMP server host, SRS process/configuration and two-client capture environment must be available for the planned A/B run. Rollback point: remove the experiment document and this worklog entry; no runtime rollback is needed for this documentation-only change.

## 自研 RTMP Server A 组初测记录

Date: 2026-09-30. Goal: preserve the user-provided PIPE-STATS and approximate visual-delay range before switching to SRS. Affected file: context/更换推拉流服务器.md and this worklog; no code or runtime configuration changed.

Evidence: 12 complete reporting windows total 361 captured and 360 encoded frames; all windows report dup = 0 and skip = 0. The final 31/30 window does not by itself prove a dropped frame. Approximate reported visual delay is 55～80 ms. The incomplete leading 1685" fragment was excluded. The original complete log lines are retained in the experiment document.

Verification: arithmetic checked against the 12 supplied lines; git diff --check passed after the documentation edit. No new build or runtime test was performed. Limitations: server identity/configuration hash, per-sample visual readings and raw imagery are absent, so no median/P90, formal A/B verdict or bottleneck conclusion is claimed. Commit ID: not created. Rollback point: remove this record and its experiment-document subsection.

## RTMP connect carries tcUrl for SRS (SRS A/B, publish-side fix)

Date: 2026-09-30. Goal: fix the first SRS interop failure, where the publisher was rejected before publishing. SRS logs `Invalid RTMP connect packet without tcUrl`, so the publish URL `rtmp://192.168.3.130:1935/live/2` could not enter the normal streaming stage and the client logged `RTMP publish timeout` then `CREATESTREAM ERROR`. The self-built `ENET/RtmpServer` never checked the field, which is why this only surfaced on SRS.

Affected files: `ECloudAssistant/Pusher/rtmp/rtmp.h`, `ECloudAssistant/Pusher/rtmp/RtmpConnection.{h,cpp}`, `context/更换推拉流服务器.md`, and this worklog.

Behavior:

- `Rtmp::GetTcUrl()` added after `GetApp()`; it rebuilds `rtmp://<ip_>:<port_>/<app_>` from the values `ParseRtmpUrl()` already parsed, so the connect layer does not re-parse the URL.
- `RtmpConnection` gains a `tc_url_` member, set from `Rtmp::GetTcUrl()` in the constructor next to `app_ = rtmp->GetApp();`.
- `RtmpConnection::Connect()` now writes `objects["tcUrl"] = AmfObject(tc_url_)` alongside the existing `app` and `type`. Only this one required field was added; no other optional connect fields were introduced. `AmfObjects` is an `unordered_map`, so key order inside the packet is not fixed, but SRS looks up keys by name and is unaffected.

Verification: an independent probe (`build/.../probe/tcurl_probe.cpp`, inside the gitignored build directory) encoded the connect object the same way `Connect()` does and decoded it back through the client's own `AmfDecoder` using the same two-step path as `HandleInvoke()`; it read back `tcUrl = rtmp://192.168.3.130:1935/live` matching the expected value (`match=1`), with `app = live` and stream name `2` unchanged. Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` rebuilt `RtmpConnection.o` (2026-09-30 15:16:57) and relinked `debug/ECloudAssistant.exe` (33,887,009 bytes, 15:17:06) with no new warnings; a follow-up make reports nothing to do, confirming the artifacts post-date the edit. `git diff --check` clean.

Commit ID: not created in this change. Remaining limitation: SRS-side interop is not yet run — the SRS log must stop reporting the missing `tcUrl`, the publisher must receive `NetStream.Publish.Start`, and the puller must show the stream. Since the self-built server does not check `tcUrl`, the added field does not affect group A, so both A and B can use the same new client binary, but the old group A samples remain reference-only and must be re-sampled.

Rollback point: remove `Rtmp::GetTcUrl()`, drop the `tc_url_` member and its constructor assignment, and delete the `objects["tcUrl"]` line in `Connect()`. No other file is involved.

## PIPE-STATS reports encoded bitrate in Chinese labels

Date: 2026-09-30. Goal: add the post-encode bitrate to the per-second `[PIPE-STATS]` line and switch the line's labels to Chinese as requested. Previously the line had no throughput figure at all, so the 12000 kbps encoder target could only be checked indirectly through the RTMP server or puller.

Affected files: `ECloudAssistant/Pusher/VideoPipelineStats.{h,cpp}` and `ECloudAssistant/Pusher/RtmpPushManager.cpp`; no protocol, capture or encoding behavior changed.

Behavior:

- `OnFrameEncoded()` gains a `frameBytes` parameter carrying `out_frame.size()`, the encoded H.264 bitstream length of that frame. It may be 0 while the encoder is still buffering; it is simply added to the interval total.
- `VideoPipelineStats` accumulates `encodedBytes_` per reporting window (cleared in `ResetInterval()` together with the other per-interval totals) and reports `bitrateKbps = encodedBytes_ * 8 / elapsedUs` scaled to the window's real duration in `ReportIfDue()`.
- The whole `[PIPE-STATS]` line now uses Chinese labels in the same order as before, with the new field inserted before the sequence number: `采集帧率 / 编码帧率 / 采集帧数 / 编码帧数 / 重复 / 跳帧 / 等待均值us / 等待峰值us / 转换均值us / 编码均值us / 码率kbps / 序号`. The `us` unit suffixes stay Latin to avoid mixed-width digits confusion.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug `mingw32-make.exe -f Makefile.Debug -j4` rebuilt `VideoPipelineStats.o`, `RtmpPushManager.o`, `moc_RtmpPushManager.o` and relinked `debug/ECloudAssistant.exe` (33,887,010 bytes, 2026-09-30 15:52:42), exit 0 with no new warnings (the sign-compare and unused-parameter warnings in the output are pre-existing). `git diff --check` clean. Runtime confirmation of the printed bitrate pending the next publish run; expected value is near the 12000 kbps target minus B-frame/buffering effects.

Commit ID: not created in this change. Remaining limitation: the figure covers video only (audio AAC bytes are not counted) and measures the encoder output, not what RTMP actually sent. Rollback point: revert the Chinese label string, remove the `frameBytes` parameter and `encodedBytes_` accumulation, and restore the old `OnFrameEncoded()` call site — four small edits in three files.

## Pull-side per-second pipeline stats ([PULL-STATS], phase-2 task 2)

Date: 2026-09-30. Goal: implement task 2 of `context/解码渲染端延迟处理.md` — instrument the control-side video path (`av_read_frame()` → H.264 packet queue → decode/convert → `avContext_->video_queue_` → `videoPlay()` → `sig_repaint` → `Repaint()` → `paintGL()`) with per-second summaries, without changing any playback behavior, so task 3 can locate the backlog from real data. Same convention as the pusher's `[PIPE-STATS]`.

Affected files: `ECloudAssistant/Codec/VideoPullStats.{h,cpp}` (new), `ECloudAssistant/Codec/AV_Queue.h`, `ECloudAssistant/Codec/H264_Decoder.{h,cpp}`, `ECloudAssistant/Codec/AVDEMuxer.{h,cpp}`, `ECloudAssistant/Codec/Codec.pri`, `ECloudAssistant/Puller/UI/AVPlayer.{h,cpp}`, `ECloudAssistant/Puller/Render/OpenGLRender.{h,cpp}`.

Behavior:

- `VideoPullStats` is a static class with all-atomic state and a `kEnabled` kill switch; the per-second report is printed only from the videoPlay thread as one `[PULL-STATS]` line (读包率/解码率/取帧率/重绘率/绘制率, 包队列当前/峰值, 帧队列当前/峰值, 信号等待均值/峰值ms, 绘制滞后均值/峰值ms). The first call only aligns the reporting window, mirroring `VideoPipelineStats`. `AVPlayer::HandleStartStream()` calls `Reset()` so each new pull session starts from zero.
- Demux thread: `AVDEMuxer::FetchStream()` counts each video packet and samples the packet-queue peak right after `put_packet()` (post-push is the peak moment) via the new `H264_Decoder::InputQueueSize()`. Decoder thread: `H264_Decoder::run()` counts decoded/converted frames and the `avContext_->video_queue_` peak after each push. videoPlay thread: counts pop/emit (one counter — pop is immediately followed by emit), and samples both queues' current length at report time through `AVDEMuxer::VideoPacketQueueSize()` (returns 0 when the decoder was reset) and `AVQueue::size()`.
- `sig_repaint` now carries the emit timestamp: `sig_repaint(AVFramePtr frame, qint64 emitUs)` connected to the widened `OpenGLRender::Repaint(AVFramePtr frame, qint64 emitUs)`. `Repaint()` measures the emit→slot wait at entry and passes its entry time to `OnPaintScheduled()` right after `update()`; `paintGL()` measures the Repaint→paintGL lag from that pending stamp (when Qt coalesces multiple updates, the last Repaint wins). The per-frame timestamp is required: a shared "last emit" atomic would read near-zero during exactly the event-queue backlog this phase hunts for, because `videoPlay()` would keep overwriting it while older frames still sit queued.
- Cross-thread queue reads are now lock-safe: `AVList::empty()/size()` take the same mutex as push/pop (they previously read `size_` unsynchronized, which the existing `videoPlay()` empty-check and decoder spin already did cross-thread). All other statistics state is atomic, so the four calling threads (demux, decoder QThread, videoPlay std::thread, GUI) need no additional locking. Audio path untouched; `LocalPlayer` does not use AVDEMuxer/OpenGLRender, so local preview does not pollute the counters.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug — `qmake.exe -o Makefile ..\..\ECloudAssistant.pro -spec win32-g++ "CONFIG+=debug"` then `mingw32-make.exe -f Makefile.Debug -j8` rebuilt `VideoPullStats.o` (2026-09-30 16:26:25), the touched objects, moc files, and relinked `debug/ECloudAssistant.exe` (34,324,943 bytes, 16:26:44), exit 0; the AVFrame-deprecation/sign-compare/unused-parameter warnings in the output are pre-existing. An initial build failure was environmental: shell PATH resolved `g++` to `C:\msys64\ucrt64\bin` instead of Qt's MinGW 13.1.0; prepending `D:\Qt\Tools\mingw1310_64\bin` fixed it. `git diff --check` clean. Runtime sampling is pending: the `[PULL-STATS]` lines must be captured in a real SRS push-pull run to compare against the pusher's `[PIPE-STATS]` and drive task 3.

Commit ID: not created in this change. Remaining limitation: statistics are compile-verified only; paint-lag averages divide by the paint count even when some paints were resize-triggered without a pending repaint (counts may show 绘制率 > 重绘率 in such windows). Rollback point: delete `VideoPullStats.{h,cpp}` and its call sites, revert `sig_repaint`/`Repaint` to the single-argument form, and restore the lock-free `empty()/size()` — contained edits in the nine files above.
## Pull-side read wait and video packet arrival interval

Date: 2026-09-30. Goal: add the two requested receive-side timing metrics to the existing per-second `[PULL-STATS]` line before changing any playback behavior.

Affected files: `ECloudAssistant/Codec/AVDEMuxer.cpp`, `ECloudAssistant/Codec/VideoPullStats.{h,cpp}`, and this worklog. The existing `Codec.pri` registration of `VideoPullStats.cpp` is reused.

Behavior: `AVDEMuxer::FetchStream()` measures each `av_read_frame()` call with `steady_clock` and passes its duration and completion timestamp to `VideoPullStats`. The call duration covers FFmpeg's entire read/demux call for video, audio, and failed returns; it is not a pure network waiting measurement. Consecutive successful video packets use their call-completion timestamps to calculate arrival gaps; the first video packet has no gap sample. The previous video arrival survives one-second report windows and is cleared for a new pull session. `[PULL-STATS]` adds average/peak milliseconds for both metrics, once per second, without per-packet logging.

Verification: Qt 6.10.1 MinGW Debug `mingw32-make.exe -f Makefile.Debug -j1` rebuilt and linked `debug/ECloudAssistant.exe`, exit 0. `git diff --check` clean for the tracked source change. Runtime values against SRS are pending; a real pull session is required to interpret whether long calls or packet gaps correlate with visible latency. Commit ID: not created in this change.

Rollback point: remove the timing call around `av_read_frame()`, the new timing counters and report fields in `VideoPullStats`, and this entry; the earlier queue/decode/render statistics remain.
## Submit pending client diagnostics and local-player layout work

Date: 2026-10-04. Goal: submit the already implemented SRS experiment records, push-side bitrate report, pull-side timing and queue/render diagnostics, and the local-player constructor structure cleanup together with their handoff documents.

Affected files: `ECloudAssistant/Codec/AVDEMuxer.{h,cpp}`, `AV_Queue.h`, `Codec.pri`, `H264_Decoder.{h,cpp}`, `VideoPullStats.{h,cpp}`, `ECloudAssistant/Puller/Render/OpenGLRender.{h,cpp}`, `ECloudAssistant/Puller/UI/AVPlayer.{h,cpp}`, `ECloudAssistant/Pusher/RtmpPushManager.cpp`, `VideoPipelineStats.{h,cpp}`, `ECloudAssistant/UI/center/LocalPlayerWgt.{h,cpp}`, `context/WORKLOG.md`, `context/本地播放.md`, `context/更换推拉流服务器.md`, and `context/解码渲染端延迟处理.md`. The earlier tcUrl source fix is already in local commit `aef1f8c` and is pushed with this submission.

Behavior: this submission packages the behavior described in the detailed worklog entries above. It adds only statistics to the remote playback path, video bitrate to the push statistics, and splits one local-player constructor into three methods without changing playback behavior.

Verification: Qt 6.10.1 MinGW Debug `mingw32-make.exe -f Makefile.Debug -j1` exited 0 on 2026-10-04 (`Nothing to be done for 'first'`), confirming the existing build is up to date; the earlier detailed entries record the actual rebuilds. `git diff --check` was clean. Runtime SRS push/pull statistics and local-player manual acceptance remain pending. Commit ID: this entry's containing commit (resolve with `git log -1 --oneline`). Rollback point: revert this commit to remove the diagnostics and layout cleanup; revert `aef1f8c` separately only if the tcUrl fix itself must be undone.

## Phase 2 task 3 display-backlog plan documented

Date: 2026-10-04. Goal: record the requested task-three plan for identifying and addressing backlog between decoded video frames and final rendering. Affected file: `context/解码渲染端延迟处理.md`; this worklog records the documentation update. Behavior: no code or playback behavior changed. The plan requires `[PULL-STATS]` evidence before adopting latest-frame plus a single pending Qt render notification, limits drops to decoded `AVFrame`s, and calls out thread safety, lost wakeup, stop/reconnect and runtime acceptance. Verification: documentation diff reviewed; `git diff --check` clean. Commit ID: not created. Remaining limitation: no new runtime stats or implementation yet. Rollback point: remove the task-three section and restore its short list item and status line.

## Phase 2 task-three design and concise pull statistics

Date: 2026-10-04. Goal: record the concrete latest-frame/single-notification design and shorten the per-second pull log for display-backlog diagnosis. Affected files: `context/解码渲染端延迟处理.md`, `ECloudAssistant/Codec/VideoPullStats.{h,cpp}`, `ECloudAssistant/Codec/AVDEMuxer.{h,cpp}`, `ECloudAssistant/Puller/UI/AVPlayer.cpp`, and this worklog. Behavior: documentation now specifies a one-frame decoded-video queue, synchronized latest-frame/sequence/pending state, frame-free queued notification, paintGL-time snapshot, session invalidation and reconnect preconditions; these playback changes remain a plan. The existing `[PULL-STATS]` output is reduced from 17 fields to five: decode rate, decoded-frame queue peak, paintGL call rate, Qt signal wait peak, and Repaint-to-paint lag peak. Other counters remain in code but are no longer printed by default; the now-unused `AVDEMuxer::VideoPacketQueueSize()` query is removed. Verification: Qt MinGW Debug single-thread build rebuilt affected objects and linked `debug/ECloudAssistant.exe`, exit 0; runtime values and the proposed playback policy remain untested. Commit ID: not created. Remaining limitation: the requested additional log content has not been specified yet. Rollback point: restore the prior `ReportIfDue` format and arguments, and remove this task-three design expansion and logging note.

## Phase 2 task 3 latest-frame display policy implemented

Date: 2026-10-04. Goal: implement task three of `context/解码渲染端延迟处理.md` — stop the decoded-video-to-display path from accumulating backlog by keeping only the newest decoded frame and issuing at most one pending render notification, without dropping H.264 packets.

Affected files: `ECloudAssistant/Codec/AV_Common.h`, `ECloudAssistant/Codec/H264_Decoder.cpp`, `ECloudAssistant/Codec/VideoPullStats.{h,cpp}`, `ECloudAssistant/Puller/Render/VideoFramePresenter.{h,cpp}` (new), `ECloudAssistant/Puller/Puller.pri`, `ECloudAssistant/Puller/Render/OpenGLRender.{h,cpp}`, and `ECloudAssistant/Puller/UI/AVPlayer.{h,cpp}`.

Behavior:

- `AVContext::video_queue_` is now a new `VideoFrameQueue` defined in `AV_Common.h`: a single-slot, mutex-protected latest-frame queue. `push()` replaces an un-fetched old frame and returns whether it did, so `H264_Decoder::run()` can count the replacement as a drop; `pop()/empty()/size()` keep the same call shape as before. The audio queue still uses the FIFO `AVQueue`. H.264 packets continue to be fed to the decoder in order and are never dropped.
- New `VideoFramePresenter` (playback thread ↔ GUI thread handshake) owns `latestFrame_`, `producedSeq_`, `takenSeq_`, `paintedSeq_`, `repaintPending_` and a session id under one mutex. `Publish()` stores the newest frame, reports whether it replaced one the GUI had not taken and whether the caller must post a notification, so only one un-executed notification can ever exist; `OnNotified()` clears the pending flag only when the session id matches; `TakeFrameForPaint()` returns a frame, sequence and session snapshot only when `producedSeq_ > paintedSeq_`; `MarkPainted()` advances the painted sequence only for the same session; `Reset()` drops the frame, bumps the session id and zeroes the sequences. The presenter deliberately does not depend on the statistics module — overwrite counts are reported by the caller.
- `AVPlayer::sig_repaint` is now `(quint64 sessionId, qint64 emitUs)` and carries no frame. `videoPlay()` pops the single-slot queue, calls `Presenter().Publish()`, reports an overwrite once, and emits only when the presenter says a notification is missing; the old per-frame `AVFramePtr`-carrying event and its `Q_DECLARE_METATYPE(AVFramePtr)` registration are removed.
- `OpenGLRender::Repaint(AVFramePtr, qint64)` is replaced by the notification slot `OnRepaintRequested(quint64, qint64)`: it ignores stale sessions, records the emit→slot wait, and calls `update()` once. Frame acquisition and texture upload/rebuild happen inside `paintGL()`, where the GL context is actually current, before the `glViewport` math so a resolution change still refreshes the letterbox geometry; `MarkPainted()`/`OnFramePainted()` run after the GL draw call. `resize`-triggered repaints with no new frame keep the existing textures within the same session.
- Reconnect fix: `stop_` is now `std::atomic<bool>`; `HandleStartStream()` resets `stop_`, recreates the `AVDEMuxer` that `Close()` destroyed, clears both context queues and resets the presenter and statistics, so a second session no longer dereferences a null demuxer or exits immediately. `Close()` resets the presenter after joining the playback threads, so a queued notification from the previous session cannot draw into the new one.
- `[PULL-STATS]` now prints one line per second with: decode rate, decoded-frame queue peak, queue-drop count, latest-frame overwrite count, notification rate, painted rate, signal-wait peak ms and Repaint-to-paint lag peak ms. `paintGL` call count was replaced by the actually-painted frame count.

Correction (2026-10-04 review): the three `Pusher/rtmp` files have one UTF-8 BOM in both the working tree and commit `aef1f8c`, and this task has no diff in them. The earlier claim that this task removed doubled BOMs was incorrect.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug — regenerated makefiles (`qmake.exe -o Makefile ..\..\ECloudAssistant.pro -spec win32-g++ CONFIG+=debug CONFIG+=qml_debug`, exit 0) so the new `VideoFramePresenter.cpp` is compiled, then `mingw32-make.exe -f Makefile.Debug -j8` exited 0 and relinked `debug/ECloudAssistant.exe` (34,395,991 bytes, 2026-10-04 14:12). `debug/VideoFramePresenter.o` was produced. The only warnings in touched files are the pre-existing `OpenGLRender::showEvent` unused-parameter and the `AVFrame::channels`/signedness warnings elsewhere; no new warning came from the new code.

Commit ID: this entry's containing commit (`git log -1 --oneline`). Remaining limitation: the user supplied ten recent `[PULL-STATS]` windows with mostly 29–31 FPS decode/paint, frame queue peak 1, occasional single queue drops, no latest-frame overwrites, signal-wait peak up to 16.6 ms and paint-lag peak up to 5.8 ms. These do not show sustained display backlog. End-to-end latency before/after, long-run stability, visual artifacts and audio/video sync across stop and reconnect remain unverified. If no repeatable latency improvement appears, roll back this display policy.

Rollback point: revert this change set — restore `AVContext::video_queue_` to `AVQueue<AVFramePtr>` (removing `VideoFrameQueue`), delete `VideoFramePresenter.{h,cpp}` and its `Puller.pri` entries, restore `AVPlayer::sig_repaint`/`OpenGLRender::Repaint` to the frame-carrying form, and restore the previous `[PULL-STATS]` field list.

## Phase 2 task 3 review fixes

Date: 2026-10-04. Goal: fix the reviewed session-crossing paint race, inaccurate latest-frame drop count, stale GL texture after stop/reconnect, and handoff record drift. Affected files: `ECloudAssistant/Puller/Render/VideoFramePresenter.{h,cpp}`, `OpenGLRender.{h,cpp}`, `ECloudAssistant/Puller/UI/AVPlayer.cpp`, `ECloudAssistant/Codec/VideoPullStats.cpp`, `ECloudAssistant/tests/VideoFramePresenterTest.cpp`, `context/解码渲染端延迟处理.md`, and this worklog.

Behavior: the GUI paint snapshot now carries a session id; `MarkPainted()` ignores a completion from an earlier session. `takenSeq_` distinguishes a frame already taken for drawing from one actually replaced before pickup, so `latest覆盖` counts only the latter. Stop/reconnect resets the presenter and queues a GUI repaint; `paintGL()` clears textures from the previous session before drawing, and records a new frame only after the GL draw call. The experiment document now describes the implemented eight-field log and pending runtime acceptance. The earlier BOM claim was corrected above; no RTMP source file was changed.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug qmake regeneration and `mingw32-make.exe -f Makefile.Debug -j1` compiled and linked `debug/ECloudAssistant.exe`, exit 0. `tests/VideoFramePresenterTest.cpp` passed the old-session notification/paint and fetched-versus-overwritten cases. The user-provided `[PULL-STATS]` windows above are a short runtime sample; long-run behavior, visual artifacts and end-to-end latency remain unverified. Commit ID: this entry's containing commit (`git log -1 --oneline`). Rollback point: revert the presenter session/`takenSeq_` edits, reset-driven GUI texture cleanup, this test, and the corresponding document corrections.

## WGC desktop capture, phase 1

Date: 2026-10-04. Goal: add Windows Graphics Capture plus D3D11 as an optional desktop capture backend feeding the existing H.264/RTMP chain while preserving the GDI path. Affected files: `ECloudAssistant/Pusher/capture/ScreenCapture.h` and `WGCScreenCapture.{h,cpp}` (new), `GDISreenScapture.{h,cpp}`, `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`, `Pusher.pri`, `context/WGC采集.md`, and this worklog.

Behavior: `ScreenCapture` exposes the existing BGRA frame view, width/height, sequence and stop contract to the manager. WGC creates the primary-monitor capture item, D3D11 device, free-threaded frame pool and session on its worker thread; it reads the latest texture through a staging texture and `Map`, then supplies an owned, tightly packed BGRA buffer to the unchanged encoder. Static-screen ticks reuse that buffer while advancing a 30 FPS capture sequence; encoder PTS still derives from sequence differences. WGC can recreate the frame pool on content-size changes and reopen the session when the primary monitor handle changes. Default backend is GDI; `ECLOUD_CAPTURE_BACKEND=wgc` opts in, with GDI fallback on WGC initialization/first-frame failure. `RequestStop` wakes the encoder before `Close` joins the WGC worker and releases WinRT/D3D11 resources. No encoder, RTMP protocol or player behavior changed.

Verification: Qt 6.10.1 MinGW 13.1.0 Debug qmake regeneration and `mingw32-make.exe -f Makefile.Debug -j1` linked the full app, exit 0; `git diff --check` clean. Ignored build-directory probes showed WGC start/stop/restart twice at 1920×1080 BGRA, 31 WGC frames encoded through the existing H.264 encoder, a 20-second WGC RTMP publish to SRS, and `ffprobe` of that live SRS stream reporting H.264 1920×1080 at 30/1 FPS plus AAC. A 5-second default-GDI RTMP probe also encoded and published successfully. No commit created. Remaining limitations: runtime resize, DPI/monitor changes, full RTMP reconnect, long-run stability, picture quality, CPU savings and end-to-end latency are unmeasured; WGC still reads GPU texture back to CPU. Rollback point: unset `ECLOUD_CAPTURE_BACKEND` for immediate GDI fallback; to remove the feature, revert the new interface/WGC files, manager selection and qmake additions while preserving the original GDI implementation.

## WGC capture backend selection in remote UI

Date: 2026-10-04. Goal: replace the environment-variable backend switch with a visible choice beside the remote-start button. Affected files: `ECloudAssistant/UI/center/RemoteWgt.{h,cpp}`, `UI/brown/main.css`, `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`, `context/WGC采集.md`, and this worklog. Behavior: the dropdown is labelled "本机采集" because capture starts on this device when it is controlled; GDI remains the default. The selected enum is stored atomically across the GUI and signaling threads and read at the next push initialization. WGC initialization failure still falls back to GDI. The environment-variable check was removed; an active stream keeps its chosen backend. Verification: regenerated the Qt 6.10.1 MinGW 13.1.0 Debug Makefile, rebuilt and linked the full app (exit 0), and `git diff --check` passed. No GUI interaction or remote-session runtime test yet; no commit created. Rollback: revert the dropdown, enum setter, and selection read to restore the previous environment-variable switch.

## Capture backend signaling and per-session first-frame log

Date: 2026-10-04. Goal: let the controller's GDI/WGC selection reach the controlled device, synchronize the controlled UI with the backend that actually started, and log the capture method once per push session. Affected files: `ECloudAssistant/UI/tool/defin.h`, `Net/SigConnection.{h,cpp}`, `Puller/UI/{AVPlayer,PullerWgt}.{h,cpp}`, `UI/center/{RemoteManager,RemoteWgt}.{h,cpp}`, `Pusher/RtmpPushManager.{h,cpp}`, `ENET/SigServer/{define.h,SigConnection.cpp}`, `context/WGC采集.md`, and this worklog.

Behavior: the controller adds a one-byte backend choice (0=GDI, 1=WGC) to OBTAINSTREAM; SigServer forwards it in CREATESTREAM. Old short packets default to GDI. The controlled client starts the selected backend, retains GDI fallback, and queues a Qt signal to set its dropdown to the actual backend after the RTMP push opens. The encoder thread writes one `[CAPTURE] session first frame` log for the first frame of each push session; a new session logs again and WGC fallback reports GDI. Changing the dropdown during a stream affects the next session only. Existing encoders and RTMP framing are unchanged.

Verification: regenerated the Qt 6.10.1 MinGW 13.1.0 Debug Makefile and linked the client, exit 0; the server `define.h` and modified `SigConnection.cpp` passed MinGW C++11 syntax checks; client and server protocol structs both passed compile-time size checks (OBTAINSTREAM 15 bytes, CREATESTREAM 5 bytes); `git diff --check` passed. The existing `ENET/build` cache points to `/mnt/hgfs/...` and `/usr/bin/gmake`, so server link and two-client runtime propagation remain unverified. No commit created. Deployment requires rebuilding and replacing the SigServer binary along with both clients. Rollback: remove the two wire bytes and forwarding, restore the prior start-stream callbacks/UI synchronization, and remove the per-session log.

Submission (2026-10-04): commit ID is the containing commit for this entry (`git log -1 --format=%H -- context/WORKLOG.md` immediately after commit). Submitted scope includes the capture abstraction, WGC backend, UI selection, one-byte SigServer forwarding, per-session first-frame log, and the WGC experiment document. The unrelated whitespace edit in `WASAPICapture.cpp` and unrelated untracked files are excluded. Server rebuild/deployment and two-client live propagation remain pending.

## MSVC capture migration in primary checkout

Date: 2026-10-05. Goal: resolve the Qt Creator MSVC build failure in `D:\shared\assient` and apply the already verified C++/WinRT capture migration from the isolated Codex worktree. Affected files: `ECloudAssistant/Net/{BufferWriter,TcpConnection}.cpp`, `ECloudAssistant/Pusher/Pusher.pri`, `ECloudAssistant/Pusher/capture/WGCScreenCapture.cpp`, `context/WGC采集.md`, and this worklog. Behavior: removed two unused `unistd.h` includes, replaced handwritten MinGW WGC interfaces and UUIDs with SDK C++/WinRT types, and linked `windowsapp.lib` for MSVC. Existing capture format, GDI selection/fallback and signaling packets were not changed. Unrelated existing edits were preserved.

Verification: regenerated the Qt 6.10.1 MSVC 2022 Debug Makefile in `ECloudAssistant/build/Desktop_Qt_6_10_1_MSVC2022_64bit-Debug`; `jom /f Makefile.Debug -j4` completed and linked `debug/ECloudAssistant.exe`, exit 0. The same migrated WGC code previously passed two `Init`/frame/`Close` cycles in the isolated worktree; GDI fetched one BGRA frame there. Commit ID: none. Remaining limitations: no two-client RTMP test, runtime WGC-to-GDI fallback test, resize/monitor-change test or long-run comparison in this primary checkout. Rollback: restore these four source/project files to `a1998f8` and use the MinGW kit.

## MSVC Debug executable local deployment

Date: 2026-10-05. Goal: make the primary checkout's MSVC Debug client launch by double-clicking its exe. Affected files: this worklog; generated runtime DLLs and Qt plugin directories under ECloudAssistant/build/Desktop_Qt_6_10_1_MSVC2022_64bit-Debug/debug (ignored build output). Behavior: deployed Qt 6.10.1 MSVC Debug libraries and plugins with windeployqt --debug --no-translations --compiler-runtime, then copied the six FFmpeg 6 DLLs directly imported by ECloudAssistant.exe from D:\FFmpeg\ffmpeg-6.0-full_build-shared\bin. No project source changed for this deployment.

Verification: before deployment, double-click showed missing Qt6Widgetsd.dll; dumpbin confirmed the Qt Debug and FFmpeg DLL imports. windeployqt exited 0. After deployment, the exe stayed running for five seconds in a smoke test with PATH limited to Windows system directories; the test process was then stopped. Commit ID: none. Remaining limitation: interactive login, capture, playback, and running on another PC were not tested. windeployqt warned that dxcompiler.dll/dxil.dll and VCINSTALLDIR were unavailable; the five-second launch did not require them. Rebuilding the build directory may require redeployment.


## WGC capture function comments

Date: 2026-10-05. Goal: add Chinese explanations to the WGC capture functions for source reading. Affected files: `ECloudAssistant/Pusher/capture/WGCScreenCapture.cpp` and this worklog. Behavior: comments describe WGC setup, frame readback, worker synchronization, latest-frame publication, restart and cadence; executable code and protocol behavior are unchanged. Verification: MSVC Debug `jom /f Makefile.Debug -j4` recompiled `WGCScreenCapture.cpp` and linked `ECloudAssistant.exe`, exit 0; `git diff --check` passed on the two affected files. An initial build invocation failed because its command-line PATH assignment hid `cl`; retrying from the MSVC developer environment succeeded. Commit ID: none. Remaining limitation: runtime behavior was not retested for comment-only changes. Rollback: remove these comments and this entry.

## Capture frame abstraction refactor and WGC GPU texture output (WGC phase 2)

Date: 2026-10-05. Goal: let a capture frame carry either a CPU BGRA buffer or a D3D11 texture, so WGC can hand out `ID3D11Texture2D` directly and the capture/encoder boundary no longer forces every backend to produce CPU memory. This phase is a data-structure and interface refactor only — no GPU BGRA→NV12, no NVENC/QSV/AMF, no zero-copy. Acceptance covers architecture and lifetime, not performance.

Affected files: `ECloudAssistant/Codec/VideoFrame.h` (new), `ECloudAssistant/Codec/Codec.pri`, `ECloudAssistant/Codec/H264Encoder.{h,cpp}`, `ECloudAssistant/Pusher/capture/ScreenCapture.h`, `ECloudAssistant/Pusher/capture/GDISreenScapture.{h,cpp}`, `ECloudAssistant/Pusher/capture/WGCScreenCapture.{h,cpp}`, `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`, `context/WGC采集.md`, and this worklog. `Pusher.pri` was not changed.

Behavior: the new platform-free `VideoFrame.h` defines `VideoFrameKind{Cpu,Gpu}`, `VideoPixelFormat{Bgra8,Nv12,Unknown}`, an abstract `IGpuVideoFrame` handle (`width/height/format/nativeTexture()/nativeDevice()`, all native resources as `void*`), `CpuFrameView` (owner/data/stride) and `VideoFrame` (kind + cpu + gpu shared_ptr + width/height/sequence/capturedAt); D3D11 is interpreted only inside `WGCScreenCapture.cpp`. `ScreenCapture::WaitLatestFrame` now takes `VideoFrame&`, and the interface adds `CaptureOutput{CpuReadback,GpuTexture}` plus `SupportsGpuOutput()/SetOutput()` defaulting to unsupported, which GDI inherits. GDI only fills the CPU fields (kind Cpu, `cpu.data` into the front of its three-buffer pool, empty owner); its index-swap logic is unchanged. WGC gained an output mode (default `CpuReadback`); in GPU mode it `CopyResource`s the pool texture into a per-frame owned `D3D11_USAGE_DEFAULT` texture, wraps it in `WgcGpuFrame` (which holds both `ComPtr<ID3D11Texture2D>` and `ComPtr<ID3D11Device>` for lifetime safety) and releases the pool frame immediately — the pool texture is not held because the two-buffer pool would otherwise starve `TryGetNextFrame`. The staging/Map/memcpy readback path is kept verbatim as the fallback. `H264Encoder` gained `EncodeFrame` (dispatches on `kind`), `EncodeCpuFrame` (the existing software path) and `EncodeGpuFrame` (a stub that logs once and returns -1). `RtmpPushManager::EncodeVideo` now uses `VideoFrame` and `EncodeFrame` with no backend branches, and a new `SetCaptureOutput` passthrough (default `CpuReadback`) is applied inside `Init()` before `screen_Capture_->Init()`. GPU output mode is code-only for now: it is not wired to the UI or signaling and is not the default.

Verification: Qt 6.10.1 MSVC2022 Debug `qmake` + `jom /f Makefile.Debug -j4` compiled and linked `debug/ECloudAssistant.exe`, exit 0, no errors (pre-existing warnings only). A launch smoke test passed (process started, stayed alive, terminated cleanly). Not verified: the acceptance cases that need a signaling server plus SRS and two clients — GDI streaming, stable WGC `D3D11Texture2D` acquisition, WGC CPU-readback streaming, and repeated Stop/Restart (20x) — were not run on this machine, nor were runtime resolution/DPI/monitor changes or long-run behavior. Commit ID: none. Rollback: revert the listed files to `a1998f8`; the new `VideoFrame.h` and `Codec.pri` entry are additive and can be dropped with the other edits.

## Software/hardware H.264 encoder coexistence (WGC phase 3, step 1)

Date: 2026-10-05. Goal: let the push session choose between the software libx264 encoder and a hardware H.264 encoder (nvenc/qsv/amf) while the input is still a CPU BGRA frame, falling back to software when no hardware encoder opens. This is step 1 of phase 3; the WGC D3D11 texture goes straight into the hardware encoder only in step 2. No GPU BGRA→NV12, no `AVHWFramesContext`, no zero-copy here.

Affected files: `ECloudAssistant/Codec/VideoEncoder.{h,cpp}`, `ECloudAssistant/Codec/SoftwareVideoEncoder.{h,cpp}` (new), `ECloudAssistant/Codec/HardwareVideoEncoder.{h,cpp}` (new), `ECloudAssistant/Codec/H264Encoder.{h,cpp}`, `ECloudAssistant/Codec/Codec.pri`, `ECloudAssistant/Pusher/RtmpPushManager.{h,cpp}`, `context/WGC采集.md`, and this worklog.

Behavior: `VideoEncoder` is now an abstract base holding the parts software and hardware share — FFmpeg context creation, the CPU BGRA→target-pixel-format conversion, send/receive and the SPS/PPS extradata. `Open` is a template method that calls two hooks: `FindCodec` (by ID for software, by name for hardware) and `ConfigureCodec` (output pixel format, profile/level, private options). `SoftwareVideoEncoder` carries the previous libx264 behavior verbatim: `AV_PIX_FMT_YUV420P`, `FF_PROFILE_H264_BASELINE`, level 40, `rc_min/max/buffer = bitrate`, `tune=zerolatency`, `preset=ultrafast`. `HardwareVideoEncoder` takes a codec name, looks it up with `avcodec_find_encoder_by_name`, sets `AV_PIX_FMT_NV12` (the base converter targets `codecContext_->pix_fmt`, so nothing else changes) and sets per-vendor low-latency options (`h264_nvenc` preset p1 / tune ull, `h264_qsv` preset veryfast / async_depth 1, `h264_amf` usage ultralowlatency / quality speed); it deliberately does not copy x264's profile/level or rate-control fields. `H264Encoder::OPen` takes a new `VideoEncoderKind{Software,Hardware}`, defaulting to `Software`; when `Hardware` is requested it tries `h264_nvenc → h264_qsv → h264_amf` in order and falls back to software if none opens, logging the encoder that actually took effect (`[ENCODE] active encoder = ...`). `RtmpPushManager` gained `SetEncoderKind/GetEncoderKind` (default `Software`, so existing behavior is unchanged) and passes it into `OPen`. The per-frame dispatch stays exactly where it was: `H264Encoder::EncodeFrame` branches once on `VideoFrameKind`; there is no software/hardware branch per frame. Audio remains AAC software encoding, untouched.

Verification: Qt 6.10.1 MSVC2022 Debug, regenerated the Makefile with `qmake -o Makefile ..\..\ECloudAssistant.pro` and ran `jom /f Makefile.Debug -j4` — compiled the new `SoftwareVideoEncoder.cpp`/`HardwareVideoEncoder.cpp` and the refactored `VideoEncoder.cpp`, recompiled dependents, and linked `debug/ECloudAssistant.exe`, exit 0; a second incremental run reported nothing to do and exit 0 (pre-existing warnings only). Not verified: whether `h264_nvenc`/`h264_qsv`/`h264_amf` actually open on this machine (needs a matching GPU and driver), and any end-to-end streaming with a hardware encoder (needs signaling server + SRS + two clients). The default `Software` path was not re-run at runtime. Commit ID: none. Rollback: revert the listed files to `a1998f8`; the two new encoder classes and the `Codec.pri` entries are additive and can be dropped with the other edits.


## WGC GPU 直通与运行时软编退化

Date: 2026-10-06. Goal: complete the WGC low-latency path by sharing the capture D3D11 device with NVENC, avoiding GPU-to-CPU readback on the hardware path, and rebuilding the push session with software encoding after a runtime video-path failure. Affected files: `ECloudAssistant/Codec/{AV_Common.h,Codec.pri,H264Encoder.{h,cpp},VideoEncoder.{h,cpp},D3D11SharedContext.{h,cpp},HardwareVideoEncoder.{h,cpp},SoftwareVideoEncoder.{h,cpp},VideoFrame.h}`, `ECloudAssistant/Net/{BufferWriter,TcpConnection}.cpp`, `ECloudAssistant/Pusher/{Pusher.pri,RtmpPushManager.{h,cpp},capture/ScreenCapture.h,capture/GDISreenScapture.{h,cpp},capture/WGCScreenCapture.{h,cpp}}`, `ECloudAssistant/UI/center/RemoteManager.{h,cpp}`, `context/WGC采集.md`, and this worklog.

Behavior: WGC GPU textures use the shared D3D11 device and D3D11 VideoProcessor to prepare NV12 frames for NVENC without CPU readback. If GPU initialization fails, startup falls back through WGC CPU readback plus x264 and then GDI plus x264. If the active hardware path fails during a session, the manager closes it and reopens once with software encoding; a later session again prefers the GPU path. NVENC low-delay options set `zerolatency=1` and `delay=0`; the shared D3D context lock is limited to D3D resource/view work.

Verification: the project WGC experiment record documents a 1920x1080 local A/B run: WGC+NVENC end-to-end 50-70 ms and GDI+x264 44-67 ms (59.33 ms mean), with zero GPU-to-CPU readback on the WGC hardware route. Earlier in this work session, GPU NVENC and CPU x264 publish/decode probes each completed three start/stop cycles. No new build or test was run for this upload request. Runtime failure recovery on a live two-client session remains to be confirmed.

Commit ID: this entry's containing commit (resolve with `git log -1 --format=%H -- context/WORKLOG.md`).
