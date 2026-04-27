# AGENTS.md

## Project summary

- This repository is a **Windows MFC SDI desktop app** for triaxial test machine control.
- Toolchain assumptions are **Visual Studio 2022 + MFC**, **x64 only**, and **MBCS** (not Unicode).
- AD/DA communication is implemented through **Modbus RTU** (`ModbusRTU`), replacing legacy CAIO dependency.

## Build / test / lint

### Build

From repository root:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "DigitShowBasicM.sln" /p:Configuration=Debug /p:Platform=x64 /nologo
```

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "DigitShowBasicM.sln" /p:Configuration=Release /p:Platform=x64 /nologo
```

### Test

- No automated test project is currently configured in this repository.
- No single-test command exists.

### Lint / static checks

- No dedicated lint command is configured.

## High-level architecture

### MFC structure

- `DigitShowBasic.cpp`: `CWinApp` entrypoint and SDI app initialization.
- `MainFrm.cpp`: main window and menu command routing for control dialogs.
- `DigitShowBasicView.cpp`: form view UI + timer-driven runtime loop.
- `DigitShowBasicDoc.cpp`: hardware I/O, calibration math, control algorithms, data saving.

### Global runtime state and hardware singletons

- `DigitShowContext` (`DigitShowContext.h/.cpp`) is a lazily initialized global singleton via `GetContext()`.
- `ModbusRTU` (`ModbusRTU.h/.cpp`) is a process-wide singleton via `GetModbusInstance()`.

### Timer-driven execution model (critical)

- **Timer 1 (100ms)** in `CDigitShowBasicView::OnTimer`:
  1. `AD_INPUT()` (Modbus AI read, 16ch)
  2. `DA_OUTPUT()` (Modbus AO write, 8ch, changed values only)
  3. `Cal_Physical()` / `Cal_Param()` / `ShowData()`
- **Timer 2**: control feedback loop (`Control_DA()`), updates `ctx->DAVout`.
- **Timer 3**: periodic file save using cached values (`SaveToFile()`), no direct Modbus I/O.

### Control mode dispatch

- `ctx->ControlID` selects control logic in `CDigitShowBasicDoc::Control_DA()`.
- IDs 1..7 are direct algorithms (pre-consolidation, consolidation, monotonic/cyclic/linear path variants).
- ID 15 executes step scripts from `ctx->controlFile` (`.ctl`) using `controlFile.Num[]` to dispatch sub-modes.

## ⚠️ ABSOLUTE RULE — Resource file character encoding

**RC/RC2 ファイルを編集・新規作成する際は、以下を必ず守ること。違反はビルドエラーまたはランタイムの文字化けを引き起こす。**

| 項目 | 規則 |
|---|---|
| **ファイルエンコード** | **UTF-8 BOM付き** (`EF BB BF`) で保存する。BOM なし・Shift-JIS は禁止。 |
| **code_page pragma** | `#pragma code_page(65001)` を使用する。`932`（Shift-JIS）は禁止。 |
| **TEXTINCLUDE 埋め込み** | TEXTINCLUDE リソース内の `"#pragma code_page(...)\r\n"` 文字列も `65001` にする。 |
| **既存 SJIS ファイルの変換手順** | `[System.IO.File]::ReadAllText(path, Encoding.GetEncoding(932))` で読み込み、`code_page(932)→65001` を置換後、`UTF8Encoding($true)` で書き直す。テキストエディタによる「上書き保存」は BOM が落ちる場合があるため PowerShell/スクリプトで行う。 |
| **確認方法** | 変換後、先頭 3 バイトが `0xEF 0xBB 0xBF` であることをバイト列で検証する。 |

---

## Key conventions for edits

1. **All cross-dialog shared state lives in `DigitShowContext`.**  
   Dialog constructors read from `ctx`; `OnOK` / `OnBUTTONUpdate` write back to `ctx` after `UpdateData(TRUE)`.

2. **Do not write Modbus output directly from dialogs/control code.**  
   Control logic updates `ctx->DAVout` (in volts). Actual hardware output happens in `DA_OUTPUT()` where values are clamped/converted to mV and sent.

3. **Use channel constants from `ModbusRTU` instead of magic channel counts.**  
   AI is `ModbusRTU::AI_CHANNELS` (16), AO is `ModbusRTU::AO_CHANNELS` (8).

4. **Board lifecycle safety pattern:**  
   `OpenBoard()` opens Modbus and zeroes AO once; `CloseBoard()` zeroes AO then closes port.

5. **Current runtime defaults are hardcoded and relied on by UI flow:**  
   Modbus open uses `COM9`, slave address `1`, and view forces timer-1 interval to `100ms` on initial update.

6. **AI input channel assignments (Phyout[] / Vout[] index → sensor):**

   | CH (index) | Sensor |
   |---|---|
   | CH0 | Load Cell |
   | CH1 | Displacement |
   | CH2 | LDT-V1 |
   | CH3 | LDT-V2 |
   | CH4 | Cell Pressure |
   | CH5 | (unused) |
   | CH6 | (unused) |
   | CH7 | (unused) |
   | CH8 | Effective Cell Pressure |
   | CH9 | Volumetric Change |
   | CH10–CH15 | (unused) |

   `Cal_Physical()` applies calibration generically to all 16 channels.  
   `Cal_Param()` reads specific indices above to compute physical quantities.  
   ⚠️ If hardware channel wiring changes, update `Cal_Param()`, `Specimen.cpp`, `TransAdjustment.cpp`, `CalibrationFactor.cpp` (labels), `DigitShowBasicView.cpp` (`.dat` header), and `DigitShowBasic.rc` (UI labels) together.  
   ⚠️ Existing `.cal` files store calibration coefficients keyed by channel index — hardware rewiring requires recalibration.

