/*
  ______                              _
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2016 Semtech

Description: Handling of the node configuration protocol

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis, Gregory Cristian and Matthieu Verdy

Modified and adapted by Alessandro Carcione for ELRS project
*/

#ifndef UNIT_TEST
#include "SX126x_Regs.h"
#include "SX126x_hal.h"
#include <SPIEx.h>
#include "logging.h"

SX126xHal *SX126xHal::instance = NULL;

// The SX126x SPI clock is limited to 16 MHz
static constexpr uint32_t SX126X_SPI_FREQUENCY = 10000000;

SX126xHal::SX126xHal()
{
    instance = this;
}

void SX126xHal::end()
{
    detachInterrupt(GPIO_PIN_DIO1);
    if (GPIO_PIN_DIO1_2 != UNDEF_PIN)
    {
        detachInterrupt(GPIO_PIN_DIO1_2);
    }
    SPIEx.end();
    IsrCallback_1 = nullptr; // remove callbacks
    IsrCallback_2 = nullptr; // remove callbacks
}

void SX126xHal::init()
{
    DBGLN("Hal Init");

    if (GPIO_PIN_BUSY != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_BUSY, INPUT);
    }
    if (GPIO_PIN_BUSY_2 != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_BUSY_2, INPUT);
    }

    pinMode(GPIO_PIN_DIO1, INPUT);
    if (GPIO_PIN_DIO1_2 != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_DIO1_2, INPUT);
    }

    pinMode(GPIO_PIN_NSS, OUTPUT);
    digitalWrite(GPIO_PIN_NSS, HIGH);

#ifdef PLATFORM_ESP32
    SPIEx.begin(GPIO_PIN_SCK, GPIO_PIN_MISO, GPIO_PIN_MOSI, GPIO_PIN_NSS); // sck, miso, mosi, ss (ss can be any GPIO)
    gpio_pullup_en((gpio_num_t)GPIO_PIN_MISO);
    SPIEx.setFrequency(SX126X_SPI_FREQUENCY);
    SPIEx.setHwCs(true);
    if (GPIO_PIN_NSS_2 != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_NSS_2, OUTPUT);
        digitalWrite(GPIO_PIN_NSS_2, HIGH);
        spiAttachSS(SPIEx.bus(), 1, GPIO_PIN_NSS_2);
    }
    spiEnableSSPins(SPIEx.bus(), SX12XX_Radio_All);
#elif defined(PLATFORM_ESP8266)
    DBGLN("PLATFORM_ESP8266");
    SPIEx.begin();
    SPIEx.setHwCs(true);
    SPIEx.setBitOrder(MSBFIRST);
    SPIEx.setDataMode(SPI_MODE0);
    SPIEx.setFrequency(SX126X_SPI_FREQUENCY);
#endif

    attachInterrupt(digitalPinToInterrupt(GPIO_PIN_DIO1), this->dioISR_1, RISING);
    if (GPIO_PIN_DIO1_2 != UNDEF_PIN)
    {
        attachInterrupt(digitalPinToInterrupt(GPIO_PIN_DIO1_2), this->dioISR_2, RISING);
    }
}

void SX126xHal::reset(void)
{
    DBGLN("SX126x Reset");

    if (GPIO_PIN_RST != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_RST, OUTPUT);
        digitalWrite(GPIO_PIN_RST, LOW);
        if (GPIO_PIN_RST_2 != UNDEF_PIN)
        {
            pinMode(GPIO_PIN_RST_2, OUTPUT);
            digitalWrite(GPIO_PIN_RST_2, LOW);
        }
        delay(50);
        digitalWrite(GPIO_PIN_RST, HIGH);
        if (GPIO_PIN_RST_2 != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_RST_2, HIGH);
        }
        delay(50); // Safety buffer. Busy takes longer to go low than the 1ms timeout in WaitOnBusy().
    }

    BusyDelay(10000); // 10ms delay if GPIO_PIN_BUSY is undefined
    WaitOnBusy(SX12XX_Radio_All);

    DBGLN("SX126x Ready!");
}

void ICACHE_RAM_ATTR SX126xHal::WriteCommand(SX126x_RadioCommands_t command, uint8_t val, SX12XX_Radio_Number_t radioNumber, uint32_t busyDelay)
{
    WriteCommand(command, &val, 1, radioNumber, busyDelay);
}

void ICACHE_RAM_ATTR SX126xHal::WriteCommand(SX126x_RadioCommands_t command, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber, uint32_t busyDelay)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 1)] = {
        command,
    };

    if (size)
    {
        memcpy(OutBuffer + 1, buffer, size);
    }

    WaitOnBusy(radioNumber);
    SPIEx.write(radioNumber, OutBuffer, size + 1);

    BusyDelay(busyDelay);
}

uint8_t ICACHE_RAM_ATTR SX126xHal::ReadCommand(SX126x_RadioCommands_t command, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 2)] = {
        (uint8_t)command,
        0x00,
        0x00,
    };

    WaitOnBusy(radioNumber);

    SPIEx.read(radioNumber, OutBuffer, size + 2); // the status comes back on the byte after the opcode, then the data
    memcpy(buffer, OutBuffer + 2, size);
    return OutBuffer[1] & SX126X_STATUS_MASK; // Discard reserved bits
}

void ICACHE_RAM_ATTR SX126xHal::WriteRegister(uint16_t address, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 3)] = {
        SX126X_RADIO_WRITE_REGISTER,
        (uint8_t)((address & 0xFF00) >> 8),
        (uint8_t)(address & 0x00FF),
    };

    memcpy(OutBuffer + 3, buffer, size);

    WaitOnBusy(radioNumber);
    SPIEx.write(radioNumber, OutBuffer, size + 3);

    BusyDelay(15);
}

void ICACHE_RAM_ATTR SX126xHal::WriteRegister(uint16_t address, uint8_t value, SX12XX_Radio_Number_t radioNumber)
{
    WriteRegister(address, &value, 1, radioNumber);
}

void ICACHE_RAM_ATTR SX126xHal::ReadRegister(uint16_t address, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 4)] = {
        SX126X_RADIO_READ_REGISTER,
        (uint8_t)((address & 0xFF00) >> 8),
        (uint8_t)(address & 0x00FF),
        0x00,
    };

    WaitOnBusy(radioNumber);

    SPIEx.read(radioNumber, OutBuffer, size + 4);
    memcpy(buffer, OutBuffer + 4, size);
}

uint8_t ICACHE_RAM_ATTR SX126xHal::ReadRegister(uint16_t address, SX12XX_Radio_Number_t radioNumber)
{
    uint8_t data;
    ReadRegister(address, &data, 1, radioNumber);
    return data;
}

void ICACHE_RAM_ATTR SX126xHal::WriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 2)] = {
        SX126X_RADIO_WRITE_BUFFER,
        offset
    };

    memcpy(OutBuffer + 2, buffer, size);

    WaitOnBusy(radioNumber);

    SPIEx.write(radioNumber, OutBuffer, size + 2);

    BusyDelay(15);
}

void ICACHE_RAM_ATTR SX126xHal::ReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size, SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t OutBuffer[WORD_PADDED(size + 3)] = {
        SX126X_RADIO_READ_BUFFER,
        offset,
        0x00
    };

    WaitOnBusy(radioNumber);

    SPIEx.read(radioNumber, OutBuffer, size + 3);

    memcpy(buffer, OutBuffer + 3, size);
}

bool ICACHE_RAM_ATTR SX126xHal::IsBusy(SX12XX_Radio_Number_t radioNumber)
{
    const bool busy1 = (radioNumber & SX12XX_Radio_1) && digitalRead(GPIO_PIN_BUSY) == HIGH;
    const bool busy2 = (radioNumber & SX12XX_Radio_2) && GPIO_PIN_BUSY_2 != UNDEF_PIN && digitalRead(GPIO_PIN_BUSY_2) == HIGH;
    return busy1 || busy2;
}

bool ICACHE_RAM_ATTR SX126xHal::WaitOnBusy(SX12XX_Radio_Number_t radioNumber)
{
    if (GPIO_PIN_BUSY != UNDEF_PIN)
    {
        constexpr uint32_t wtimeoutUS = 1000U;
        uint32_t startTime = 0;

        // SPIEx::write() returns while its frame is still being clocked out, and BUSY rises only after NSS goes high
        // (up to 600 ns, datasheet 8.3.1). Sampled earlier, BUSY reads low and the next command reaches a busy chip,
        // which drops or garbles it. So wait for the transfer to end, then at least 1 us, then poll BUSY
        SPIEx.waitIdle();
        const uint32_t idleUs = micros();
        while ((micros() - idleUs) < 2)
        {
        }
        while (IsBusy(radioNumber))
        {
            // Use this time to call micros().
            uint32_t now = micros();
            if (startTime == 0) startTime = now;
            if ((now - startTime) > wtimeoutUS)
            {
                DBGLN("SX126x BUSY timeout, radio %u", radioNumber);
                return false;
            }
        }
    }
    else
    {
        uint32_t now = micros();
        while ((now - BusyDelayStart) < BusyDelayDuration)
            now = micros();
        BusyDelayDuration = 0;
    }
    return true;
}

bool SX126xHal::WaitOnBusyLong(SX12XX_Radio_Number_t radioNumber, uint32_t timeoutMs)
{
    if (GPIO_PIN_BUSY == UNDEF_PIN)
    {
        // Nothing to poll, so allow the whole time
        delay(timeoutMs);
        BusyDelayDuration = 0;
        return true;
    }

    // BUSY rises shortly after NSS goes high, and SPIEx::write() returns before the frame has ended. Checked straight
    // after the command, BUSY can still read low; the long operation (calibration, TCXO start) then blocks the next
    // commands, and the chip ignores commands while BUSY is high
    SPIEx.waitIdle();
    delayMicroseconds(10);
    const uint32_t startTime = millis();
    while (IsBusy(radioNumber))
    {
        if ((millis() - startTime) > timeoutMs)
        {
            DBGLN("SX126x BUSY still high after %ums, radio %u", timeoutMs, radioNumber);
            return false;
        }
    }
    return true;
}

void ICACHE_RAM_ATTR SX126xHal::dioISR_1()
{
    if (instance->IsrCallback_1)
        instance->IsrCallback_1();
}

void ICACHE_RAM_ATTR SX126xHal::dioISR_2()
{
    if (instance->IsrCallback_2)
        instance->IsrCallback_2();
}

#endif // UNIT_TEST
