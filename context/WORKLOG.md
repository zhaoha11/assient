# ECloudAssistant / ENET Worklog

## Current handoff state

- Local branch: `main`
- Latest implementation commit: `b854c07 feat: isolate control sessions and enforce single controller`
- Working-tree expectation after this record is committed: clean.
- Client build command:

  ```powershell
  $env:PATH='D:\Qt\6.10.1\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
  & 'D:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe' -f Makefile.Debug -j4
  ```

- Client build directory: `ECloudAssistant/build/Desktop_Qt_6_10_1_MinGW_64_bit-Debug`
- Client result: the full Debug build and link succeeded on 2026-08-14.
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
