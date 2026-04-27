/*
 * DigitShowBasic - Triaxial Test Machine Control Software
 * Copyright (C) 2025 Makoto KUNO
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include    "stdafx.h"
#include    "DigitShowBasic.h"
#include    "DigitShowBasicDoc.h"
#include    "ModbusRTU.h"

#include    "time.h"
#include    "math.h"

#include    <setupapi.h>
#pragma comment(lib, "setupapi.lib")

// NOTE: The following areas may need manual adjustment after migration:
// 1. The COM port for Modbus needs to be configured (currently hardcoded as "COM3")
// 2. The Modbus slave address needs to be configured (currently set to 1)
// 3. The timer-based AD acquisition is handled in DigitShowBasicView::OnTimer()
// 4. DA output is now triggered only when values change, after AI read

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNCREATE(CDigitShowBasicDoc, CDocument)

BEGIN_MESSAGE_MAP(CDigitShowBasicDoc, CDocument)
    //{{AFX_MSG_MAP(CDigitShowBasicDoc)
        // メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
        //        この位置に生成されるコードを編集しないでください。
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDigitShowBasicDoc クラスの構築/消滅

CDigitShowBasicDoc::CDigitShowBasicDoc()
{

}

CDigitShowBasicDoc::~CDigitShowBasicDoc()
{
}

BOOL CDigitShowBasicDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;


    // (SDI ドキュメントはこのドキュメントを再利用します。)

    return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CDigitShowBasicDoc シリアライゼーション

void CDigitShowBasicDoc::Serialize(CArchive& ar)
{
    if (ar.IsStoring())
    {
    
    }
    else
    {
    
    }
}

/////////////////////////////////////////////////////////////////////////////
// CDigitShowBasicDoc クラスの診断

#ifdef _DEBUG
void CDigitShowBasicDoc::AssertValid() const
{
    CDocument::AssertValid();
}

void CDigitShowBasicDoc::Dump(CDumpContext& dc) const
{
    CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// USB COMポート自動検出ヘルパー
//
// SetupAPI でシステムに存在するシリアルポートクラスのデバイスを列挙し、
// ハードウェアID が "USB\" で始まる USB-COMポートだけを対象に
// 既知の VID/PID テーブルと照合して優先度スコアを付ける。
// 最高スコアのポートを返す。同スコアなら番号の大きいものを優先する。
// 既知デバイスに該当しない USB-COMポートもスコア1(最低)で候補に残る。
// ハードウェアCOMポート(ACPI/ISA等)は除外する。
static CString DetectArduinoPort()
{
    // //@todo 以下のテーブルに検出したい VID/PID を追加・変更できます。
    //         pid が NULL の場合は該当 VID のどの PID も一致とみなします。
    static const struct {
        const char* vid;
        const char* pid;    // NULL = VID が一致すれば PID は問わない
        int         priority;
        const char* desc;
    } knownDevices[] = {
        // Arduino UNO R4 Minima / WiFi (Renesas RA4M1)
        { "2341", "0069", 95, "Arduino UNO R4 Minima" },
        { "2341", "0243", 95, "Arduino Nano ESP32" },
        // Arduino 公式 (VID 0x2341) — 上記以外のモデルも含む
        { "2341", NULL,   90, "Arduino" },
        // Raspberry Pi Pico / Pico W (RP2040 CDC)
        { "2E8A", "000A", 90, "Raspberry Pi Pico (CDC)" },
        { "2E8A", "0005", 88, "Raspberry Pi Pico (MicroPython)" },
        { "2E8A", NULL,   86, "Raspberry Pi (RP2040)" },
        // STM32 Virtual COM Port (CDC ACM)
        { "0483", "5740", 85, "STM32 CDC ACM" },
        { "0483", "374B", 82, "STM32 ST-LINK CDC" },
        // CH340 / CH341 (Arduino clone 定番チップ)
        { "1A86", "7523", 80, "CH340" },
        { "1A86", "55D3", 80, "CH340C" },
        { "1A86", "7522", 80, "CH341" },
        { "1A86", NULL,   78, "CH340/CH341 (any)" },
        // CP2102 / CP2104 (Silicon Labs)
        { "10C4", "EA60", 80, "CP2102/CP2104" },
        { "10C4", NULL,   78, "CP210x (any)" },
    };
    const int numKnown = static_cast<int>(sizeof(knownDevices) / sizeof(knownDevices[0]));

    CString bestPort = "";
    int     bestPriority = -1;
    int     bestPortNum  = -1;

    // シリアルポートクラス GUID: {4D36E978-E325-11CE-BFC1-08002BE10318}
    static const GUID GUID_DEVCLASS_PORTS =
        { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };

    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return bestPort;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD idx = 0; SetupDiEnumDeviceInfo(hDevInfo, idx, &devInfoData); idx++)
    {
        // ハードウェアID を取得 (複数行の MULTI_SZ 先頭エントリのみ使用)
        char hwId[1024] = { 0 };
        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData,
            SPDRP_HARDWAREID, NULL, reinterpret_cast<PBYTE>(hwId), sizeof(hwId) - 1, NULL))
            continue;

        // "USB\" で始まらないものはハードウェアCOMポートなのでスキップ
        if (_strnicmp(hwId, "USB\\", 4) != 0)
            continue;

        // フレンドリ名からCOMポート番号を取得 ("USB Serial Port (COM12)" → "COM12")
        char friendlyName[256] = { 0 };
        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData,
            SPDRP_FRIENDLYNAME, NULL, reinterpret_cast<PBYTE>(friendlyName), sizeof(friendlyName) - 1, NULL))
            continue;

        char* comStart = strstr(friendlyName, "(COM");
        if (!comStart) continue;
        char portNumStr[16] = { 0 };
        if (sscanf_s(comStart + 4, "%10[^)]", portNumStr, static_cast<unsigned int>(sizeof(portNumStr))) != 1)
            continue;
        int portNum = atoi(portNumStr);
        CString portName;
        portName.Format("COM%s", portNumStr);

        // ハードウェアIDから VID / PID を解析
        // 例: "USB\VID_1A86&PID_7523&REV_0264"
        char* vidPtr = strstr(hwId, "VID_");
        char* pidPtr = strstr(hwId, "PID_");
        if (!vidPtr || !pidPtr) continue;
        char vid[5] = { 0 }, pid[5] = { 0 };
        strncpy_s(vid, sizeof(vid), vidPtr + 4, _TRUNCATE);
        strncpy_s(pid, sizeof(pid), pidPtr + 4, _TRUNCATE);
        _strupr_s(vid);
        _strupr_s(pid);

        // 既知デバイステーブルと照合して優先度を決定
        int priority = 1;   // USB-COMだがテーブル未登録の場合の最低スコア
        for (int k = 0; k < numKnown; k++)
        {
            if (_stricmp(vid, knownDevices[k].vid) == 0)
            {
                if (knownDevices[k].pid == NULL || _stricmp(pid, knownDevices[k].pid) == 0)
                {
                    priority = knownDevices[k].priority;
                    break;
                }
            }
        }

        // 最高優先度を更新。同優先度ならポート番号の大きい方を採用
        if (priority > bestPriority ||
            (priority == bestPriority && portNum > bestPortNum))
        {
            bestPriority = priority;
            bestPortNum  = portNum;
            bestPort     = portName;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return bestPort;
}

/////////////////////////////////////////////////////////////////////////////
// CDigitShowBasicDoc コマンド
void CDigitShowBasicDoc::OpenBoard()
{
    DigitShowContext* ctx = GetContext();
    
    if(ctx->FlagSetBoard){
        AfxMessageBox("Initialization has been already accomplished", MB_ICONSTOP | MB_OK );
        return;
    }
    else{
        //@todo COMポートを手動で指定したい場合は strPort に直接ポート名を設定してください。
        //@todo 例: strPort = "COM3";  または strPort = "COM10"; など
        //@todo 空文字列 ("") のままにすると、以下の自動検出処理が実行されます。
        CString strPort = "";  // COMポート番号指定文字列 (空の場合は自動検出)

        // strPort が未指定の場合、USB COMポートのうち Arduino/Pico/STM32/CH340/CP2102
        // っぽいポートを自動検出する。判別できない場合は最大ポート番号を採用。
        if (strPort.IsEmpty())
        {
            strPort = DetectArduinoPort();
        }

        if (strPort.IsEmpty())
        {
            AfxMessageBox("Arduino互換USB COMポートが見つかりませんでした。\nデバイスの接続とドライバを確認してください。",
                MB_ICONSTOP | MB_OK);
            return;
        }

        // Open Modbus RTU connection
        // COM port is specified here - no configuration dialog needed
        ModbusRTU* modbus = GetModbusInstance();
        
        if(!modbus->Open(strPort, 1)){
            ctx->TextString.Format("Modbus Open failed: %s", modbus->GetLastError());
            AfxMessageBox(ctx->TextString, MB_ICONSTOP | MB_OK);
            return;
        }
        
        // Sampling settings (simplified for Modbus - no buffering/averaging needed)
        ctx->sampling.SavingTime = 300;
        ctx->sampling.TotalSamplingTimes = ctx->sampling.SavingTime * 10;  // 10 samples/sec at 100ms interval
        
        // Output 0V to all AO channels on startup
        uint16_t zeroOutput[ModbusRTU::AO_CHANNELS] = {0};
        modbus->WriteHoldingRegisters(zeroOutput);
        
        // Initialize DAVout to 0
        for(int i = 0; i < ModbusRTU::AO_CHANNELS; i++){
            ctx->DAVout[i] = 0.0f;
        }
        
        ctx->FlagSetBoard = TRUE;
    }
    return;
}

void CDigitShowBasicDoc::CloseBoard()
{
    DigitShowContext* ctx = GetContext();
    // Close Modbus connection
    if( ctx->FlagSetBoard==TRUE ){
        ModbusRTU* modbus = GetModbusInstance();
        
        // Output 0V to all AO channels before closing
        uint16_t zeroOutput[ModbusRTU::AO_CHANNELS] = {0};
        modbus->WriteHoldingRegisters(zeroOutput);
        
        modbus->Close();
        ctx->FlagSetBoard = FALSE;
    }
}

//--- Input from A/D Board ---
// This function is called by the timer every 100ms (Timer ID 1 in DigitShowBasicView)
// Reads 16 channels of int16_t data via Modbus RTU ReadInputRegisters
// HX711 (ch 0-7): int16_t values
// ADS1115 (ch 8-15): int16_t values
// No averaging is performed (Modbus assumed to be noise-free)
void CDigitShowBasicDoc::AD_INPUT()
{
    DigitShowContext* ctx = GetContext();
    ModbusRTU* modbus = GetModbusInstance();
    
    if(!modbus->IsOpen()){
        return;
    }
    
    // Read all 16 AI channels via Modbus
    int16_t aiData[ModbusRTU::AI_CHANNELS];
    if(!modbus->ReadInputRegisters(aiData)){
        // Read failed - could log error here
        // ctx->TextString.Format("Modbus read error: %s", modbus->GetLastError());
        return;
    }
    
    // Convert int16_t values to float for Vout
    // NOTE: The calibration factors (ctx->cal) will convert these to physical values
    // in Cal_Physical(). The raw int16_t values are stored as-is.
    for(int i = 0; i < ModbusRTU::AI_CHANNELS; i++){
        ctx->Vout[i] = static_cast<float>(aiData[i]);
    }
}

//--- Output to D/A Board ---
// This function outputs to 8 channels via Modbus RTU WriteHoldingRegisters
// GP8403 (ch 0-7): 0-10000 mV output, clamped to range
// Only writes when values have changed (change detection is in ModbusRTU class)
void CDigitShowBasicDoc::DA_OUTPUT()
{
    DigitShowContext* ctx = GetContext();
    ModbusRTU* modbus = GetModbusInstance();
    
    if(!modbus->IsOpen()){
        return;
    }
    
    // Convert DAVout (voltage) to mV for GP8403
    // DAVout is in Volts (0-10V range), GP8403 expects 0-10000 mV
    uint16_t aoData[ModbusRTU::AO_CHANNELS];
    for(int i = 0; i < ModbusRTU::AO_CHANNELS; i++){
        // Clamp voltage to 0-10V range
        float voltage = ctx->DAVout[i];
        if(voltage < 0.0f) voltage = 0.0f;
        if(voltage > 10.0f) voltage = 10.0f;
        
        // Convert to mV (0-10000)
        aoData[i] = static_cast<uint16_t>(voltage * 1000.0f);
    }
    
    // Write to Modbus (only if values changed)
    modbus->WriteHoldingRegisters(aoData);
}

//--- Calcuration of Physical Value ---
void CDigitShowBasicDoc::Cal_Physical()
{
    DigitShowContext* ctx = GetContext();
    int    i;
    for(i = 0;i< ModbusRTU::AI_CHANNELS;i++){
        ctx->Phyout[i] =    ctx->cal.a[i]* ctx->Vout[i]* ctx->Vout[i] + ctx->cal.b[i]* ctx->Vout[i] + ctx->cal.c[i];
    }
}

//--- Calcuration of the Other Parameters ---
void CDigitShowBasicDoc::Cal_Param()
{
    DigitShowContext* ctx = GetContext();
    auto SpecimenData = &ctx->specimen;
    //    Specimen Data in drain and undrain condition
    ctx->height = SpecimenData->Height[0]-ctx->Phyout[1];               // Current height
    ctx->volume = SpecimenData->Volume[0]- ctx->Phyout[9];              // Current volume in drain condition
    ctx->area = ctx->volume/ ctx->height;                               // Current area
    ctx->phys.ea = -log(ctx->height/SpecimenData->Height[0])*100.0;     // True Axial Strain (%)
    ctx->phys.ev = -log(ctx->volume/SpecimenData->Volume[0])*100.0;     // True Volumetric Strain in drain condition (%)
    ctx->phys.er = (ctx->phys.ev- ctx->phys.ea)/2.0;                    // True Radial strain (%)
    if(SpecimenData->VLDT1[0]>0.0 && ctx->Phyout[6]>0.0) {
        // True LDT Strain (%)
        ctx->phys.eLDT1 = -log(ctx->Phyout[6]/SpecimenData->VLDT1[0])*100.0;
    }
    else{
        ctx->phys.eLDT1 = 0.0;
    }
    if(SpecimenData->VLDT2[0]>0.0 && ctx->Phyout[7]>0.0) {
        // True LDT Strain (%)
        ctx->phys.eLDT2 = -log(ctx->Phyout[7]/SpecimenData->VLDT2[0])*100.0;
    }
    else{
        ctx->phys.eLDT2 = 0.0;
    }
    ctx->phys.eLDT = (ctx->phys.eLDT1+ ctx->phys.eLDT2)/2.0;
    ctx->phys.q = ctx->Phyout[0]/ctx->area*1000.0;              // Deviator Stress (kPa)
    ctx->phys.sr = ctx->Phyout[2];                              // Cell(Radial) Stress (kPa)
    ctx->phys.sa = ctx->phys.q+ ctx->phys.sr;                   // Axial Stress (kPa)
    ctx->phys.p = (ctx->phys.sa+2.0* ctx->phys.sr)/3.0;         // Mean Principal Stress (kPa)
    ctx->phys.e_sr = ctx->Phyout[8];                            // Cell Effective Stress (kPa)
    ctx->phys.e_sa = ctx->phys.q+ ctx->phys.e_sr;               // Axial Effective Stress (kPa)
    ctx->phys.u = ctx->phys.sr- ctx->phys.e_sr;                 // Pore Pressure
    ctx->phys.e_p = (ctx->phys.e_sa+2.0* ctx->phys.e_sr)/3.0;   // Mean Effective Stress (kPa)
    //---The Value to display---
    ctx->CalParam[0] = ctx->phys.sa;
    ctx->CalParam[1] = ctx->phys.sr;
    ctx->CalParam[2] = ctx->phys.e_sa;
    ctx->CalParam[3] = ctx->phys.e_sr;
    ctx->CalParam[4] = ctx->phys.u;
    ctx->CalParam[5] = ctx->phys.p;
    ctx->CalParam[6] = ctx->phys.q;
    ctx->CalParam[7] = ctx->phys.e_p;
    ctx->CalParam[8] = ctx->phys.ea;
    ctx->CalParam[9] = ctx->phys.er;
    ctx->CalParam[10] = ctx->phys.ev;
    ctx->CalParam[11] = ctx->phys.eLDT1;
    ctx->CalParam[12] = ctx->phys.eLDT2;
    ctx->CalParam[13] = ctx->phys.eLDT;
    ctx->CalParam[14] = (ctx->phys.e_sa+ ctx->phys.e_sr)/2.0;
    ctx->CalParam[15] = (ctx->phys.e_sa- ctx->phys.e_sr)/2.0;
}

void CDigitShowBasicDoc::SaveToFile()
{
    DigitShowContext* ctx = GetContext();
    // Save Voltage and Physical Data
    int    i,j,k;

    k = 0;
    fprintf(ctx->FileSaveData0,"%.3lf    ",ctx->SequentTime2);
    fprintf(ctx->FileSaveData1,"%.3lf    ",ctx->SequentTime2);
    for(j = 0;j<ModbusRTU::AI_CHANNELS;j++){
        fprintf(ctx->FileSaveData0,"%lf    ",ctx->Vout[k]);
        fprintf(ctx->FileSaveData1,"%lf    ", ctx->Phyout[k]);
        k = k+1;
    }
    fprintf(ctx->FileSaveData0,"\n");
    fprintf(ctx->FileSaveData1,"\n");
    // Save Parameter Data
    fprintf(ctx->FileSaveData2,"%.3lf    ",ctx->SequentTime2);    
    for(i = 0;i<16;i++){
        fprintf(ctx->FileSaveData2,"%lf    ",ctx->CalParam[i]);
    }
    fprintf(ctx->FileSaveData2,"\n");
}

void CDigitShowBasicDoc::SaveToFile2()
{
    DigitShowContext* ctx = GetContext();
    int    i,j,k;
    for(i = 0;i<ctx->sampling.CurrentSamplingTimes;i++){
        k = 0;
        fprintf(ctx->FileSaveData0,"%.3lf    ",ctx->sampling.SavingClock/1000000.0*i);
        fprintf(ctx->FileSaveData1,"%.3lf    ",ctx->sampling.SavingClock/1000000.0*i);
        for(j = 0;j<ModbusRTU::AI_CHANNELS;j++){
            ctx->Vtmp = ctx->ai_raw[j];
            ctx->Ptmp = ctx->cal.a[k]*ctx->Vtmp*ctx->Vtmp+ctx->cal.b[k]*ctx->Vtmp+ctx->cal.c[k];
            k = k+1;
            fprintf(ctx->FileSaveData0,"%lf    ",ctx->Vtmp);
            fprintf(ctx->FileSaveData1,"%lf    ",ctx->Ptmp);
        }
        fprintf(ctx->FileSaveData0,"\n");
        fprintf(ctx->FileSaveData1,"\n");
    }
}

//--- Control Statements ---
void CDigitShowBasicDoc::Control_DA()
{
    DigitShowContext* ctx = GetContext();
    auto ControlData = ctx->control;

    switch (ctx->ControlID)
    {
    case 0:
        { 
        }
        break;
    case 1:
        { 
        //---Before Consolidation: Keep the specimen isotropic condition by Motor Control.--- 
        // ControlData[1].q: Reference Error Stress (kPa).
        // ControlData[1].MotorSpeed: The Maximum Motor Speed (rpm).
            SetMotorOn();
            if(ctx->phys.q > ctx->errTol.StressCom ){
                SetMotorUp();
                if( ctx->phys.q > ControlData[1].q ){
                    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[1].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
                }
                if( ctx->phys.q <= ControlData[1].q ){
                    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*(ctx->phys.q/ControlData[1].q)*ControlData[1].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
                }
            }
            else if( ctx->phys.q < ctx->errTol.StressExt ){
                SetMotorDown();
                if( ctx->phys.q < -ControlData[1].q ){
                    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[1].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
                }
                if( ctx->phys.q >= -ControlData[1].q ){
                    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*(-ctx->phys.q/ControlData[1].q)*ControlData[1].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
                }
            }
            else {
                    SetMotorSpeed(0.0f);
            }
        }
        break;
    case 2:
        { 
        // Consolidation (Motor Control):
        // ControlData[2].e_sigma[0]:    Target Axial Effectve Stress,
        // ControlData[2].K0:            K0 value,
        // ControlData[2].sigmaRate[2]:    Increase Rate of Cell Pressure 
        // ControlData[2].MotorSpeed:    Motor Speed
            SetMotorOn();
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[2].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            if( ctx->phys.e_sr < ControlData[2].e_sigma[0]*ControlData[2].K0-ctx->errTol.StressA){
                SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*ControlData[2].sigmaRate[2]/60.0*ctx->timeSettings.Interval2/1000.0));
            }    
            if( ctx->phys.e_sr > ControlData[2].e_sigma[0]*ControlData[2].K0+ctx->errTol.StressA){
                SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]-float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*ControlData[2].sigmaRate[2]/60.0*ctx->timeSettings.Interval2/1000.0));
            }    
            if( ctx->phys.e_sa < ctx->phys.e_sr/ControlData[2].K0+ctx->errTol.StressExt ){
                SetMotorDown();
            }            
            else if( ctx->phys.e_sa > ctx->phys.e_sr/ControlData[2].K0+ctx->errTol.StressCom ){
                SetMotorUp();
            }
            else {
                SetMotorSpeed(0.0f);
            }
        }
        break;
    case 3:
        { 
        // Monotonic Loading (Motor Control)
        // ControlData[3].MotorSpeed:    Motor Speed
        // ControlData[3].MotorCruch:    Compression:1 /Extension:0                        
        // ControlData[3].flag[0]:        Monotonic_Loading:0 /Creep:1
        // ControlData[3].sigma[0];        Limiter
            SetMotorOn();
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[3].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            if(ControlData[3].flag[0]==FALSE){        // Monotonic Loading
                if(ControlData[3].MotorCruch == 0 ){
                    SetMotorDown();
                    if( ctx->phys.q >= ControlData[3].q) ControlData[3].flag[0] = TRUE;
                }
                if(ControlData[3].MotorCruch == 1 )    {
                    SetMotorUp();
                    if( ctx->phys.q <= ControlData[3].q) ControlData[3].flag[0] = TRUE;
                }
            }
            if(ControlData[3].flag[0]==TRUE){        // Creep
                if(ControlData[3].MotorCruch == 0 ){
                    SetMotorDown();
                    if( ctx->phys.q>=ControlData[3].q+ctx->errTol.StressExt)    SetMotorSpeed(0.0f);
                }
                if(ControlData[3].MotorCruch == 1 ){
                    SetMotorUp();
                    if( ctx->phys.q<=ControlData[3].q+ctx->errTol.StressCom)    SetMotorSpeed(0.0f);
                }
            }
        }
        break;
    case 4:
        { 
        // Monotonic Loading (Motor Control)
        // ControlData[4].MotorSpeed:    Motor Speed
        // ControlData[4].MotorCruch:    Cruch Loading:1 /Unloading:0                        
        // ControlData[4].flag:            Loading:0 /Creep:1
        // ControlData[4].sigma[0];        Limiter
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[4].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            SetMotorOn();
            if(ControlData[4].flag[0]==FALSE){        // Monotonic Loading
                if(ControlData[4].MotorCruch == 0 ){
                    SetMotorDown();
                    if( ctx->phys.q >= ControlData[4].q) ControlData[4].flag[0] = TRUE;
                }
                if(ControlData[4].MotorCruch == 1 )    {
                    SetMotorUp();
                    if( ctx->phys.q <= ControlData[4].q) ControlData[4].flag[0] = TRUE;
                }
            }
            if(ControlData[4].flag[0]==TRUE){        // Creep
                if(ControlData[4].MotorCruch == 0 ){
                    SetMotorDown();
                    if( ctx->phys.q>=ControlData[4].q+ctx->errTol.StressExt)    SetMotorSpeed(0.0f);
                }
                if(ControlData[4].MotorCruch == 1 ){
                    SetMotorUp();
                    if( ctx->phys.q<=ControlData[4].q+ctx->errTol.StressCom)    SetMotorSpeed(0.0f);
                }
            }
        }
        break;
    case 5:
        { 
            // Cyclic Loading
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[5].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            SetMotorOn();
            if(ControlData[5].flag[0]==FALSE){            // Cyclic in compression test
                if(ControlData[5].time[0]<ControlData[5].time[1]){ 
                    SetMotorDown();
                    if( ctx->phys.q>=ControlData[5].sigma[1]) {
                        ControlData[5].time[0] = ControlData[5].time[1];
                        ctx->FlagCyclic = FALSE;
                    }
                }
                if(ControlData[5].time[1]<=ControlData[5].time[0] || ControlData[5].time[0]<=ControlData[5].time[2]){
                    if(ctx->FlagCyclic==FALSE){
                        SetMotorUp();
                        if( ctx->phys.q<=ControlData[5].sigma[0]) ctx->FlagCyclic = TRUE;
                    }
                    if(ctx->FlagCyclic==TRUE){
                        SetMotorDown();
                        if( ctx->phys.q>=ControlData[5].sigma[1]) {
                            ctx->FlagCyclic = FALSE;
                            ControlData[5].time[0] = ControlData[5].time[0]+1;
                        }
                    }
                }
                if(ControlData[5].time[0]>ControlData[5].time[2]){ 
                    SetMotorDown();
                }
            }
            if(ControlData[5].flag[0]==TRUE){
                if(ControlData[5].time[0]<ControlData[5].time[1]){ 
                    SetMotorUp();
                    if( ctx->phys.q<=ControlData[5].sigma[0]) {
                        ControlData[5].time[0] = ControlData[5].time[1];
                        ctx->FlagCyclic = TRUE;
                    }
                }
                if(ControlData[5].time[1]<=ControlData[5].time[0] || ControlData[5].time[0]<=ControlData[5].time[2]){
                    if(ctx->FlagCyclic==TRUE){
                        SetMotorDown();
                        if( ctx->phys.q>=ControlData[5].sigma[1]) ctx->FlagCyclic = FALSE;
                    }
                    if(ctx->FlagCyclic==FALSE){
                        SetMotorUp();
                        if( ctx->phys.q<=ControlData[5].sigma[0]) {
                            ctx->FlagCyclic = TRUE;
                            ControlData[5].time[0] = ControlData[5].time[0]+1;
                        }
                    }
                }
                if(ControlData[5].time[0]>ControlData[5].time[2]){ 
                    SetMotorUp();
                }
            }
        }
        break;
    case 6:
        { 
            // Drain Cyclic Loading
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[6].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            SetMotorOn();
            if(ControlData[6].flag[0]==FALSE){
                if(ControlData[6].time[0]<ControlData[6].time[1]){ 
                    SetMotorDown();
                    if( ctx->phys.q>=ControlData[6].sigma[1]) {
                        ControlData[6].time[0] = ControlData[6].time[1];
                        ctx->FlagCyclic = FALSE;
                    }
                }
                if(ControlData[6].time[1]<=ControlData[6].time[0] || ControlData[6].time[0]<=ControlData[6].time[2]){
                    if(ctx->FlagCyclic==FALSE){
                        SetMotorUp();
                        if( ctx->phys.q<=ControlData[6].sigma[0]) ctx->FlagCyclic = TRUE;
                    }
                    if(ctx->FlagCyclic==TRUE){
                        SetMotorDown();
                        if( ctx->phys.q>=ControlData[6].sigma[1]) {
                            ctx->FlagCyclic = FALSE;
                            ControlData[6].time[0] = ControlData[6].time[0]+1;
                        }
                    }
                }
                if(ControlData[6].time[0]>ControlData[6].time[2]){ 
                    SetMotorDown();
                }
            }
            if(ControlData[6].flag[0]==TRUE){
                if(ControlData[6].time[0]<ControlData[6].time[1]){ 
                    SetMotorUp();
                    if( ctx->phys.q<=ControlData[6].sigma[0]) {
                        ControlData[6].time[0] = ControlData[6].time[1];
                        ctx->FlagCyclic = TRUE;
                    }
                }
                if(ControlData[6].time[1]<=ControlData[6].time[0] || ControlData[6].time[0]<=ControlData[6].time[2]){
                    if(ctx->FlagCyclic==TRUE){
                        SetMotorDown();
                        if( ctx->phys.q>=ControlData[6].sigma[1]) ctx->FlagCyclic = FALSE;
                    }
                    if(ctx->FlagCyclic==FALSE){
                        SetMotorUp();
                        if( ctx->phys.q<=ControlData[6].sigma[0]) {
                            ctx->FlagCyclic = TRUE;
                            ControlData[6].time[0] = ControlData[6].time[0]+1;
                        }
                    }
                }
                if(ControlData[6].time[0]>ControlData[6].time[2]){ 
                    SetMotorUp();
                }
            }
        }
        break;
    case 7:
        { 
            SetMotorOn();
            SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ControlData[7].MotorSpeed+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
            if(ControlData[7].sigma[1] == ControlData[7].e_sigma[1]){
                SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ControlData[7].e_sigma[1]- ctx->phys.e_sr)));
                if( ctx->phys.e_sa > ControlData[7].e_sigma[0]+ctx->errTol.StressCom)        SetMotorUp();
                else if( ctx->phys.e_sa < ControlData[7].e_sigma[0]+ctx->errTol.StressExt)    SetMotorDown();
                else SetMotorSpeed(0.0f);
            }
            if(ControlData[7].sigma[1] < ControlData[7].e_sigma[1]){
                if( ctx->phys.e_sr >= ControlData[7].e_sigma[1]) {
                    SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]-float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ctx->phys.e_sr-ControlData[7].e_sigma[1])));
                }
                if( ctx->phys.e_sr < ControlData[7].e_sigma[1]) {
                    SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*fabs(ControlData[7].sigmaRate[0])/60.0*ctx->timeSettings.Interval2/1000.0));
                }
                if( ctx->phys.e_sa > (ControlData[7].e_sigma[0]-ControlData[7].sigma[0])/(ControlData[7].e_sigma[1]-ControlData[7].sigma[1])*(ctx->phys.e_sr-ControlData[7].sigma[1])+ControlData[7].sigma[0]+ctx->errTol.StressCom){
                    SetMotorUp();
                }
                else if( ctx->phys.e_sa < (ControlData[7].e_sigma[0]-ControlData[7].sigma[0])/(ControlData[7].e_sigma[1]-ControlData[7].sigma[1])*(ctx->phys.e_sr-ControlData[7].sigma[1])+ControlData[7].sigma[0]+ctx->errTol.StressExt){
                    SetMotorDown();
                }
                else {
                    SetMotorSpeed(0.0f);
                }
            }
            if(ControlData[7].sigma[1] > ControlData[7].e_sigma[1]){
                if( ctx->phys.e_sr > ControlData[7].e_sigma[1]) {
                    SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]-float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*fabs(ControlData[7].sigmaRate[0])/60.0*ctx->timeSettings.Interval2/1000.0));
                }
                if( ctx->phys.e_sr <= ControlData[7].e_sigma[1]) {
                    SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ControlData[7].e_sigma[1]- ctx->phys.e_sr)));
                }
                if( ctx->phys.e_sa > (ControlData[7].e_sigma[0]-ControlData[7].sigma[0])/(ControlData[7].e_sigma[1]-ControlData[7].sigma[1])*(ctx->phys.e_sr-ControlData[7].sigma[1])+ControlData[7].sigma[0]+ctx->errTol.StressCom){
                    SetMotorUp();
                }
                else if( ctx->phys.e_sa < (ControlData[7].e_sigma[0]-ControlData[7].sigma[0])/(ControlData[7].e_sigma[1]-ControlData[7].sigma[1])*(ctx->phys.e_sr-ControlData[7].sigma[1])+ControlData[7].sigma[0]+ctx->errTol.StressExt){
                    SetMotorDown();
                }
                else {
                    SetMotorSpeed(0.0f);
                }
            }
        }
        break;
    case 8:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 9:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 10:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 11:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 12:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 13:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 14:
        { 
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
        }
        break;
    case 15:
        { 
            if( ctx->controlFile.CurrentNum >=0 && ctx->controlFile.CurrentNum < 128 ){
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==0 ){
                    SetMotorOff();
                }
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==1 )    MLoading_Stress();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==2 )    MLoading_Strain();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==3 )    CLoading_Stress();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==4 )    CLoading_Strain();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==5 )    Creep();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==6 )    LinearEffectiveStressPath();
                if( ctx->controlFile.Num[ctx->controlFile.CurrentNum]==7 )    Creep2();
            // Note: DAVout values are written by Timer 1 DA_OUTPUT()
            }
        }
        break;
    }
}
void CDigitShowBasicDoc::Start_Control()
{

}

void CDigitShowBasicDoc::SetMotorOn(void)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.MotorOnOff] = 5.0f;
}

void CDigitShowBasicDoc::SetMotorOff(void)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.MotorOnOff] = 0.0f;
}

void CDigitShowBasicDoc::SetMotorUp(void)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.MotorUpDown] = 5.0f;
}

void CDigitShowBasicDoc::SetMotorDown(void)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.MotorUpDown] = 0.0f;
}

void CDigitShowBasicDoc::SetMotorSpeed(float voltage)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.MotorSpeed] = voltage;
}

void CDigitShowBasicDoc::SetEPCell(float voltage)
{
    DigitShowContext* ctx = GetContext();
    ctx->DAVout[ctx->daChannel.EP_Cell] = voltage;
}

void CDigitShowBasicDoc::Stop_Control()
{
    SetMotorSpeed(0.0f);
}

void CDigitShowBasicDoc::MLoading_Stress()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==0.0){
        if( ctx->phys.q <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
            SetMotorDown();
        }
        else {
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==1.0){
        if( ctx->phys.q >= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
            SetMotorUp();
        }
        else {
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
        }
    }
}

void CDigitShowBasicDoc::MLoading_Strain()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==0.0){
        if(ctx->phys.ea <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
            SetMotorDown();
        }
        else {
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==1.0){
        if(ctx->phys.ea >= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
            SetMotorUp();
        }
        else {
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
        }
    }
}

void CDigitShowBasicDoc::CLoading_Stress()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==0.0){
        if(ctx->NumCyclic==0){
            ctx->FlagCyclic = FALSE;
            ctx->NumCyclic = 1;
        }
        if(ctx->NumCyclic!=0 && ctx->NumCyclic <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){
            if(ctx->FlagCyclic==FALSE){
                SetMotorUp();
                if( ctx->phys.q<=ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) ctx->FlagCyclic = TRUE;
            }
            if(ctx->FlagCyclic==TRUE){
                SetMotorDown();
                if( ctx->phys.q>=ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) {
                    ctx->FlagCyclic = FALSE;
                    ctx->NumCyclic = ctx->NumCyclic+1;
                }
            }
        }
        if(ctx->NumCyclic>ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){ 
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
            ctx->NumCyclic = 0;
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==1.0){
        if(ctx->NumCyclic==0){
            ctx->FlagCyclic = TRUE;
            ctx->NumCyclic = 1;
        }
        if(ctx->NumCyclic!=0 && ctx->NumCyclic <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){
            if(ctx->FlagCyclic==FALSE){
                SetMotorUp();
                if( ctx->phys.q<=ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
                    ctx->FlagCyclic = TRUE;
                    ctx->NumCyclic = ctx->NumCyclic+1;
                }
            }
            if(ctx->FlagCyclic==TRUE){
                SetMotorDown();
                if( ctx->phys.q>=ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) ctx->FlagCyclic = FALSE;
            }
        }
        if(ctx->NumCyclic>ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){ 
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
            ctx->NumCyclic = 0;
        }
    }
}

void CDigitShowBasicDoc::CLoading_Strain()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==0.0){
        if(ctx->NumCyclic==0){
            ctx->FlagCyclic = FALSE;
            ctx->NumCyclic = 1;
        }
        if(ctx->NumCyclic!=0 && ctx->NumCyclic <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){
            if(ctx->FlagCyclic==FALSE){
                SetMotorUp();
                if(ctx->phys.ea<=ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) ctx->FlagCyclic = TRUE;
            }
            if(ctx->FlagCyclic==TRUE){
                SetMotorDown();
                if(ctx->phys.ea>=ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) {
                    ctx->FlagCyclic = FALSE;
                    ctx->NumCyclic = ctx->NumCyclic+1;
                }
            }
        }
        if(ctx->NumCyclic>ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){ 
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
            ctx->NumCyclic = 0;
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]==1.0){
        if(ctx->NumCyclic==0){
            ctx->FlagCyclic = TRUE;
            ctx->NumCyclic = 1;
        }
        if(ctx->NumCyclic!=0 && ctx->NumCyclic <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){
            if(ctx->FlagCyclic==FALSE){
                SetMotorUp();
                if(ctx->phys.ea<=ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
                    ctx->FlagCyclic = TRUE;
                    ctx->NumCyclic = ctx->NumCyclic+1;
                }
            }
            if(ctx->FlagCyclic==TRUE){
                SetMotorDown();
                if(ctx->phys.ea>=ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) ctx->FlagCyclic = FALSE;
            }
        }
        if(ctx->NumCyclic>ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]){ 
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
            ctx->NumCyclic = 0;
        }
    }
}

void CDigitShowBasicDoc::Creep()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if( ctx->phys.q>=ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->errTol.StressCom)    {
        SetMotorUp();
    }
    else if( ctx->phys.q<=ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->errTol.StressExt)    {
        SetMotorDown();
    }        
    else {
        SetMotorSpeed(0.0f);
    }
    if(ctx->TotalStepTime>= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
        ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
        ctx->TotalStepTime = 0.0;
    }
}

void CDigitShowBasicDoc::LinearEffectiveStressPath()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][4]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]==ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]){
        SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]- ctx->phys.e_sr)));
        if( ctx->phys.e_sa > ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]+ctx->errTol.StressCom){
            SetMotorUp();
        }
        else if( ctx->phys.e_sa < ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]+ctx->errTol.StressExt){
            SetMotorDown();
        }
        else {
            ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
            ctx->TotalStepTime = 0.0;
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][1] < ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]){
        if( ctx->phys.e_sr >= ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->errTol.StressA) {
            SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]-float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][3])));
        }
        if( ctx->phys.e_sr < ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->errTol.StressA) {
            SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*fabs(ctx->controlFile.Para[ctx->controlFile.CurrentNum][5])/60.0*ctx->timeSettings.Interval2/1000.0));
        }
        if( ctx->phys.e_sa > (ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][0])/(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])*(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])+ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->errTol.StressCom){
            SetMotorUp();
        }
        else if( ctx->phys.e_sa < (ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][0])/(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])*(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])+ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->errTol.StressExt){
            SetMotorDown();
        }
        else {
            SetMotorSpeed(0.0f);
            if(fabs(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) <= ctx->errTol.StressA) {
                ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
                ctx->TotalStepTime = 0.0;
            }
        }
    }
    else if(ctx->controlFile.Para[ctx->controlFile.CurrentNum][1] > ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]){
        if( ctx->phys.e_sr > ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]+ctx->errTol.StressA) {
            SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]-float(ctx->cal.DA_a[ctx->daChannel.EP_Cell]*fabs(ctx->controlFile.Para[ctx->controlFile.CurrentNum][5])/60.0*ctx->timeSettings.Interval2/1000.0));
        }
        if( ctx->phys.e_sr <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]+ctx->errTol.StressA) {
            SetEPCell(ctx->DAVout[ctx->daChannel.EP_Cell]+float(0.2*ctx->cal.DA_a[ctx->daChannel.EP_Cell]*(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]- ctx->phys.e_sr)));
        }
        if( ctx->phys.e_sa > (ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][0])/(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])*(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])+ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->errTol.StressCom){
            SetMotorUp();
        }
        else if( ctx->phys.e_sa < (ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][0])/(ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])*(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][1])+ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->errTol.StressExt){
            SetMotorDown();
        }
        else {
            SetMotorSpeed(0.0f);
            if(fabs(ctx->phys.e_sr-ctx->controlFile.Para[ctx->controlFile.CurrentNum][3]) <= ctx->errTol.StressA){
                ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
                ctx->TotalStepTime = 0.0;
            }
        }
    }
}

void CDigitShowBasicDoc::Creep2()
{
    DigitShowContext* ctx = GetContext();
    ctx->TotalStepTime = ctx->TotalStepTime+ctx->CtrlStepTime/60.0;
    SetMotorOn();
    SetMotorSpeed(float(ctx->cal.DA_a[ctx->daChannel.MotorSpeed]*ctx->controlFile.Para[ctx->controlFile.CurrentNum][0]+ctx->cal.DA_b[ctx->daChannel.MotorSpeed]));
    if( ctx->phys.q <= ctx->controlFile.Para[ctx->controlFile.CurrentNum][1]+ctx->errTol.StressExt)    {
        SetMotorDown();
    }        
    else {
        SetMotorSpeed(0.0f);
    }
    if(ctx->TotalStepTime >= ctx->controlFile.Para[ctx->controlFile.CurrentNum][2]) {
        ctx->controlFile.CurrentNum = ctx->controlFile.CurrentNum+1;
        ctx->TotalStepTime = 0.0;
    }
}
