# MotherVR-AVP bootstrap

This workspace currently contains the installed macOS game plus the initial reverse-engineering scaffold for the Vision Pro port plan.

## What was validated locally

- The game bundle exists at `Alien Isolation.app`
- The executable is `x86_64`
- The app is unsigned, so `DYLD_INSERT_LIBRARIES` is viable
- The binary imports the OpenGL symbols the plan depends on:
  - `glBufferSubData`
  - `glProgramUniform4fv`
  - `glViewportArrayv`
  - `glBindBuffer`
  - `glBindBufferBase`
  - `glBindBufferRange`
  - `glMapBufferRange`
  - `CGLFlushDrawable`

## Important plan adjustment

To identify the camera UBO reliably, `glBufferSubData` logging is not enough by itself. We also need buffer-binding telemetry so we can correlate:

- `glBindBuffer(GL_UNIFORM_BUFFER, buffer)`
- `glBindBufferBase(GL_UNIFORM_BUFFER, index, buffer)`
- `glBindBufferRange(GL_UNIFORM_BUFFER, index, buffer, ...)`
- `glBufferSubData(GL_UNIFORM_BUFFER, ...)`

That is why the first code artifact is a recon dylib, not a matrix-mutating mod yet.

## Recon dylib

Path: `mothervr_avp/inject`

Files:

- `src/recon_interpose.c`: interposes the most relevant OpenGL calls and logs probable matrix traffic
- `build.sh`: builds an `x86_64` dylib for Rosetta-hosted injection
- `run_recon.sh`: direct launch helper for quick experiments
- `attach_recon_lldb.sh`: waits for the Steam-launched process, then injects the recon dylib with `lldb`
- `early_probe_gl_lldb.sh`: attaches early and counts hits on key GL/debug symbols during gameplay
- `early_probe_gld_lldb.sh`: attaches early and counts hits on Apple OpenGL backend `gld*` / `gli*` symbols
- `steam_launch_recon.sh`: experimental Steam launch-option wrapper
- `install_bundle_wrapper.sh`: experimental in-app wrapper installer
- `uninstall_bundle_wrapper.sh`: restores the original binary

## Usage

```bash
cd "mothervr_avp/inject"
chmod +x build.sh run_recon.sh steam_launch_recon.sh
./build.sh
```

### Recommended: attach after Steam launch

The Feral macOS build appears to expect a real Steam launch context. Direct binary launch leaves SteamAPI partially detached, and replacing the app bundle executable appears to cause Steam to refuse launch before our wrapper runs.

The most promising path is:

```bash
cd "mothervr_avp/inject"
chmod +x build.sh attach_recon_lldb.sh
./build.sh
./attach_recon_lldb.sh
```

Then, while the script is waiting, launch Alien: Isolation normally from Steam.

The attach script:

- preserves Steam's normal launch path
- waits for the live game process
- attaches with `lldb`
- calls `dlopen()` on the recon dylib inside the running process
- writes attach details to `mothervr_avp/inject/build/attach.log`
- writes interposer activity to `mothervr_avp/inject/build/recon.log`

### Early GL probe

If the dylib loads but no hook traffic appears, use the breakpoint probe:

```bash
cd "mothervr_avp/inject"
./early_probe_gl_lldb.sh
```

Launch the game immediately after starting the script, then load into gameplay and move around for ~30 seconds. Results are written to:

```bash
mothervr_avp/inject/build/early_probe_gl.log
```

If the public `gl*` breakpoints stay cold but the `dlsym` trace shows `gld*` / `gli*` symbols, run:

```bash
cd "mothervr_avp/inject"
./early_probe_gld_lldb.sh
```

Results are written to:

```bash
mothervr_avp/inject/build/early_probe_gld.log
```

If macOS prompts for Developer Tools or debugger permissions, allow it.

### Experimental alternatives

These were explored but are currently not the recommended path:

- `steam_launch_recon.sh`
- `install_bundle_wrapper.sh`
- `uninstall_bundle_wrapper.sh`

To ensure the original game binary is restored:

```bash
./uninstall_bundle_wrapper.sh
```

### Direct launch fallback

```bash
./run_recon.sh
```

Use this only for quick experiments; the Steam-native path is preferred.

The default log file is:

```bash
mothervr_avp/inject/build/recon.log
```

## What to look for in the log

- Repeating `GL_UNIFORM_BUFFER` uploads with sizes around `64`, `128`, or `192` bytes
- Stable buffer IDs that are rebound every frame
- A consistent indexed binding that changes when the in-game camera rotates
- `glProgramUniform4fv` calls with `count` values that look like 4x4 matrix row uploads

## Best next steps

1. Run the recon dylib and capture a few minutes of play plus pause/menu transitions.
2. Correlate the candidate UBO binding with camera movement.
3. Add mutation gates so we can selectively override only the likely view matrix upload.
4. In parallel, start a separate macOS + visionOS app project for the RemoteImmersiveSpace transport path.
