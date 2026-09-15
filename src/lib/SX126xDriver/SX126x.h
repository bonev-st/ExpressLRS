#pragma once

#include "targets.h"
#include "SX126x_Regs.h"
#include "SX12xxDriverCommon.h"
#include "SX126x_hal.h"

#ifdef PLATFORM_ESP8266
#include <cstdint>
#endif

#define RADIO_SNR_SCALE 4 // Units for LastPacketSNRRaw

class SX126xDriver: public SX12xxDriverCommon
{
public:
    static SX126xDriver *instance;

    ////////////////Configuration Functions/////////////
    SX126xDriver();
    bool Begin(uint32_t minimumFrequency, uint32_t maximumFrequency);
    void End();
    void SetTxIdleMode() { SetMode(SX126X_MODE_FS, SX12XX_Radio_All); }; // set Idle mode used when switching from RX to TX
    void Config(uint8_t bw, uint8_t sf, uint8_t cr, uint32_t freq,
                uint8_t PreambleLength, bool InvertIQ, uint8_t PayloadLength);
    // Frequencies are in Hz. A radio that was receiving keeps receiving on the new frequency
    void SetFrequencyReg(uint32_t freq, SX12XX_Radio_Number_t radioNumber, bool doRx = false);
    void SetOutputPower(int8_t power);
    void startCWTest(uint32_t freq, SX12XX_Radio_Number_t radioNumber);

    bool GetFrequencyErrorbool(SX12XX_Radio_Number_t radioNumber);
    // Both ends use a TCXO, as on the LR1121, so there is no frequency correction
    bool FrequencyErrorAvailable() const { return false; }

    void TXnb(uint8_t * data, bool sendGeminiBuffer, uint8_t * dataGemini, SX12XX_Radio_Number_t radioNumber);
    void RXnb();

    uint16_t GetIrqStatus(SX12XX_Radio_Number_t radioNumber);
    void ClearIrqStatus(uint16_t irqMask, SX12XX_Radio_Number_t radioNumber);

    int8_t GetRssiInst(SX12XX_Radio_Number_t radioNumber);
    void GetLastPacketStats();
    void CheckForSecondPacket();

private:
    // constant used for no power change pending
    // must not be a valid power value
    static constexpr int8_t PWRPENDING_NONE = 0x7f;
    static constexpr uint8_t SX126X_TX_BUFFER_BASE = 0x00;
    static constexpr uint8_t SX126X_RX_BUFFER_BASE = 0x80;

    SX126x_RadioOperatingModes_t currOpmode;
    SX126x_RadioOperatingModes_t fallBackMode;
    int8_t pwrCurrent;
    int8_t pwrPending;
    uint32_t pendingFreq[2]; // Hop that came during a transmission, applied at TX_DONE (0 = none)

    bool DetectChip(SX12XX_Radio_Number_t radioNumber);
    void SetMode(SX126x_RadioOperatingModes_t OPmode, SX12XX_Radio_Number_t radioNumber);
    void SetFIFOaddr(uint8_t txBaseAddr, uint8_t rxBaseAddr);
    void UpdateRegisterBits(uint16_t address, uint8_t mask, uint8_t bits);

    // LoRa functions
    void ConfigModParamsLoRa(uint8_t bw, uint8_t sf, uint8_t cr);
    void SetPacketParamsLoRa(uint8_t PreambleLength, SX126x_RadioLoRaPacketLengthsModes_t HeaderType);

    void SetDioIrqParams(uint16_t irqMask,
                         uint16_t dio1Mask=SX126X_IRQ_RADIO_NONE,
                         uint16_t dio2Mask=SX126X_IRQ_RADIO_NONE,
                         uint16_t dio3Mask=SX126X_IRQ_RADIO_NONE);

    uint8_t GetRxBufferAddr(SX12XX_Radio_Number_t radioNumber);

    static void IsrCallback_1();
    static void IsrCallback_2();
    static void IsrCallback(SX12XX_Radio_Number_t radioNumber);
    bool RXnbISR(SX12XX_Radio_Number_t radioNumber); // ISR for non-blocking RX routine
    void TXnbISR(); // ISR for non-blocking TX routine
    void CommitOutputPower();
};
