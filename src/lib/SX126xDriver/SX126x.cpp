#include "SX126x_Regs.h"
#include "SX126x_hal.h"
#include "SX126x.h"
#include "logging.h"
#include "RFAMP_hal.h"

SX126xHal hal;
SX126xDriver *SX126xDriver::instance = NULL;

RFAMP_hal RFAMP;

//DEBUG_SX126X_OTA_TIMING

#if defined(DEBUG_SX126X_OTA_TIMING)
static uint32_t beginTX;
static uint32_t endTX;
#endif

static constexpr SX12XX_Radio_Number_t radioList[2] = {SX12XX_Radio_1, SX12XX_Radio_2};

// Commands that return data address one chip at a time, so loop over the fitted radios
static uint8_t radioCount()
{
    return (GPIO_PIN_NSS_2 == UNDEF_PIN) ? 1 : 2;
}

/*
 * The PLL step is 32 MHz / 2^25, so reg = freqHz * 1.048576.
 * 1.048576 * 2^30 rounds to 1125899907: a 32x32->64 multiply and a shift, no division,
 * with under one step (0.95 Hz) of error up to 960 MHz.
 */
static uint32_t ICACHE_RAM_ATTR freqHzToReg(uint32_t freqHz)
{
    return (uint32_t)(((uint64_t)freqHz * 1125899907ULL) >> 30);
}

SX126xDriver::SX126xDriver(): SX12xxDriverCommon()
{
    instance = this;
    currOpmode = SX126X_MODE_SLEEP;
    fallBackMode = SX126X_MODE_FS;
    strongestReceivingRadio = SX12XX_Radio_1;
    pwrCurrent = PWRPENDING_NONE;
    pwrPending = PWRPENDING_NONE;
    pendingFreq[0] = pendingFreq[1] = 0;
    currFreq = 915000000;
    PayloadLength = 8; // Dummy default value which is overwritten during setup.
}

void SX126xDriver::End()
{
    if (currOpmode != SX126X_MODE_SLEEP)
    {
        SetMode(SX126X_MODE_SLEEP, SX12XX_Radio_All);
    }
    hal.end();
    RFAMP.TXRXdisable();
    RemoveCallbacks();
    currFreq = 915000000;
    PayloadLength = 8; // Dummy default value which is overwritten during setup.
}

bool SX126xDriver::DetectChip(SX12XX_Radio_Number_t radioNumber)
{
    // The LoRa sync word registers read 0x1424 after reset; with no chip the bus reads 0x00 or 0xFF
    uint8_t syncWord[2] = {0};
    hal.ReadRegister(SX126X_REG_LORA_SYNC_WORD_MSB, syncWord, sizeof(syncWord), radioNumber);
    if (syncWord[0] != SX126X_LORA_SYNC_WORD_MSB || syncWord[1] != SX126X_LORA_SYNC_WORD_LSB)
    {
        DBGLN("SX126x #%u not found, sync word reads 0x%x 0x%x", radioNumber, syncWord[0], syncWord[1]);
        return false;
    }
    DBGLN("SX126x #%u found", radioNumber);
    return true;
}

bool SX126xDriver::Begin(uint32_t minimumFrequency, uint32_t maximumFrequency)
{
    hal.init();
    hal.IsrCallback_1 = &SX126xDriver::IsrCallback_1;
    hal.IsrCallback_2 = &SX126xDriver::IsrCallback_2;

    hal.reset();
    DBGLN("SX126x Begin");

    RFAMP.init();

    SetMode(SX126X_MODE_STDBY_RC, SX12XX_Radio_All);

    for (uint8_t i = 0; i < radioCount(); i++)
    {
        if (!DetectChip(radioList[i]))
        {
            return false;
        }
    }

    // Calibration and every oscillator start below wait for the TCXO, so its delay sets the long BUSY timeout
    uint32_t tcxoDelay = 0;
    if (hardware_int(HARDWARE_radio_tcxo) != -1)
    {
        tcxoDelay = (hardware_int(HARDWARE_radio_tcxo_delay) != -1) ? hardware_int(HARDWARE_radio_tcxo_delay) : SX126X_TCXO_DELAY_DEFAULT;
        uint8_t tcxoMode[4] = {(uint8_t)hardware_int(HARDWARE_radio_tcxo), (uint8_t)(tcxoDelay >> 16), (uint8_t)(tcxoDelay >> 8), (uint8_t)tcxoDelay};
        hal.WriteCommand(SX126X_RADIO_SET_TCXOMODE, tcxoMode, sizeof(tcxoMode), SX12XX_Radio_All);
        DBGLN("SX126x TCXO voltage %u, delay %u", tcxoMode[0], tcxoDelay);
    }
    const uint32_t busyTimeoutMs = tcxoDelay / 64 + 10; // 15.625 us steps to ms, plus margin for ~3.5 ms of calibration

    // Clear the XOSC start error set at power-up (the TCXO was not powered yet), then calibrate every block
    uint8_t clearErrors[2] = {0x00, 0x00};
    hal.WriteCommand(SX126X_RADIO_CLR_ERROR, clearErrors, sizeof(clearErrors), SX12XX_Radio_All);
    hal.WriteCommand(SX126X_RADIO_CALIBRATE, SX126X_CALIBRATE_ALL, SX12XX_Radio_All);
    hal.WaitOnBusyLong(SX12XX_Radio_All, busyTimeoutMs);

    if (OPT_USE_HARDWARE_DCDC)
    {
        DBGLN("Enabling DCDC regulator");
        hal.WriteCommand(SX126X_RADIO_SET_REGULATORMODE, SX126X_USE_DCDC, SX12XX_Radio_All); // Enable DCDC converter instead of LDO
    }

    // Modules such as the Wio-SX1262 have no TX path unless DIO2 drives the RF switch
    const bool dio2RfSwitch = hardware_flag(HARDWARE_radio_dio2_rfsw);
    if (dio2RfSwitch)
    {
        hal.WriteCommand(SX126X_RADIO_SET_RFSWITCHMODE, 0x01, SX12XX_Radio_All);
    }
    DBGLN("SX126x DIO2 RF switch %s", dio2RfSwitch ? "on" : "off");

    // LoRa is the only packet type, so Config() does not set it again
    hal.WriteCommand(SX126X_RADIO_SET_PACKETTYPE, SX126X_PACKET_TYPE_LORA, SX12XX_Radio_All);

    // SetPaConfig resets the current limit, so OCP must follow it
    uint8_t paConfig[4] = {SX126X_PA_DUTY_CYCLE_22DBM, SX126X_PA_HP_MAX_22DBM, SX126X_PA_DEVICE_SX1262, SX126X_PA_LUT};
    hal.WriteCommand(SX126X_RADIO_SET_PACONFIG, paConfig, sizeof(paConfig), SX12XX_Radio_All);
    hal.WriteRegister(SX126X_REG_OCP_CONFIGURATION, SX126X_OCP_140_MA, SX12XX_Radio_All);
    // Errata 15.2: PA clamping for antenna mismatch
    UpdateRegisterBits(SX126X_REG_TX_CLAMP_CONFIG, SX126X_TX_CLAMP_CONFIG_BITS, SX126X_TX_CLAMP_CONFIG_BITS);
    hal.WriteRegister(SX126X_REG_RX_GAIN, SX126X_RX_GAIN_BOOSTED, SX12XX_Radio_All);
    uint8_t syncWord[2] = {SX126X_LORA_SYNC_WORD_MSB, SX126X_LORA_SYNC_WORD_LSB};
    hal.WriteRegister(SX126X_REG_LORA_SYNC_WORD_MSB, syncWord, sizeof(syncWord), SX12XX_Radio_All);

    // Image calibration over the regulatory domain, rounded outwards as on the LR1121
    uint8_t calImage[2];
    calImage[0] = ((minimumFrequency / 1000000) - 1) / 4;       // Freq1 = floor( (fmin_mhz - 1)/4)
    calImage[1] = 1 + ((maximumFrequency / 1000000) + 1) / 4;   // Freq2 = ceil( (fmax_mhz + 1)/4)
    hal.WriteCommand(SX126X_RADIO_CALIBRATEIMAGE, calImage, sizeof(calImage), SX12XX_Radio_All);
    hal.WaitOnBusyLong(SX12XX_Radio_All, busyTimeoutMs);

    /*
     * Never fall back to STDBY_RC: it turns the TCXO off, and the next TX would wait for its start delay.
     * Dual radio TX falls back to STDBY_XOSC rather than FS, as the SX1280 driver avoids AutoFS there: when
     * telemetry reaches only 1 of the 2 radios, the radios are left in different modes, BUSY after the next
     * SetTx lasts differently for each, and the 2 TX_DONE IRQs come at very different times.
     */
#if defined(TARGET_TX)
    fallBackMode = (GPIO_PIN_NSS_2 == UNDEF_PIN) ? SX126X_MODE_FS : SX126X_MODE_STDBY_XOSC;
#else
    fallBackMode = SX126X_MODE_FS;
#endif
    hal.WriteCommand(SX126X_RADIO_SET_RXTXFALLBACKMODE,
                     (fallBackMode == SX126X_MODE_FS) ? SX126X_FALLBACK_FS : SX126X_FALLBACK_STDBY_XOSC,
                     SX12XX_Radio_All);

    SetDioIrqParams(SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE, SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE);
    SetFIFOaddr(SX126X_TX_BUFFER_BASE, SX126X_RX_BUFFER_BASE);

    // POWERMGNT sets the configured power later. RX telemetry keeps this one if the layout has no power_values
    pwrCurrent = PWRPENDING_NONE;
    SetOutputPower(14);
    CommitOutputPower();

    for (uint8_t i = 0; i < radioCount(); i++)
    {
        uint8_t errors[2] = {0};
        hal.ReadCommand(SX126X_RADIO_GET_ERROR, errors, sizeof(errors), radioList[i]);
        DBGLN("SX126x #%u device errors 0x%x", radioList[i], (errors[0] << 8) | errors[1]);
    }

    // Keep the TCXO running from here on: nothing after Begin() returns to STDBY_RC
    SetMode(SX126X_MODE_STDBY_XOSC, SX12XX_Radio_All);
    hal.WaitOnBusyLong(SX12XX_Radio_All, busyTimeoutMs);

    return true;
}

void SX126xDriver::startCWTest(uint32_t freq, SX12XX_Radio_Number_t radioNumber)
{
    SetFrequencyReg(freq, radioNumber);
    CommitOutputPower();
    RFAMP.TXenable(radioNumber);
    hal.WriteCommand(SX126X_RADIO_SET_TXCONTINUOUSWAVE, nullptr, 0, radioNumber);
    hal.WaitOnBusyLong(radioNumber, 10);
}

void SX126xDriver::Config(uint8_t bw, uint8_t sf, uint8_t cr, uint32_t freq,
                          uint8_t PreambleLength, bool InvertIQ, uint8_t _PayloadLength)
{
    PayloadLength = _PayloadLength;
    // InvertIQ is ignored: standard IQ below 1 GHz, as SX127x ignores it and the LR1121 forces standard on sub-GHz
    IQinverted = false;

    // STDBY_XOSC, never STDBY_RC, which would turn off the TCXO
    SetMode(SX126X_MODE_STDBY_XOSC, SX12XX_Radio_All);
    ConfigModParamsLoRa(bw, sf, cr);
#if defined(DEBUG_FREQ_CORRECTION)
    SetPacketParamsLoRa(PreambleLength, SX126X_LORA_PACKET_EXPLICIT);
#else
    SetPacketParamsLoRa(PreambleLength, SX126X_LORA_PACKET_IMPLICIT);
#endif
    pendingFreq[0] = pendingFreq[1] = 0;
    SetFrequencyReg(freq, SX12XX_Radio_All, false);
    ClearIrqStatus(SX126X_IRQ_RADIO_ALL, SX12XX_Radio_All);
}

/***
 * @brief: Schedule an output power change after the next transmit
 ***/
void SX126xDriver::SetOutputPower(int8_t power)
{
    const int8_t pwrNew = constrain(power, SX126X_POWER_MIN, SX126X_POWER_MAX);

    if (pwrNew != pwrCurrent || pwrPending != PWRPENDING_NONE)
    {
        pwrPending = pwrNew;
        DBGLN("SetPower: %d", pwrPending);
    }
}

void ICACHE_RAM_ATTR SX126xDriver::CommitOutputPower()
{
    if (pwrPending == PWRPENDING_NONE)
        return;

    pwrCurrent = pwrPending;
    pwrPending = PWRPENDING_NONE;
    uint8_t buf[2] = { (uint8_t)pwrCurrent, (uint8_t)SX126X_RADIO_RAMP_40_US };
    hal.WriteCommand(SX126X_RADIO_SET_TXPARAMS, buf, sizeof(buf), SX12XX_Radio_All);
}

void ICACHE_RAM_ATTR SX126xDriver::SetMode(SX126x_RadioOperatingModes_t OPmode, SX12XX_Radio_Number_t radioNumber)
{
    /*
    Comment out since it is difficult to keep track of dual radios.
    When checking SPI it is also useful to see every possible SPI transaction to make sure it fits when required.
    */
    // if (OPmode == currOpmode)
    // {
    //    return;
    // }

    WORD_ALIGNED_ATTR uint8_t buf[3];

    switch (OPmode)
    {

    case SX126X_MODE_SLEEP:
        hal.WriteCommand(SX126X_RADIO_SET_SLEEP, SX126X_SLEEP_COLD_START, radioNumber);
        break;

    case SX126X_MODE_STDBY_RC:
        hal.WriteCommand(SX126X_RADIO_SET_STANDBY, SX126X_STDBY_RC, radioNumber, 1500);
        break;

    // The DC-DC supply regulation is automatically powered in STDBY_XOSC mode.
    case SX126X_MODE_STDBY_XOSC:
        hal.WriteCommand(SX126X_RADIO_SET_STANDBY, SX126X_STDBY_XOSC, radioNumber, 50);
        break;

    case SX126X_MODE_FS:
        hal.WriteCommand(SX126X_RADIO_SET_FS, nullptr, 0, radioNumber, 70);
        break;

    case SX126X_MODE_RX_CONT:
        buf[0] = (uint8_t)(SX126X_RX_TIMEOUT_CONTINUOUS >> 16);
        buf[1] = (uint8_t)(SX126X_RX_TIMEOUT_CONTINUOUS >> 8);
        buf[2] = (uint8_t)SX126X_RX_TIMEOUT_CONTINUOUS;
        hal.WriteCommand(SX126X_RADIO_SET_RX, buf, sizeof(buf), radioNumber, 100);
        break;

    case SX126X_MODE_TX:
        buf[0] = (uint8_t)(SX126X_TX_TIMEOUT_NONE >> 16);
        buf[1] = (uint8_t)(SX126X_TX_TIMEOUT_NONE >> 8);
        buf[2] = (uint8_t)SX126X_TX_TIMEOUT_NONE;
        hal.WriteCommand(SX126X_RADIO_SET_TX, buf, sizeof(buf), radioNumber, 100);
        break;

    default:
        break;
    }

    currOpmode = OPmode;
}

void SX126xDriver::UpdateRegisterBits(uint16_t address, uint8_t mask, uint8_t bits)
{
    for (uint8_t i = 0; i < radioCount(); i++)
    {
        const uint8_t value = hal.ReadRegister(address, radioList[i]);
        hal.WriteRegister(address, (uint8_t)((value & ~mask) | bits), radioList[i]);
    }
}

void SX126xDriver::ConfigModParamsLoRa(uint8_t bw, uint8_t sf, uint8_t cr)
{
    // Only the first 4 bytes are used for LoRa
    WORD_ALIGNED_ATTR uint8_t rfparams[8] = {sf, bw, cr, SX126X_LORA_LDRO_OFF, 0x00, 0x00, 0x00, 0x00};

    hal.WriteCommand(SX126X_RADIO_SET_MODULATIONPARAMS, rfparams, sizeof(rfparams), SX12XX_Radio_All, 25);

    // Errata 15.1: TX modulation quality with BW500
    UpdateRegisterBits(SX126X_REG_TX_MODULATION, SX126X_TX_MODULATION_BW500_BIT,
                       (bw == SX126X_LORA_BW_500) ? 0x00 : SX126X_TX_MODULATION_BW500_BIT);
}

void SX126xDriver::SetPacketParamsLoRa(uint8_t PreambleLength, SX126x_RadioLoRaPacketLengthsModes_t HeaderType)
{
    uint8_t buf[6];

    buf[0] = 0x00; // Preamble length MSB
    buf[1] = PreambleLength;
    buf[2] = HeaderType;
    buf[3] = PayloadLength;
    buf[4] = SX126X_LORA_CRC_OFF;
    buf[5] = SX126X_LORA_IQ_STANDARD;

    hal.WriteCommand(SX126X_RADIO_SET_PACKETPARAMS, buf, sizeof(buf), SX12XX_Radio_All, 20);

    // Errata 15.4: standard IQ needs bit 2 set
    UpdateRegisterBits(SX126X_REG_IQ_POLARITY, SX126X_IQ_POLARITY_STANDARD_BIT, SX126X_IQ_POLARITY_STANDARD_BIT);
}

void ICACHE_RAM_ATTR SX126xDriver::SetFrequencyReg(uint32_t freq, SX12XX_Radio_Number_t radioNumber, bool doRx)
{
    // Normally no hop comes during a transmission: the TX hops after TX_DONE, the RX before it sends telemetry.
    // One comes only when a TX_DONE is lost. Retuning then would abort the packet (STDBY_XOSC) or leave the PLL on
    // the old frequency, so keep the hop until TX_DONE or the next TXnb() timeout, where TXnbISR() applies it
    if (currOpmode == SX126X_MODE_TX)
    {
        for (uint8_t i = 0; i < 2; i++)
        {
            if (radioNumber & radioList[i])
            {
                pendingFreq[i] = freq;
            }
        }
        currFreq = freq;
        return;
    }

    // Checked before anything changes currOpmode, so the second per-radio call in Gemini mode also sees RX
    const bool wasRx = currOpmode == SX126X_MODE_RX_CONT;
    // Change the frequency from STDBY_XOSC: in FS (the fallback after TX) or RX the PLL stays locked to the old
    // frequency, and the link lost every packet after the first hop. The next SetTx or SetRx locks the PLL again
    if (wasRx || currOpmode == SX126X_MODE_FS)
    {
        SetMode(SX126X_MODE_STDBY_XOSC, radioNumber);
    }

    const uint32_t regfreq = freqHzToReg(freq);
    WORD_ALIGNED_ATTR uint8_t buf[4] = {
        (uint8_t)(regfreq >> 24),
        (uint8_t)(regfreq >> 16),
        (uint8_t)(regfreq >> 8),
        (uint8_t)regfreq,
    };
    hal.WriteCommand(SX126X_RADIO_SET_RFFREQUENCY, buf, sizeof(buf), radioNumber);

    currFreq = freq;

    // Keep receiving across a frequency change, like the SX1280 and LR1121. After TX the fallback mode is already FS
    if (doRx || wasRx)
    {
        RFAMP.RXenable();
        SetMode(SX126X_MODE_RX_CONT, radioNumber);
    }
}

void SX126xDriver::SetFIFOaddr(uint8_t txBaseAddr, uint8_t rxBaseAddr)
{
    uint8_t buf[2];

    buf[0] = txBaseAddr;
    buf[1] = rxBaseAddr;
    hal.WriteCommand(SX126X_RADIO_SET_BUFFERBASEADDRESS, buf, sizeof(buf), SX12XX_Radio_All);
}

void SX126xDriver::SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask)
{
    uint8_t buf[8];

    buf[0] = (uint8_t)((irqMask >> 8) & 0x00FF);
    buf[1] = (uint8_t)(irqMask & 0x00FF);
    buf[2] = (uint8_t)((dio1Mask >> 8) & 0x00FF);
    buf[3] = (uint8_t)(dio1Mask & 0x00FF);
    buf[4] = (uint8_t)((dio2Mask >> 8) & 0x00FF);
    buf[5] = (uint8_t)(dio2Mask & 0x00FF);
    buf[6] = (uint8_t)((dio3Mask >> 8) & 0x00FF);
    buf[7] = (uint8_t)(dio3Mask & 0x00FF);

    hal.WriteCommand(SX126X_RADIO_CFG_DIOIRQ, buf, sizeof(buf), SX12XX_Radio_All);
}

uint16_t ICACHE_RAM_ATTR SX126xDriver::GetIrqStatus(SX12XX_Radio_Number_t radioNumber)
{
    uint8_t status[2];

    hal.ReadCommand(SX126X_RADIO_GET_IRQSTATUS, status, 2, radioNumber);
    return status[0] << 8 | status[1];
}

void ICACHE_RAM_ATTR SX126xDriver::ClearIrqStatus(uint16_t irqMask, SX12XX_Radio_Number_t radioNumber)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(((uint16_t)irqMask >> 8) & 0x00FF);
    buf[1] = (uint8_t)((uint16_t)irqMask & 0x00FF);

    hal.WriteCommand(SX126X_RADIO_CLR_IRQSTATUS, buf, sizeof(buf), radioNumber);
}

void ICACHE_RAM_ATTR SX126xDriver::TXnbISR()
{
    currOpmode = fallBackMode; // the radio falls back after TX
    // Apply a hop that came during the transmission (see SetFrequencyReg)
    for (uint8_t i = 0; i < radioCount(); i++)
    {
        if (pendingFreq[i] != 0)
        {
            const uint32_t freq = pendingFreq[i];
            pendingFreq[i] = 0;
            SetFrequencyReg(freq, radioList[i], false);
        }
    }
#ifdef DEBUG_SX126X_OTA_TIMING
    endTX = micros();
    DBGLN("TOA: %d", endTX - beginTX);
#endif
    CommitOutputPower();
    TXdoneCallback();
}

void ICACHE_RAM_ATTR SX126xDriver::TXnb(uint8_t * data, bool sendGeminiBuffer, uint8_t * dataGemini, SX12XX_Radio_Number_t radioNumber)
{
    transmittingRadio = radioNumber;

    //catch TX timeout
    if (currOpmode == SX126X_MODE_TX)
    {
        SetMode(fallBackMode, SX12XX_Radio_All);
        ClearIrqStatus(SX126X_IRQ_RADIO_ALL, SX12XX_Radio_All);
        TXnbISR();
        return;
    }

    if (radioNumber == SX12XX_Radio_NONE)
    {
        instance->SetMode(fallBackMode, SX12XX_Radio_All);
        return;
    }

    // Leave RX first: the RX telemetry slot, or the TX that follows a telemetry RX window
    if (currOpmode == SX126X_MODE_RX_CONT)
    {
        SetMode(SX126X_MODE_FS, SX12XX_Radio_All);
    }

#if defined(DEBUG_RCVR_SIGNAL_STATS)
    if (radioNumber == SX12XX_Radio_All || radioNumber == SX12XX_Radio_1)
    {
        instance->rxSignalStats[0].telem_count++;
    }
    if (radioNumber == SX12XX_Radio_All || radioNumber == SX12XX_Radio_2)
    {
        instance->rxSignalStats[1].telem_count++;
    }
#endif

    // Normal diversity mode
    if (GPIO_PIN_NSS_2 != UNDEF_PIN && radioNumber != SX12XX_Radio_All)
    {
        // Make sure the unused radio is in FS mode and will not receive the tx packet.
        if (radioNumber == SX12XX_Radio_1)
        {
            instance->SetMode(fallBackMode, SX12XX_Radio_2);
        }
        else
        {
            instance->SetMode(fallBackMode, SX12XX_Radio_1);
        }
    }

    RFAMP.TXenable(radioNumber); // do first to allow PA stablise
    if (sendGeminiBuffer)
    {
        hal.WriteBuffer(SX126X_TX_BUFFER_BASE, data, PayloadLength, SX12XX_Radio_1);
        hal.WriteBuffer(SX126X_TX_BUFFER_BASE, dataGemini, PayloadLength, SX12XX_Radio_2);
    }
    else
    {
        hal.WriteBuffer(SX126X_TX_BUFFER_BASE, data, PayloadLength, radioNumber);
    }

    instance->SetMode(SX126X_MODE_TX, radioNumber);

#ifdef DEBUG_SX126X_OTA_TIMING
    beginTX = micros();
#endif
}

bool ICACHE_RAM_ATTR SX126xDriver::RXnbISR(SX12XX_Radio_Number_t radioNumber)
{
    // The radio CRC is off, ELRS checks its own OTA CRC in the callback
    const uint8_t FIFOaddr = GetRxBufferAddr(radioNumber);
    hal.ReadBuffer(FIFOaddr, RXdataBuffer, PayloadLength, radioNumber);
    return RXdoneCallback(SX12XX_RX_OK);
}

void ICACHE_RAM_ATTR SX126xDriver::RXnb()
{
    RFAMP.RXenable();
    SetMode(SX126X_MODE_RX_CONT, SX12XX_Radio_All);
}

uint8_t ICACHE_RAM_ATTR SX126xDriver::GetRxBufferAddr(SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t status[2] = {0}; // PayloadLengthRx, RxStartBufferPointer
    // Packets are accepted on RX_DONE alone, so the chip status is not checked. No logging here: this runs
    // in the DIO1 ISR, and the USB CDC log stream is not ISR-safe
    hal.ReadCommand(SX126X_RADIO_GET_RXBUFFERSTATUS, status, 2, radioNumber);

    return status[1];
}

bool ICACHE_RAM_ATTR SX126xDriver::GetFrequencyErrorbool(SX12XX_Radio_Number_t radioNumber)
{
    // Only the sign of the 20-bit estimate is needed. The sign convention is unverified; nothing calls this
    // while FrequencyErrorAvailable() returns false
    const uint8_t feiMsb = hal.ReadRegister(SX126X_REG_FREQ_ERROR, radioNumber);
    return (feiMsb & 0x08) == 0;
}

int8_t ICACHE_RAM_ATTR SX126xDriver::GetRssiInst(SX12XX_Radio_Number_t radioNumber)
{
    uint8_t rssiInst = 0;

    hal.ReadCommand(SX126X_RADIO_GET_RSSIINST, &rssiInst, 1, radioNumber);
    return -(int8_t)(rssiInst / 2);
}

void ICACHE_RAM_ATTR SX126xDriver::CheckForSecondPacket()
{
    SX12XX_Radio_Number_t radio[2] = {SX12XX_Radio_1, SX12XX_Radio_2};
    uint8_t processingRadioIdx = (instance->processingPacketRadio == SX12XX_Radio_1) ? 0 : 1;
    uint8_t secondRadioIdx = !processingRadioIdx;

    // processingRadio always passed the sanity check here
    gotRadio[processingRadioIdx] = true;
    gotRadio[secondRadioIdx] = false;

    hasSecondRadioGotData = false;

    if (GPIO_PIN_NSS_2 != UNDEF_PIN)
    {
        uint16_t secondIrqStatus = instance->GetIrqStatus(radio[secondRadioIdx]);
        if(secondIrqStatus & SX126X_IRQ_RX_DONE)
        {
            const uint8_t FIFOaddr = GetRxBufferAddr(radio[secondRadioIdx]);
            hal.ReadBuffer(FIFOaddr, RXdataBufferSecond, PayloadLength, radio[secondRadioIdx]);
            hasSecondRadioGotData = true;
        }
    }
}

void ICACHE_RAM_ATTR SX126xDriver::GetLastPacketStats()
{
    SX12XX_Radio_Number_t radio[2] = {SX12XX_Radio_1, SX12XX_Radio_2};
    uint8_t processingRadioIdx = (instance->processingPacketRadio == SX12XX_Radio_1) ? 0 : 1;
    uint8_t secondRadioIdx = !processingRadioIdx;

    uint8_t status[3]; // RssiPkt, SnrPkt, SignalRssiPkt
    int8_t rssi[2];
    int8_t snr[2];

    gotRadio[secondRadioIdx] = hasSecondRadioGotData;
    #if defined(DEBUG_RCVR_SIGNAL_STATS)
    if(!hasSecondRadioGotData)
    {
        instance->rxSignalStats[secondRadioIdx].fail_count++;
    }
    #endif

    for(uint8_t i=0;i<2;i++)
    {
        if (gotRadio[i])
        {
            hal.ReadCommand(SX126X_RADIO_GET_PACKETSTATUS, status, sizeof(status), radio[i]);

            // RSSI averaged over the packet is -RssiPkt/2 dBm, SNR is in 0.25 dB steps
            rssi[i] = -(int8_t)(status[0] / 2);
            snr[i] = (int8_t)status[1];

            // As on the SX127x, subtract the SNR from the RSSI when the SNR is negative
            int8_t negOffset = (snr[i] < 0) ? (snr[i] / RADIO_SNR_SCALE) : 0;
            rssi[i] += negOffset;

            // If radio # is 0, update LastPacketRSSI, otherwise LastPacketRSSI2
            (i == 0) ? LastPacketRSSI = rssi[i] : LastPacketRSSI2 = rssi[i];
            // Update whatever SNRs we have
            LastPacketSNRRaw = snr[i];
        }
    }

    // by default, set the strongest receiving radio to be the current processing radio (which got a successful packet)
    instance->strongestReceivingRadio = instance->processingPacketRadio;

    // when both radio got the packet, use the better RSSI one
    if(gotRadio[0] && gotRadio[1])
    {
        LastPacketSNRRaw = instance->fuzzy_snr(snr[0], snr[1], instance->FuzzySNRThreshold);
        // Update the strongest receiving radio to be the one with better signal strength
        instance->strongestReceivingRadio = (rssi[0]>rssi[1])? radio[0]: radio[1];
    }

#if defined(DEBUG_RCVR_SIGNAL_STATS)
    // stat updates
    for (uint8_t i = 0; i < 2; i++)
    {
        if (gotRadio[i])
        {
            instance->rxSignalStats[i].irq_count++;
            instance->rxSignalStats[i].rssi_sum += rssi[i];
            instance->rxSignalStats[i].snr_sum += snr[i];
            if (snr[i] > instance->rxSignalStats[i].snr_max)
            {
                instance->rxSignalStats[i].snr_max = snr[i];
            }
            LastPacketSNRRaw = snr[i];
        }
    }
    if(gotRadio[0] || gotRadio[1])
    {
        instance->irq_count_or++;
    }
    if(gotRadio[0] && gotRadio[1])
    {
        instance->irq_count_both++;
    }
#endif
}

void ICACHE_RAM_ATTR SX126xDriver::IsrCallback_1()
{
    instance->IsrCallback(SX12XX_Radio_1);
}

void ICACHE_RAM_ATTR SX126xDriver::IsrCallback_2()
{
    instance->IsrCallback(SX12XX_Radio_2);
}

void ICACHE_RAM_ATTR SX126xDriver::IsrCallback(SX12XX_Radio_Number_t radioNumber)
{
    instance->processingPacketRadio = radioNumber;
    SX12XX_Radio_Number_t irqClearRadio = radioNumber;

    uint16_t irqStatus = instance->GetIrqStatus(radioNumber);
    if (irqStatus & SX126X_IRQ_TX_DONE)
    {
        RFAMP.TXRXdisable();
        instance->TXnbISR();
        irqClearRadio = SX12XX_Radio_All;
    }
    else if (irqStatus & SX126X_IRQ_RX_DONE)
    {
        if (instance->RXnbISR(radioNumber))
        {
            irqClearRadio = SX12XX_Radio_All; // Packet received so clear all radios and dont spend extra time retrieving data.
        }
#if defined(DEBUG_RCVR_SIGNAL_STATS)
        else
        {
            instance->rxSignalStats[(radioNumber == SX12XX_Radio_1) ? 0 : 1].fail_count++;
        }
#endif
    }
    else if (irqStatus == SX126X_IRQ_RADIO_NONE)
    {
        return;
    }
    instance->ClearIrqStatus(SX126X_IRQ_RADIO_ALL, irqClearRadio);
}
