# MadMaxFix

Unofficial fixes for **Mad Max (2015, Avalanche Studios / WB Games)** on PC, delivered as a single drop-in DLL.

It fixes two problems the game has had since WB shut down its online services in 2020:

1. **The Settings menu opened from the main menu is broken.** It shows only part of the options and cannot be navigated. When you leave it, the game writes zeros to `settings.ini`: subtitles and tutorials off, mouse sensitivity 0, cutscene/voice volume 0, lowest graphics and windowed mode.
2. **The Scrap Crew no longer pays out.** Without WB's server the Scrap Crew project never delivers scrap again. That makes the **"A Penny Saved"** and **"Dividend"** challenges impossible, and with them the **"Up to the Task"** achievement (complete all non-repeatable challenges).

| Game version | Executable build | Status |
|---|---|---|
| GOG | 2015-12-01 | Tested: both fixes work |
| Steam | 2015-10-22 | Tested: both fixes work |

**Download:** [GitHub Releases](https://github.com/Alexxlml/MadMaxFix/releases/latest) or [Nexus Mods](https://www.nexusmods.com/madmax/mods/215).

## Installation

1. Download the latest release (GitHub or Nexus Mods) and extract it.
2. Copy `XInput9_1_0.dll` and `MadMaxFix.ini` into the folder that contains `MadMax.exe`.
3. If you use *MadMaxCrashFix* (`dinput8.dll` / `dxgi.dll`), you can remove it: this mod already handles the display-mode list. Do not combine it with another mod that also ships an `XInput9_1_0.dll`.
4. Start the game. A `MadMaxFix.log` file is written next to the game with what the mod did.

To uninstall, delete `XInput9_1_0.dll` and `MadMaxFix.ini`.

**Back up your save before first use**, as with any mod.

## Configuration (`MadMaxFix.ini`)

```ini
[General]
Log=1              ; 1 = write MadMaxFix.log

[ModeList]
Enabled=1          ; main-menu Settings fix
MaxRows=160        ; unique rows (resolution + refresh rate) per monitor; 160 tested, 175 breaks

[ScrapCrew]
Enabled=1          ; restore Scrap Crew rewards
ScrapPerHour=100   ; scrap per hour while the game is closed, per stronghold
ScrapCap=500       ; maximum per stronghold for each payout (the game never pays more than 500)
```

The file is optional; without it the values above are used.

## How it works

### Settings menu (ModeList)

Modern high-refresh monitors report hundreds of display modes through DXGI (348 modes, or 187 unique resolution/refresh-rate rows, on a 1440p 200 Hz monitor). The pause-menu Settings screen copes with that, but the main-menu one does not: above roughly 160 unique rows it is only partly built, and the empty controls are later saved as `0`.

The mod hooks the game's mode-count and mode-getter functions and presents a smaller, consistent list. It keeps the largest resolutions with **all their real refresh rates** and drops the smallest ones until the list fits in `MaxRows`. Unlike MadMaxCrashFix, it does not force a single refresh rate.

### Scrap Crew (ScrapCrew)

The Scrap Crew reward is requested by a script node, `GetScrapCrewReward`:

- On **GOG**, the node is a stub that always fires `failed`.
- On **Steam**, the node still asks WB's server, which no longer answers, so it never reaches `success`.

Either way the reward is lost. The mod makes the node answer the way it did when the server was online. It writes a reward amount into the node's output variable and fires its `success` pin. From there the game's original script does everything else: it shows *"Scrap Crew Rewards: N"* for each stronghold, adds the scrap, and counts it toward the challenges.

The amount is based on the **real time the game was closed**. The game already measures this from the PC clock, without the server. `ScrapPerHour` sets the rate. The original server-side rate is unknown, so the default of 100 per hour means a full 500-scrap payout after about 5 hours offline. The game's own script caps every payout at 500 per stronghold.

### Safety

- Nothing is modified on disk. Everything happens in memory while the game runs.
- Every hooked function is checked byte for byte before hooking. If your executable is a different build, the mod does nothing and writes the reason to the log.
- On Steam, the game decrypts its code in memory shortly after start-up. The mod waits until the expected code is present and only then installs its hooks. The DRM itself is not touched.

## Known limitations

- **High frame rates:** above 75 FPS, opening the in-game MAX menu while running or driving can leave Max's model in the menu stuck in his running/driving pose, and above ~120 FPS the audio may crackle slightly. These are separate engine issues that this mod does not fix. Capping the game at 75 FPS (for example with RTSS or your GPU driver) avoids the menu issue.
- Only the two executable builds listed above are supported.

## Building from source

Requirements: Python 3 and Zig through pip (`pip install ziglang`). Zig is used as a C cross compiler and ships the MinGW headers, so Visual Studio is not needed.

```bash
./build/build.sh                                            # Linux, WSL or Git Bash
powershell -ExecutionPolicy Bypass -File .\build\build.ps1  # Windows PowerShell
```

The DLL is written to `out/XInput9_1_0.dll`.

### Repository layout

```
src/fix.c              the mod (XInput9_1_0.dll proxy + hooks)
config/MadMaxFix.ini   default configuration
third_party/minhook/   MinHook hooking library (BSD 2-Clause)
build/                 build scripts
```

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause).
- MadMaxCrashFix, whose refresh-rate workaround pointed at the display-mode code.

## License

MIT, see [LICENSE](LICENSE). MinHook keeps its own license (`third_party/minhook/LICENSE.txt`).

This project is not affiliated with or endorsed by Avalanche Studios, Warner Bros. Games or GOG. Mad Max is a trademark of its respective owners.
