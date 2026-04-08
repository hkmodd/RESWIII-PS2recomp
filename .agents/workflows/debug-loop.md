---
description: Structured debug loop for PS2Recomp runtime — build, run, analyze, cross-reference, fix, rebuild, retest
---

# PS2Recomp Debug Loop — `/debug-loop`

> **GOAL:** Systematic, repeatable cycle to advance the runtime from current state toward rendering.
> Each iteration has clear ENTRY, ANALYSIS, REASONING, ACTION, and VERIFICATION phases.
> Never skip the reasoning step. Never fix without understanding the root cause.

---

## Pre-Flight (once per session)

// turbo
1. **Load project state.** Read `PS2_PROJECT_STATE.md` in repo root (if exists).

// turbo
2. **Verify build environment.** Run: `cmd.exe /c "where cl"` — must return a path. If not, tell user to open x64 Native Tools Command Prompt for VS.

// turbo
3. **Verify runner exists.** Run: `Test-Path "E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp\build64\Release\ps2xRunner.exe"` — must be True.

4. **Load the skill.** Read the PS2Recomp Agent SKILL for hardware reference:
   - SKILL path: `E:\Programmi VARI\PROGETTI\antigravity-awesome-skills\skills\ps2-recomp-Agent-SKILL\SKILL.md`
   - Key resources (load ON-DEMAND only):
     - `resources/04-runtime-syscalls-stubs.md` — syscall table
     - `resources/07-ps2-code-patterns.md` — DMA/VIF/GS patterns
     - `resources/09-ps2tek.md` — 230KB PS2 hardware holy grail
     - `resources/db-sdk-functions.md` — SDK function reference
     - `resources/12-pcsx2-mcp-playbook.md` — PCSX2 A/B comparison

5. **Connect MCP tools** (if needed for this iteration):
   - Ghidra: `mcp_ghydra_instances_list()` → `mcp_ghydra_instances_use(port)`
   - PCSX2: `mcp_pcsx2_pcsx2_connect()` (only if running PCSX2 for A/B comparison)

---

## Phase 1: RUN — Execute and Capture

// turbo
6. **Run the game runner.** Launch in background with 15-second timeout:
   ```
   cmd.exe /c "cd /d E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp && build64\Release\ps2xRunner.exe --config starwars.toml 2>&1"
   ```
   Set `WaitMsBeforeAsync=500` to capture early crashes. Let it run for ~15 seconds, then kill.

// turbo
7. **Capture output.** Use `command_status` with `OutputCharacterCount=8000` to read stdout/stderr.
   - Save mentally: first error, last 50 lines, any `[UNHANDLED]` or `[FATAL]` or `Error` lines.
   - **DO NOT** paste the entire log back — summarize key lines only.

---

## Phase 2: ANALYZE — Understand What Happened

8. **Classify the output.** Answer these questions (write answers before proceeding):

   **Q1: Did it crash or hang?**
   - CRASH = segfault, access violation, bad PC, abort → go to Phase 3A
   - HANG = stuck in loop, no output for 10+ seconds → go to Phase 3B
   - PROGRESS = new output we haven't seen before → go to Phase 3C

   **Q2: What is the FIRST error?** (ignore everything after the first error — cascading failures are noise)

   **Q3: Which layer is the error in?**
   - `[Syscall]` / `[WaitSema]` / `[CreateThread]` → Kernel HLE layer (`syscalls/*.inl`)
   - `[SifCallRpc]` / `[SifBindRpc]` / `[CDVD]` → SIF/IOP HLE layer (`ps2_syscalls_rpc.inl`)
   - `[GS]` / `[VIF]` / `[DMA]` → Graphics pipeline stubs (`ps2_stubs_gs.inl`)
   - Game function name / `FUN_00xxxxxx` → Recompiled game code → needs override or VFS data
   - `Error Opening` / file path → VFS/File I/O incomplete → `starwars_sif_overrides.cpp`

---

## Phase 3: CROSS-REFERENCE — Look Before You Leap

### 3A: For CRASHES

9. **Get crash address.** Extract the PC or address from the error.

10. **Decompile in Ghidra.** `mcp_ghydra_functions_decompile(address="0xXXXXXX")` — understand what the game was trying to do.

11. **Check xrefs.** `mcp_ghydra_xrefs_list(to_addr="0xXXXXXX")` — who calls this? Is it a vtable dispatch?

12. **Compare with PCSX2** (if available). The same game running in PCSX2 is truth:
    - `mcp_pcsx2_pcsx2_set_breakpoint(address="0xXXXXXX")` → `mcp_pcsx2_pcsx2_continue()`
    - When hit: `mcp_pcsx2_pcsx2_read_registers()` — compare register state
    - `mcp_pcsx2_pcsx2_read_memory(address="0xXXXXXX", length=64)` — compare memory content

### 3B: For HANGS

13. **Identify the loop.** Look for repeated log lines. What syscall is being called in a loop?

14. **Check if it's a poll loop.** Common: `PollSema`, `WaitEventFlag`, `sceSifCheckStatRpc`.
    - If polling for IOP/hardware state → needs HLE bypass
    - If polling for data → data source (VFS/CDVD) not providing data

15. **Decompile the polling function** in Ghidra to understand the exit condition.

### 3C: For PROGRESS

16. **Identify what's new.** What output is the game producing that we haven't seen before?

17. **Is it requesting something we don't provide?** (file open, RPC call to unknown SID, etc.)

18. **Trace forward.** What should happen NEXT after this point? Decompile the caller to see what it expects.

---

## Phase 4: REASON — Think Before You Code

> **MANDATORY.** Write your reasoning BEFORE writing any code. Format:

```
DIAGNOSIS:
  - Layer: [kernel/sif/gs/game/vfs]
  - Root cause: [what is actually wrong]
  - Evidence: [specific log line or Ghidra/PCSX2 data]

FIX APPROACH:
  - Tool: [toml_stub | runtime_cpp | game_override | vfs_handler]
  - File to edit: [exact path]
  - What to add/change: [specific description]
  - Risk: [low/medium/high — what could this break?]

EXPECTED RESULT:
  - After this fix, the game should: [specific observable change]
  - New log output expected: [what we should see]
```

19. **Apply the Fix Taxonomy** (from SKILL §B.5):
    - **TOML** → stub/skip/nop/patch → only if the function should be completely bypassed
    - **Runtime C++** → PS2 hardware HLE → `src/lib/*.inl` or `src/lib/*.cpp`
    - **Game Override** → replace broken recompiled function → `starwars_sif_overrides.cpp`
    - **VFS Handler** → file I/O → CDVD SID 0x40 handler in `ps2_syscalls_rpc.inl`

20. **Consult PS2 hardware docs** if the fix involves hardware behavior:
    - Load relevant resource from the SKILL: `resources/09-ps2tek.md` (search for specific topic)
    - Or use `resources/db-*.md` index files for specific lookups

---

## Phase 5: IMPLEMENT — Write the Fix

21. **Rules before writing code:**
    - ❌ NEVER edit `runner/*.cpp` (auto-generated)
    - ❌ NEVER edit `.h` headers (triggers 30K file rebuild)
    - ❌ NEVER clean the build
    - ✅ Edit `src/lib/syscalls/*.inl` for syscall fixes
    - ✅ Edit `src/lib/stubs/*.inl` for stub fixes
    - ✅ Edit `src/runner/starwars_sif_overrides.cpp` for game-specific overrides
    - ✅ Edit `src/lib/ps2_runtime.cpp` for runtime infrastructure

22. **Write the minimal fix.** Smallest possible change. Add diagnostic logging so we can verify.

23. **Add a comment** explaining WHY, not WHAT. Future sessions need to understand the reasoning.

---

## Phase 6: BUILD — Incremental Only

// turbo
24. **Build.** Run:
    ```
    cmd.exe /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cd /d E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp && cmake --build build64 --config Release 2>&1"
    ```
    - **VERIFY:** exit code 0, no errors
    - If build fails → fix compilation error → rebuild (DO NOT clean)

---

## Phase 7: VERIFY — Did It Work?

25. **Run again** (repeat Phase 1).

26. **Compare output** with previous run:
    - Did the FIRST error change? → Progress!
    - Same error? → Diagnosis was wrong → go back to Phase 4, reason differently
    - New error? → Previous fix worked, new gate found → start new iteration from Phase 2

27. **Update state.** Record what you learned in this iteration:
    - What was the problem?
    - What fixed it?
    - What's the new blocker?

---

## Anti-Patterns — NEVER Do These

| Anti-Pattern | Why It Kills Progress |
|---|---|
| Fix without understanding | Creates cascading bugs you can't trace back |
| Skip Ghidra cross-reference | You'll guess wrong about what the function does |
| Edit multiple files at once | Can't isolate which change worked/broke things |
| Ignore the FIRST error | Everything after the first error is noise |
| Fix the symptom not the cause | The same class of bug will come back 10 times |
| Assume you remember | After 15+ tool calls, re-read the state and rules |
| Paste entire logs | Context overflow → you forget your own rules |

---

## Quick Reference: Key Files

| What | Where |
|---|---|
| SIF RPC / CDVD handler | `ps2xRuntime/src/lib/syscalls/ps2_syscalls_rpc.inl` |
| Semaphore / EventFlag | `ps2xRuntime/src/lib/syscalls/ps2_syscalls_flags.inl` |
| Thread management | `ps2xRuntime/src/lib/syscalls/ps2_syscalls_thread.inl` |
| Interrupt / VBlank | `ps2xRuntime/src/lib/syscalls/ps2_syscalls_interrupt.inl` |
| System (heap, memory) | `ps2xRuntime/src/lib/syscalls/ps2_syscalls_system.inl` |
| GS stubs | `ps2xRuntime/src/lib/stubs/ps2_stubs_gs.inl` |
| Misc stubs (SIF regs) | `ps2xRuntime/src/lib/stubs/ps2_stubs_misc.inl` |
| Game-specific overrides | `ps2xRuntime/src/runner/starwars_sif_overrides.cpp` |
| Runtime infrastructure | `ps2xRuntime/src/lib/ps2_runtime.cpp` |
| Syscall dispatch | `ps2xRuntime/src/lib/ps2_syscalls.cpp` |
| PS2 hardware bible | SKILL `resources/09-ps2tek.md` |
| Build dir | `build64` (Release config) |
| Runner exe | `build64/Release/ps2xRunner.exe` |
| Game config | `starwars.toml` |
