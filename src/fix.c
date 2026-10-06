// MadMaxFix - unofficial fixes for Mad Max (2015, Avalanche Studios / WB Games) on PC.
// Supported builds: GOG 2015-12-01 and Steam 2015-10-22.
//
// 1) ModeList: the Settings menu opened from the main menu (no save loaded) is broken.
//    It shows only part of the options, cannot be navigated, and when you leave it the game
//    writes zeros to settings.ini (subtitles off, mouse sensitivity 0, cutscene volume 0,
//    lowest graphics, windowed mode...).
//    Cause: the main-menu Settings screen cannot handle a large display-mode matrix
//    (resolution + refresh rate). Modern high-refresh monitors report hundreds of modes
//    through DXGI (348 modes -> 187 unique rows on a 1440p @ 200 Hz monitor). Above roughly
//    160 unique rows the menu is only half built, and the empty controls are saved as 0.
//    Fix: the game is shown a smaller, consistent mode list (count + getter hooks). The
//    largest resolutions are kept with all their real refresh rates, up to MaxRows unique
//    rows per monitor.
//
// 2) ScrapCrew: the Scrap Crew project stopped paying out when WB shut down its online
//    servers (2020), which makes the "A Penny Saved" and "Dividend" challenges (and the
//    "Up to the Task" achievement) impossible. See the SCRAP CREW section below.
//
// Nothing is changed on disk. Every hooked function is verified byte by byte first; if the
// build does not match, the mod does nothing and says so in MadMaxFix.log.
// On Steam the game's code is decrypted in memory a moment after start-up, so the hooks are
// installed from a thread once the signatures match. The DRM is not touched.
//
// Install: copy XInput9_1_0.dll (and optionally MadMaxFix.ini) next to MadMax.exe.
// MadMaxCrashFix (dinput8.dll / dxgi.dll) is not needed with this mod.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include "minhook/include/MinHook.h"

#define FIX_VERSION "1.2.0"

// ---------------------------------------------------------------------------
// Supported builds (no ASLR, ImageBase 0x140000000)
// ---------------------------------------------------------------------------
typedef struct {
    const char* name;
    uint64_t getCount, getMode;   const uint8_t *sigCount, *sigMode;
    uint64_t scrap;               const uint8_t *sigScrap;
    uint64_t timeOff;             // GetTimeOffline script node: its calls are used to verify the helpers
    int offFindVar, offMgr, offGetData, offIsInt, offStorage, offFirePin;  // offsets of the E8 calls / mov rdx,[rip]
    uint64_t findVar, mgrPtr, getData, isInt, storage, firePin;
} Build;

#define SIGLEN 16
static const uint8_t GOG_COUNT[SIGLEN]   = {0x48,0x8B,0xC1,0x83,0xFA,0xFF,0x75,0x03,0x8B,0x51,0x38,0x48,0x8B,0x40,0x50,0x8B};
static const uint8_t GOG_MODE[SIGLEN]    = {0x8B,0xC2,0x4C,0x8B,0xD1,0x41,0x83,0xF8,0xFF,0x75,0x04,0x44,0x8B,0x41,0x38,0x41};
static const uint8_t GOG_SCRAP[SIGLEN]   = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0xCA};
static const uint8_t STEAM_COUNT[SIGLEN] = {0x48,0x89,0xC8,0x83,0xFA,0xFF,0x75,0x03,0x8B,0x51,0x38,0x48,0x8B,0x40,0x50,0x89};
static const uint8_t STEAM_MODE[SIGLEN]  = {0x89,0xD0,0x49,0x89,0xCA,0x41,0x83,0xF8,0xFF,0x75,0x04,0x44,0x8B,0x41,0x38,0x44};
static const uint8_t STEAM_SCRAP[SIGLEN] = {0x53,0x55,0x57,0x48,0x83,0xEC,0x20,0x48,0x89,0xCB,0x48,0x89,0xD1,0x48,0x89,0xD7};

static const Build g_builds[] = {
    { "GOG 2015-12-01",
      0x140F17DF0ULL, 0x140F17E10ULL, GOG_COUNT, GOG_MODE,
      0x1402D1E90ULL, GOG_SCRAP,
      0x1402D38D0ULL, 0x28, 0x35, 0x56, 0x61, 0x70, 0x83,
      0x140F79AC0ULL, 0x141715B48ULL, 0x140F78C40ULL, 0x140F74CE0ULL, 0x140F74BA0ULL, 0x140F79560ULL },
    { "Steam 2015-10-22",
      0x142F5C3F0ULL, 0x142F5C420ULL, STEAM_COUNT, STEAM_MODE,
      0x141EA2930ULL, STEAM_SCRAP,
      0x141EA2C00ULL, 0x2F, 0x3C, 0x5D, 0x68, 0x77, 0x91,
      0x14300B8D0ULL, 0x1417F4ED0ULL, 0x14300A8C0ULL, 0x143005F70ULL, 0x143005DB0ULL, 0x14300B330ULL },
};
static const Build* B;   // detected build
#define STEAM_TIMESTAMP 0x5628A448u

// Safe reads (on Steam the code may not be decrypted or readable yet).
static int ReadMem(uint64_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, n, &got) && got == n;
}
static int SigAt(uint64_t addr, const uint8_t* sig) {
    uint8_t b[SIGLEN];
    return ReadMem(addr, b, SIGLEN) && memcmp(b, sig, SIGLEN) == 0;
}
static uint64_t CallTarget(uint64_t at) {     // E8 rel32
    uint8_t b[5];
    if (!ReadMem(at, b, 5) || b[0] != 0xE8) return 0;
    return at + 5 + (int64_t)*(int32_t*)(b + 1);
}
static uint64_t RipMovTarget(uint64_t at) {   // 48 8B 15 disp32 (mov rdx,[rip+disp])
    uint8_t b[7];
    if (!ReadMem(at, b, 7) || b[0] != 0x48 || b[1] != 0x8B || b[2] != 0x15) return 0;
    return at + 7 + (int64_t)*(int32_t*)(b + 3);
}

// ---------------------------------------------------------------------------
// Configuration and log
// ---------------------------------------------------------------------------
static int g_enabled = 1;      // [ModeList] Enabled
static int g_maxRows = 160;    // [ModeList] MaxRows  (tested: 160 works, 175 breaks the menu)
static int g_log     = 1;      // [General]  Log

static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static char   g_dir[MAX_PATH];
static char   g_ini[MAX_PATH];

static void Log(const char* fmt, ...) {
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 3) n = sizeof(buf) - 3;
    buf[n++] = '\r'; buf[n++] = '\n';
    DWORD w; WriteFile(g_logFile, buf, n, &w, NULL);
}

static void Init(void) {
    DWORD len = GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    while (len > 0 && g_dir[len - 1] != '\\' && g_dir[len - 1] != '/') len--;
    g_dir[len] = 0;

    snprintf(g_ini, sizeof(g_ini), "%sMadMaxFix.ini", g_dir);
    g_enabled = GetPrivateProfileIntA("ModeList", "Enabled", 1, g_ini);
    g_maxRows = GetPrivateProfileIntA("ModeList", "MaxRows", 160, g_ini);
    g_log     = GetPrivateProfileIntA("General", "Log", 1, g_ini);
    if (g_maxRows < 8) g_maxRows = 8;
    if (g_maxRows > 1024) g_maxRows = 1024;

    if (g_log) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%sMadMaxFix.log", g_dir);
        g_logFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    }
    Log("MadMaxFix v%s - ModeList.Enabled=%d MaxRows=%d", FIX_VERSION, g_enabled, g_maxRows);
}

// ===========================================================================
// MODE LIST
// Display object layout (from GetModeCount / GetMode):
//   this+0x38 : current output (monitor)
//   this+0x50 : array of { uint32 count; pad; DXGI_MODE_DESC* modes; } per output (16 bytes each)
//   DXGI_MODE_DESC (0x1C bytes): Width, Height, RefreshRate.Numerator, RefreshRate.Denominator, ...
// The game counts unique rows of (width, height, Hz = numerator / denominator as an integer).
// ===========================================================================
#define MAX_OUT   8
#define MAX_MODES 1024
typedef struct { void* modes; uint32_t raw; uint32_t n; uint16_t map[MAX_MODES]; } FilterCache;
static FilterCache g_fc[MAX_OUT];
static CRITICAL_SECTION g_lock;

typedef uint32_t (*GetCountFn)(void* self, int output);
typedef void     (*GetModeFn)(void* self, int idx, int output, void* out);
static GetCountFn o_GetCount;
static GetModeFn  o_GetMode;

static FilterCache* GetFilter(void* self, int output) {
    int outIdx = (output == -1) ? *(int*)((uint8_t*)self + 0x38) : output;
    if (outIdx < 0 || outIdx >= MAX_OUT) return NULL;
    uint8_t* table = *(uint8_t**)((uint8_t*)self + 0x50);
    if (!table) return NULL;
    uint32_t raw   = *(uint32_t*)(table + (size_t)outIdx * 16);
    uint8_t* modes = *(uint8_t**)(table + (size_t)outIdx * 16 + 8);
    if (!modes || raw == 0) return NULL;

    FilterCache* fc = &g_fc[outIdx];
    if (fc->modes == modes && fc->raw == raw) return fc;

    EnterCriticalSection(&g_lock);
    if (!(fc->modes == modes && fc->raw == raw)) {
        static uint32_t rw[MAX_MODES], rh[MAX_MODES], rz[MAX_MODES];
        static uint16_t keep[MAX_MODES];
        uint32_t lim = raw < MAX_MODES ? raw : MAX_MODES;
        uint32_t rows = 0, nk = 0;
        // From the largest mode down: keep whole resolutions while they fit.
        for (int i = (int)lim - 1; i >= 0; i--) {
            const uint32_t* m = (const uint32_t*)(modes + (size_t)i * 0x1C);
            uint32_t hz = m[3] ? m[2] / m[3] : 0;
            int found = 0;
            for (uint32_t k = 0; k < rows && !found; k++)
                found = rw[k] == m[0] && rh[k] == m[1] && rz[k] == hz;
            if (!found) {
                if ((int)rows >= g_maxRows) continue;
                rw[rows] = m[0]; rh[rows] = m[1]; rz[rows] = hz; rows++;
            }
            keep[nk++] = (uint16_t)i;
        }
        for (uint32_t k = 0; k < nk; k++) fc->map[k] = keep[nk - 1 - k];  // ascending order
        fc->n = nk;
        MemoryBarrier();     // map and n are written before the cache is published
        fc->raw = raw;
        fc->modes = modes;   // published last
        Log("Monitor %d: %u DXGI modes -> %u modes / %u rows shown to the game", outIdx, raw, nk, rows);
    }
    LeaveCriticalSection(&g_lock);
    return fc;
}

static uint32_t hk_GetCount(void* self, int output) {
    FilterCache* fc = GetFilter(self, output);
    return fc ? fc->n : o_GetCount(self, output);
}

static void hk_GetMode(void* self, int idx, int output, void* out) {
    FilterCache* fc = GetFilter(self, output);
    if (fc && idx >= 0 && (uint32_t)idx < fc->n) idx = fc->map[idx];
    o_GetMode(self, idx, output, out);
}

static void InstallModeList(void) {
    if (!g_enabled) { Log("ModeList disabled in MadMaxFix.ini."); return; }
    if (!SigAt(B->getCount, B->sigCount) || !SigAt(B->getMode, B->sigMode)) {
        Log("ModeList: display-mode functions do not match build %s; nothing changed.", B->name);
        return;
    }
    if (MH_CreateHook((LPVOID)B->getCount, (LPVOID)hk_GetCount, (LPVOID*)&o_GetCount) != MH_OK ||
        MH_CreateHook((LPVOID)B->getMode,  (LPVOID)hk_GetMode,  (LPVOID*)&o_GetMode)  != MH_OK ||
        MH_EnableHook((LPVOID)B->getCount) != MH_OK || MH_EnableHook((LPVOID)B->getMode) != MH_OK) {
        Log("ModeList: ERROR installing hooks.");
        return;
    }
    Log("ModeList fix active.");
}

// ===========================================================================
// SCRAP CREW
// GOG: the GetScrapCrewReward script node (0x1402D1E90) always fires "failed", because the reward
// used to be computed by WB's server. Steam (older build, 0x141EA2930): the node still asks the
// server, which no longer exists, so it never reaches "success". Either way the reward is lost.
// Here the node answers the way it did with the server: it writes the amount to its connected
// variable (hash F9714AD3) and fires the "success" pin. The game's own script does the rest:
// it shows "Scrap Crew Rewards: N", adds the scrap and counts it for the "A Penny Saved" and
// "Dividend" challenges.
// The script divides the value by 10 and gives it to each stronghold with the project
// (5000 -> 500 each). It also caps the payout at 500 per stronghold.
// The offline minutes are measured by the game itself from the PC clock (manager + 0xE08).
// ===========================================================================
typedef void*    (*FindVarFn)(void* node, void* ctx, uint32_t hash);
typedef uint8_t* (*GetDataFn)(void* node);
typedef uint8_t  (*IsIntFn)(void* var);
typedef void*    (*StorageFn)(void* data, void* var);
typedef void     (*FirePinFn)(void* node, void* ctx, uint32_t hash);
#define FindVar  ((FindVarFn)B->findVar)
#define GetData  ((GetDataFn)B->getData)
#define IsIntVar ((IsIntFn)B->isInt)
#define Storage  ((StorageFn)B->storage)
#define FirePin  ((FirePinFn)B->firePin)
#define H_IN            0xC94BA74Au   // "in"
#define H_SUCCESS       0x1483175Eu   // "success"
#define H_REWARD_AMOUNT 0xF9714AD3u   // connected variable that holds the amount

static int g_scrapEnabled = 1;    // [ScrapCrew] Enabled
static int g_scrapPerHour = 100;  // [ScrapCrew] ScrapPerHour (per stronghold)
static int g_scrapCap     = 500;  // [ScrapCrew] ScrapCap     (per stronghold; the game never pays more than 500)

typedef uint64_t (*ScriptFn)(void*, void*, uint64_t, uint64_t);
static ScriptFn o_ScrapRew;

static uint64_t hk_ScrapRew(void* self, void* ctx, uint64_t msg, uint64_t r9) {
    if ((uint32_t)msg != H_IN) return o_ScrapRew(self, ctx, msg, r9);
    uint8_t* mgr = *(uint8_t**)B->mgrPtr;
    int minutes = mgr ? *(int*)(mgr + 0xE08) : 0;
    if (minutes < 0) minutes = 0;
    long long per = (long long)minutes * g_scrapPerHour / 60;
    if (per > g_scrapCap) per = g_scrapCap;
    if (per <= 0) {
        Log("Scrap Crew: %d minutes offline, no reward yet.", minutes);
        return o_ScrapRew(self, ctx, msg, r9);
    }
    void* var = FindVar(self, ctx, H_REWARD_AMOUNT);
    if (!var || !IsIntVar(var)) {
        Log("Scrap Crew: reward variable not found; original behavior kept.");
        return o_ScrapRew(self, ctx, msg, r9);
    }
    *(int*)Storage(GetData(self), var) = (int)(per * 10);
    FirePin(self, ctx, H_SUCCESS);
    Log("Scrap Crew: %d minutes offline -> %lld scrap per stronghold.", minutes, per);
    return 1;
}

static void InstallScrapCrew(void) {
    g_scrapEnabled = GetPrivateProfileIntA("ScrapCrew", "Enabled", 1, g_ini);
    g_scrapPerHour = GetPrivateProfileIntA("ScrapCrew", "ScrapPerHour", 100, g_ini);
    g_scrapCap     = GetPrivateProfileIntA("ScrapCrew", "ScrapCap", 500, g_ini);
    if (g_scrapPerHour < 0) g_scrapPerHour = 0;
    if (g_scrapCap < 0) g_scrapCap = 0;
    if (g_scrapCap > 500) g_scrapCap = 500;   // the game's script caps each payout at 500 anyway
    Log("ScrapCrew.Enabled=%d ScrapPerHour=%d ScrapCap=%d", g_scrapEnabled, g_scrapPerHour, g_scrapCap);
    if (!g_scrapEnabled) return;
    if (!SigAt(B->scrap, B->sigScrap)) {
        Log("Scrap Crew: script node does not match build %s; nothing changed.", B->name);
        return;
    }
    // Verify the script helpers by reading the calls made by GetTimeOffline.
    uint64_t t = B->timeOff;
    if (CallTarget(t + B->offFindVar) != B->findVar || RipMovTarget(t + B->offMgr) != B->mgrPtr ||
        CallTarget(t + B->offGetData) != B->getData || CallTarget(t + B->offIsInt) != B->isInt ||
        CallTarget(t + B->offStorage) != B->storage || CallTarget(t + B->offFirePin) != B->firePin) {
        Log("Scrap Crew: script helpers do not match build %s; nothing changed.", B->name);
        return;
    }
    if (MH_CreateHook((LPVOID)B->scrap, (LPVOID)hk_ScrapRew, (LPVOID*)&o_ScrapRew) != MH_OK ||
        MH_EnableHook((LPVOID)B->scrap) != MH_OK) {
        Log("Scrap Crew: ERROR installing hook.");
        return;
    }
    Log("Scrap Crew fix active.");
}

// ---------------------------------------------------------------------------
// Build detection. GOG is ready at load time; Steam decrypts its code a few seconds later.
// ---------------------------------------------------------------------------
static DWORD WINAPI InstallThread(LPVOID p) {
    (void)p;
    MH_Initialize();
    B = NULL;
    if (SigAt(g_builds[0].getCount, g_builds[0].sigCount)) B = &g_builds[0];
    else {
        uint8_t* base = (uint8_t*)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        if (nt->FileHeader.TimeDateStamp != STEAM_TIMESTAMP) {
            Log("Unsupported game build (timestamp 0x%08X); nothing changed.", (unsigned)nt->FileHeader.TimeDateStamp);
            return 0;
        }
        Log("Steam build detected; waiting for the game code to be ready...");
        const Build* s = &g_builds[1];
        DWORD t0 = GetTickCount();
        while (!(SigAt(s->getCount, s->sigCount) && SigAt(s->scrap, s->sigScrap))) {
            if (GetTickCount() - t0 > 120000) {
                Log("Steam: code did not match after 120 s; nothing changed.");
                return 0;
            }
            Sleep(250);
        }
        Log("Steam: code ready after %.1f s.", (GetTickCount() - t0) / 1000.0);
        B = s;
    }
    Log("Build: %s", B->name);
    InstallModeList();
    InstallScrapCrew();
    return 0;
}

// ---------------------------------------------------------------------------
// XInput9_1_0.dll proxy (the game only imports these two functions)
// ---------------------------------------------------------------------------
typedef DWORD (WINAPI *XInputFn)(DWORD, void*);
static HMODULE  g_realXInput;
static XInputFn r_GetState, r_SetState;

static void LoadRealXInput(void) {
    if (g_realXInput) return;
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    snprintf(path + n, MAX_PATH - n, "\\XInput9_1_0.dll");
    g_realXInput = LoadLibraryA(path);
    if (g_realXInput) {
        r_GetState = (XInputFn)GetProcAddress(g_realXInput, "XInputGetState");
        r_SetState = (XInputFn)GetProcAddress(g_realXInput, "XInputSetState");
    }
}

__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD idx, void* state) {
    LoadRealXInput();
    return r_GetState ? r_GetState(idx, state) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD idx, void* vib) {
    LoadRealXInput();
    return r_SetState ? r_SetState(idx, vib) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        Init();
        // GOG: install right away, before the game starts. Steam: from a thread that waits for the code.
        if (SigAt(g_builds[0].getCount, g_builds[0].sigCount)) InstallThread(NULL);
        else { HANDLE h = CreateThread(NULL, 0, InstallThread, NULL, 0, NULL); if (h) CloseHandle(h); }
    }
    return TRUE;
}
