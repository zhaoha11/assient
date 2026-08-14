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
