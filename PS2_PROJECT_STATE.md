# PS2Recomp Project State

## Critical Paths (NEVER FORGET)

| What | Path |
|---|---|
| **ELF file** | `E:\Programmi VARI\PROGETTI\RESWIII\ISO extracted\SLES_531.55` |
| **ISO extracted root** | `E:\Programmi VARI\PROGETTI\RESWIII\ISO extracted\` |
| **Runner exe** | `build_clang\ps2xRuntime\Release\ps2EntryRunner.exe` |
| **Build dir** | `build_clang` (NOT `build64`) |
| **Config** | `test_config.toml` |
| **Game overrides** | `ps2xRuntime\src\runner\starwars_sif_overrides.cpp` |
| **Log output** | `logs\agent_run_latest.log` |
| **Working dir** | `E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp` |

## How to Run

```powershell
# Build
cmake --build build_clang --config Release -j12

# Run (18 second test)
$proc = Start-Process -FilePath "build_clang\ps2xRuntime\Release\ps2EntryRunner.exe" `
  -ArgumentList '"E:\Programmi VARI\PROGETTI\RESWIII\ISO extracted\SLES_531.55"' `
  -WorkingDirectory "E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp" `
  -RedirectStandardOutput "logs\agent_run_latest.log" `
  -RedirectStandardError "logs\agent_run_latest.err" `
  -PassThru
Start-Sleep -Seconds 18
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
```

## Current Fix Applied (not yet tested)
- `hookCdvdPollStatus` changed from returning `1` (error) to `3` (success)
- This should fix the `[dispatch:pc-zero]` crash at `0x250050`

## Issue History
| Date | Issue | Fix | Result |
|---|---|---|---|
| 2026-04-08 | pc-zero crash from 0x250050 after PS2PAK.HSH load | hookCdvdPollStatus return 3 instead of 1 | PENDING TEST |
