## RESWIII - PS2Recomp fork: PlayStation 2 Static Recompiler (Experimental)

<p align="center">
<img height="356" alt="RESWIIIicon" src="https://github.com/user-attachments/assets/0a3b88a3-7c9f-490b-9333-5846161f259a" />
</p>

> [!IMPORTANT]
> WIP!!! Nothing works for now!
> EDIT: 10/04/2026 hehehe

<img width="1105" height="616" alt="image" src="https://github.com/user-attachments/assets/3c99c09b-4bc2-40ff-bb2c-874053b1e512" />


[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/JQ8mawxUEf)

Also check our [WIKI](https://github.com/ran-j/PS2Recomp/wiki)


This project statically recompiles PS2 ELF binaries into C++ and provides a runtime to execute the generated code.

### New Architecture (IR Pipeline via GhidraBridge)

The core architecture of `RESWIII-PS2recomp` has radically evolved. The legacy native C++ ELF analyzer logic has been deprecated and replaced by a powerful 4-layer pipeline that queries the Ghidra decompiler dynamically:

* **Extraction Layer (`GhidraBridge`)**: Connects directly to the Ghidra SRE (via the GhydraMCP plugin on port `8192` or `8193`) using REST APIs. It pulls strictly organized function boundaries, thunks, and disassembly blocks. To prevent extreme network latency, this layer implements a Local FileSystem Cache (`.ghidra_cache`).
* **Control Flow Analysis (`JumpResolver`)**: Intelligently scans MIPS R5900 branches and jumps (`jr ra`, switch-cases, dynamic jump tables) to statically resolve abstract control flow ahead-of-time.
* **Lifting Layer (`IRLifter`)**: Abandons 1:1 C++ translation by elevating the raw MIPS R5900 asm into a highly abstract Intermediate Representation (IR). This unlocks dataflow optimization, loop unrolling, and future hardware detachment.
* **Code Generation (`CppEmitter`)**: Compiles the mapped IR down to clean, readable, and highly optimized modern C++ targeting the underlying PS2 Context struct.
* **Runtime (`ps2xRuntime`)**: The beating heart of the execution. Features guest-virtual memory models, a `JumpResolver`-compliant dispatch system, dynamic syscall interceptors, and hardware bindings (MMI/VU).

*Note: The historical `ps2xAnalyzer` tool is now kept purely as a fallback for ELFs loaded outside the Ghidra ecosystem.*

### Requirements

* CMake 3.20+
* C++20 compiler (currently tested mainly with MSVC)
* SSE4/AVX host support for some vector paths
* **Ghidra SRE** with the **GhydraMCP** plugin running on port `8192` or `8193`.

### Build

```bash
git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp

mkdir build_clang
cd build_clang
cmake -G "Visual Studio 17 2022" -T ClangCL ..
cmake --build . --target ps2_recomp --config Release
```

### Usage

**The new Live-Ghidra Workflow (Preferred):**

1. Keep your PS2 ELF open in **Ghidra**.
2. Start the GhydraMCP Server (ensure the GUI server is green and listening).
3. Directly run the recompiler using the `--use-ir` flag (no manual Java exports required!).

```bash
./ps2_recomp config.toml --use-ir
```

*Note: The old mandate of running the Java script `ExportPS2Functions.java` inside Ghidra is now entirely obsolete! The `GhidraBridge` fetches functions and disassembled instructions automatically upon execution.*

### Configuration

Main fields in `config.toml`:

* `general.input`: source ELF path.
* `general.output`: generated C++ output folder.
* `general.single_file_output`: one combined cpp or one file per function.
* `general.patch_syscalls`: apply configured patches to `SYSCALL` instructions (`false` recommended).
* `general.patch_cop0`: apply configured patches to COP0 instructions.
* `general.patch_cache`: apply configured patches to CACHE instructions.
* `general.stubs`: names to force as stubs. Also accepts `handler@0xADDRESS` to bind a stripped function address directly to a runtime syscall/stub handler. Includes generic handlers `ret0`, `ret1`, `reta0`.
* `general.skip`: names to force as skipped wrappers.
* `patches.instructions`: raw instruction replacements by address.

### Runtime

To execute the recompiled code.

`ps2xRuntime` currently provides:

* Guest memory model and function dispatch table.
* Some syscall dispatcher with common kernel IDs.
* Basic GS/VU/file/system stubs.
* Foundation to expand and port your game.

### Game Override Hooks

Game overrides are runtime-side, build-scoped patch modules.
 
A game override is C++ code that runs during `loadELF` and can replace function bindings by address for one specific game build. This is separate from recompilation output and separate from global runtime stubs/syscalls. 
 
API:

* Header: `ps2xRuntime/include/game_overrides.h`
* Register macro: `PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)`
* Direct bind helper: `ps2_game_overrides::bindAddressHandler(runtime, addr, "handler")`

### Limitations

* Graphics Synthesizer and other hardware components need external implementation
* VU1 microcode is not complete.
* Hardware emulation is partial and many paths are stubbed.

###  Acknowledgments

* Inspired by N64Recomp
* Uses ELFIO for ELF parsing
* Uses toml11 for TOML parsing
* Uses fmt for string formatting
