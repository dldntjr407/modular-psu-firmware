/* / mcu / sound.h
 * EEZ Modular Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <eez/firmware.h>
#include <eez/system.h>
#include <eez/sound.h>
#include <eez/index.h>

#include <eez/scpi/scpi.h>

#include <eez/modules/psu/psu.h>
#include <eez/modules/psu/serial_psu.h>
#if OPTION_ETHERNET
#include <eez/modules/psu/ethernet.h>
#include <eez/modules/psu/ntp.h>
#endif
#include <eez/modules/psu/board.h>
#include <eez/modules/psu/datetime.h>
#include <eez/modules/psu/persist_conf.h>
#include <eez/modules/psu/rtc.h>
#include <eez/modules/psu/temperature.h>
#include <eez/modules/psu/calibration.h>
#include <eez/modules/psu/profile.h>
#include <eez/modules/psu/dlog_record.h>
#include <eez/modules/psu/sd_card.h>
#include <eez/modules/psu/channel_dispatcher.h>
#include <eez/modules/psu/event_queue.h>
#include <eez/modules/psu/idle.h>
#include <eez/modules/psu/io_pins.h>
#include <eez/modules/psu/list_program.h>
#include <eez/modules/psu/trigger.h>
#include <eez/modules/psu/ontime.h>

#if OPTION_DISPLAY
#include <eez/modules/psu/gui/psu.h>
#endif

#if OPTION_FAN
#include <eez/modules/aux_ps/fan.h>
#endif

#include <eez/modules/mcu/battery.h>
#include <eez/modules/mcu/eeprom.h>

#include <eez/modules/dcpX05/ioexp.h>
#include <eez/modules/dcpX05/dac.h>
#include <eez/modules/dcpX05/adc.h>

#include <eez/modules/bp3c/io_exp.h>
#include <eez/modules/bp3c/eeprom.h>

#if defined(EEZ_PLATFORM_SIMULATOR)

// for home directory (see getConfFilePath)
#ifdef _WIN32
#undef INPUT
#undef OUTPUT
#include <Shlobj.h>
#include <Windows.h>
#include <direct.h>
#else
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#endif

namespace eez {

using namespace scpi;

TestResult g_masterTestResult;

#if defined(EEZ_PLATFORM_SIMULATOR)

char *getConfFilePath(const char *file_name) {
    static char file_path[1024];

    *file_path = 0;

#ifdef _WIN32
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, file_path))) {
        strcat(file_path, "\\.eez_psu_sim");
        _mkdir(file_path);
        strcat(file_path, "\\");
    }
#elif defined(__EMSCRIPTEN__)
    strcat(file_path, "/eez_modular_firmware/");
#else
    const char *home_dir = 0;
    if ((home_dir = getenv("HOME")) == NULL) {
        home_dir = getpwuid(getuid())->pw_dir;
    }
    if (home_dir) {
        strcat(file_path, home_dir);
        strcat(file_path, "/.eez_psu_sim");
        mkdir(file_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        strcat(file_path, "/");
    }
#endif

    char *q = file_path + strlen(file_path);
    const char *p = file_name;
    while (*p) {
        char ch = *p++;
#ifdef _WIN32
        if (ch == '/')
            *q++ = '\\';
#else
        if (ch == '\\')
            *q++ = '/';
#endif
        else
            *q++ = ch;
    }
    *q = 0;

    return file_path;
}

#endif

void generateError(int16_t error) {
    eez::scpi::generateError(error);
}

namespace psu {

using namespace scpi;

////////////////////////////////////////////////////////////////////////////////

void mainLoop(const void *);

#if defined(EEZ_PLATFORM_STM32)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

osThreadDef(g_psuTask, mainLoop, osPriorityAboveNormal, 0, 2048);

#if defined(EEZ_PLATFORM_STM32)
#pragma GCC diagnostic pop
#endif

osThreadId g_psuTaskHandle;

#if defined(EEZ_PLATFORM_STM32)
#define PSU_QUEUE_SIZE 10
#endif

#if defined(EEZ_PLATFORM_SIMULATOR)
#define PSU_QUEUE_SIZE 100
#endif

osMessageQDef(g_psuMessageQueue, PSU_QUEUE_SIZE, uint32_t);
osMessageQId g_psuMessageQueueId;

////////////////////////////////////////////////////////////////////////////////

static bool g_powerIsUp;
static bool g_testPowerUpDelay;
static uint32_t g_powerDownTime;

static MaxCurrentLimitCause g_maxCurrentLimitCause;

RLState g_rlState = RL_STATE_LOCAL;

bool g_rprogAlarm = false;

void (*g_diagCallback)();

////////////////////////////////////////////////////////////////////////////////

void startThread() {
    g_psuMessageQueueId = osMessageCreate(osMessageQ(g_psuMessageQueue), NULL);
    g_psuTaskHandle = osThreadCreate(osThread(g_psuTask), nullptr);
}

void oneIter();

void mainLoop(const void *) {
#ifdef __EMSCRIPTEN__
    oneIter();
#else
    while (1) {
        oneIter();
    }
#endif
}

bool g_adcMeasureAllFinished = false;

void oneIter() {
    osEvent event = osMessageGet(g_psuMessageQueueId, 1);
    if (event.status == osEventMessage) {
    	uint32_t message = event.value.v;
    	uint32_t type = PSU_QUEUE_MESSAGE_TYPE(message);
    	uint32_t param = PSU_QUEUE_MESSAGE_PARAM(message);
        if (type == PSU_QUEUE_MESSAGE_TYPE_CHANGE_POWER_STATE) {
            changePowerState(param ? true : false);
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_RESET) {
            reset();
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_TEST) {
            test();
        } else if (type == PSU_QUEUE_MESSAGE_SPI_IRQ) {
            auto channelInterface = eez::psu::Channel::getBySlotIndex(param).channelInterface;
            if (channelInterface) {
            	channelInterface->onSpiIrq();
            }
        } else if (type == PSU_QUEUE_MESSAGE_ADC_MEASURE_ALL) {
            eez::psu::Channel::get(param).adcMeasureAll();
            g_adcMeasureAllFinished = true;
        } else if (type == PSU_QUEUE_TRIGGER_START_IMMEDIATELY) {
            trigger::startImmediatelyInPsuThread();
        } else if (type == PSU_QUEUE_TRIGGER_ABORT) {
            trigger::abort();
        } else if (type == PSU_QUEUE_TRIGGER_CHANNEL_SAVE_AND_DISABLE_OE) {
            Channel::saveAndDisableOE();
        } else if (type == PSU_QUEUE_TRIGGER_CHANNEL_RESTORE_OE) {
            Channel::restoreOE();
        } else if (type == PSU_QUEUE_SET_COUPLING_TYPE) {
            channel_dispatcher::setCouplingTypeInPsuThread((channel_dispatcher::CouplingType)param);
        } else if (type == PSU_QUEUE_SET_TRACKING_CHANNELS) {
            channel_dispatcher::setTrackingChannels((uint16_t)param);
        } else if (type == PSU_QUEUE_CHANNEL_OUTPUT_ENABLE) {
            channel_dispatcher::outputEnable(Channel::get((param >> 8) & 0xFF), param & 0xFF ? true : false);
        } else if (type == PSU_QUEUE_SYNC_OUTPUT_ENABLE) {
            channel_dispatcher::syncOutputEnable();
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_HARD_RESET) {
            restart();
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_SHUTDOWN) {
            shutdown();
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_SET_VOLTAGE) {
            channel_dispatcher::setVoltageInPsuThread((int)param);
        } else if (type == PSU_QUEUE_MESSAGE_TYPE_SET_CURRENT) {
            channel_dispatcher::setCurrentInPsuThread((int)param);
        }
    } else if (g_isBooted) {
        tick();
    }
}

bool measureAllAdcValuesOnChannel(int channelIndex) {
	if (g_slots[Channel::get(channelIndex).slotIndex].moduleInfo->moduleType == MODULE_TYPE_NONE) {
		return true;
	}

    g_adcMeasureAllFinished = false;
    osMessagePut(eez::psu::g_psuMessageQueueId, PSU_QUEUE_MESSAGE(PSU_QUEUE_MESSAGE_ADC_MEASURE_ALL, channelIndex), 0);

    int i;
    for (i = 0; i < 100 && !g_adcMeasureAllFinished; ++i) {
        osDelay(10);
    }

    return g_adcMeasureAllFinished;
}

////////////////////////////////////////////////////////////////////////////////

void initChannels() {
    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).init();
    }
}

bool testChannels() {
    if (!g_powerIsUp) {
        // test is skipped
        return true;
    }

    bool result = true;

    channel_dispatcher::disableOutputForAllChannels();

    for (int i = 0; i < CH_NUM; ++i) {
        WATCHDOG_RESET();
        result &= Channel::get(i).test();
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

bool psuReset() {
    // *ESE 0
    scpi_reg_set(SCPI_REG_ESE, 0);

    // *SRE 0
    scpi_reg_set(SCPI_REG_SRE, 0);

    // *STB 0
    scpi_reg_set(SCPI_REG_STB, 0);

    // *ESR 0
    scpi_reg_set(SCPI_REG_ESR, 0);

    // STAT:OPER[:EVEN] 0
    scpi_reg_set(SCPI_REG_OPER, 0);

    // STAT:OPER:COND 0
    reg_set(SCPI_PSU_REG_OPER_COND, 0);

    // STAT:OPER:ENAB 0
    scpi_reg_set(SCPI_REG_OPERE, 0);

    // STAT:OPER:INST[:EVEN] 0
    reg_set(SCPI_PSU_REG_OPER_INST_EVENT, 0);

    // STAT:OPER:INST:COND 0
    reg_set(SCPI_PSU_REG_OPER_INST_COND, 0);

    // STAT:OPER:INST:ENAB 0
    reg_set(SCPI_PSU_REG_OPER_INST_ENABLE, 0);

    // STAT:OPER:INST:ISUM[:EVEN] 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_EVENT1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_EVENT2, 0);

    // STAT:OPER:INST:ISUM:COND 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_COND1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_COND2, 0);

    // STAT:OPER:INST:ISUM:ENAB 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_ENABLE1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_ENABLE2, 0);

    // STAT:QUES[:EVEN] 0
    scpi_reg_set(SCPI_REG_QUES, 0);

    // STAT:QUES:COND 0
    reg_set(SCPI_PSU_REG_QUES_COND, 0);

    // STAT:QUES:ENAB 0
    scpi_reg_set(SCPI_REG_QUESE, 0);

    // STAT:QUES:INST[:EVEN] 0
    reg_set(SCPI_PSU_REG_QUES_INST_EVENT, 0);

    // STAT:QUES:INST:COND 0
    reg_set(SCPI_PSU_REG_QUES_INST_COND, 0);

    // STAT:QUES:INST:ENAB 0
    reg_set(SCPI_PSU_REG_QUES_INST_ENABLE, 0);

    // STAT:QUES:INST:ISUM[:EVEN] 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_EVENT1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_EVENT2, 0);

    // STAT:QUES:INST:ISUM:COND 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_COND1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_COND2, 0);

    // STAT:OPER:INST:ISUM:ENAB 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_ENABLE1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_ENABLE2, 0);

    eez::scpi::resetContext();

#if OPTION_ETHERNET
    ntp::reset();
#endif

    // TEMP:PROT[AUX]
    // TEMP:PROT:DEL
    // TEMP:PROT:STAT[AUX]
    for (int i = 0; i < temp_sensor::NUM_TEMP_SENSORS; ++i) {
        temperature::ProtectionConfiguration &temp_prot = temperature::sensors[i].prot_conf;
        if (i == temp_sensor::AUX) {
            temp_prot.delay = OTP_AUX_DEFAULT_DELAY;
            temp_prot.level = OTP_AUX_DEFAULT_LEVEL;
            temp_prot.state = OTP_AUX_DEFAULT_STATE;
        } else {
            temp_prot.delay = OTP_CH_DEFAULT_DELAY;
            temp_prot.level = OTP_CH_DEFAULT_LEVEL;
            temp_prot.state = OTP_CH_DEFAULT_STATE;
        }
    }

    // CAL[:MODE] OFF
    calibration::stop();

    // reset channels
    int err;
    if (!channel_dispatcher::setCouplingType(channel_dispatcher::COUPLING_TYPE_NONE, &err)) {
        event_queue::pushEvent(err);
    }
    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).reset();
    }

    //
    channel_dispatcher::setTrackingChannels(0);

    //
    trigger::reset();

    //
    list::reset();

    //
    dlog_record::reset();

    // SYST:POW ON
    if (powerUp()) {
        Channel::updateAllChannels();

    	return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////

bool autoRecall(int recallOptions) {
    if (persist_conf::isProfileAutoRecallEnabled()) {
        int location = persist_conf::getProfileAutoRecallLocation();
        int err;
        auto forceDisableOutput = persist_conf::isForceDisablingAllOutputsOnPowerUpEnabled() || !g_bootTestSuccess;
        if (profile::recallFromLocation(location, recallOptions | (forceDisableOutput ? profile::RECALL_OPTION_FORCE_DISABLE_OUTPUT : 0), false, &err)) {
            return true;
        }
        if (err != SCPI_ERROR_FILE_NOT_FOUND) {
            generateError(err);
        }
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////

bool powerUp() {
    if (g_powerIsUp) {
        return true;
    }

    if (!temperature::isAllowedToPowerUp()) {
        return false;
    }

    sound::playPowerUp(sound::PLAY_POWER_UP_CONDITION_NONE);

    g_rlState = persist_conf::devConf.isFrontPanelLocked ? RL_STATE_REMOTE : RL_STATE_LOCAL;

#if OPTION_DISPLAY
    gui::showWelcomePage();
#endif

    // turn power on
    board::powerUp();
    g_powerIsUp = true;

    psuReset();

    ontime::g_mcuCounter.start();
    for (int slotIndex = 0; slotIndex < NUM_SLOTS; slotIndex++) {
        if (g_slots[slotIndex].moduleInfo->moduleType != MODULE_TYPE_NONE) {
            ontime::g_moduleCounters[slotIndex].start();
        }
    }

    // init channels
    initChannels();
    uint32_t initTime = millis();

    bool testSuccess = true;

    if (g_isBooted) {
    	testSuccess &= testMaster();
    }

    // test channels
    testSuccess &= testChannels();

    int32_t diff = millis() - initTime;
    static const int32_t CONF_INIT_TIME = 1000;
    if (diff < CONF_INIT_TIME) {
        delay(CONF_INIT_TIME - diff);
    }

    // turn on Power On (PON) bit of ESE register
    reg_set_esr_bits(ESR_PON);

    event_queue::pushEvent(event_queue::EVENT_INFO_POWER_UP);

    // play power up tune on success
    if (testSuccess) {
        sound::playPowerUp(sound::PLAY_POWER_UP_CONDITION_TEST_SUCCESSFUL);
    }

    g_bootTestSuccess &= testSuccess;

    return true;
}

void powerDown() {
#if OPTION_DISPLAY
    if (!g_shutdownInProgress) {
        if (g_isBooted) {
            gui::showEnteringStandbyPage();
        } else {
            gui::showStandbyPage();
        }
    }
#endif

    if (!g_powerIsUp)
        return;

    trigger::abort();
    dlog_record::abort();

    int err;
    if (!channel_dispatcher::setCouplingType(channel_dispatcher::COUPLING_TYPE_NONE, &err)) {
        event_queue::pushEvent(err);
    }

    powerDownChannels();

    board::powerDown();

    g_powerIsUp = false;

    ontime::g_mcuCounter.stop();
    for (int slotIndex = 0; slotIndex < NUM_SLOTS; slotIndex++) {
        if (g_slots[slotIndex].moduleInfo->moduleType != MODULE_TYPE_NONE) {
            ontime::g_moduleCounters[slotIndex].stop();
        }
    }

    event_queue::pushEvent(event_queue::EVENT_INFO_POWER_DOWN);

#if OPTION_FAN
    aux_ps::fan::g_testResult = TEST_OK;
#endif

    io_pins::tick(micros());

    sound::playPowerDown();
}

void powerDownChannels() {
    channel_dispatcher::disableOutputForAllChannels();

    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).onPowerDown();
    }
}

bool isPowerUp() {
    return g_powerIsUp;
}

void changePowerState(bool up) {
    if (up == g_powerIsUp)
        return;

    // at least MIN_POWER_UP_DELAY seconds shall pass after last power down
    if (g_testPowerUpDelay) {
        if (millis() - g_powerDownTime < MIN_POWER_UP_DELAY * 1000)
            return;
        g_testPowerUpDelay = false;
    }

    if (osThreadGetId() != g_psuTaskHandle) {
        osMessagePut(g_psuMessageQueueId, PSU_QUEUE_MESSAGE(PSU_QUEUE_MESSAGE_TYPE_CHANGE_POWER_STATE, up ? 1 : 0), osWaitForever);
        return;
    }

    if (up) {
        g_bootTestSuccess = true;

        if (!powerUp()) {
            return;
        }

        autoRecall(profile::RECALL_OPTION_IGNORE_POWER);
    } else {
#if OPTION_DISPLAY
        if (!g_shutdownInProgress) {
            gui::showEnteringStandbyPage();
        }
#endif

        powerDown();

        g_testPowerUpDelay = true;
        g_powerDownTime = millis();
    }
}

void powerDownBySensor() {
    if (g_powerIsUp) {
#if OPTION_DISPLAY
        gui::showEnteringStandbyPage();
#endif

        channel_dispatcher::disableOutputForAllChannels();

        powerDown();
    }
}

////////////////////////////////////////////////////////////////////////////////

void onProtectionTripped() {
    if (isPowerUp()) {
        if (persist_conf::isShutdownWhenProtectionTrippedEnabled()) {
            powerDownBySensor();
        } else {
            if (persist_conf::isOutputProtectionCoupleEnabled()) {
                channel_dispatcher::disableOutputForAllChannels();
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

typedef void (*TickFunc)(uint32_t tickCount);
static TickFunc g_tickFuncs[] = {
    temperature::tick,
#if OPTION_FAN
    aux_ps::fan::tick,
#endif
    datetime::tick,
    idle::tick
};
static const int NUM_TICK_FUNCS = sizeof(g_tickFuncs) / sizeof(TickFunc);
static int g_tickFuncIndex = 0;

void tick() {
    WATCHDOG_RESET();

    uint32_t tickCount = micros();

    dlog_record::tick(tickCount);

    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).tick(tickCount);
    }

    io_pins::tick(tickCount);

    trigger::tick(tickCount);

    list::tick(tickCount);

    g_tickFuncs[g_tickFuncIndex](tickCount);
    g_tickFuncIndex = (g_tickFuncIndex + 1) % NUM_TICK_FUNCS;

    if (g_diagCallback) {
        g_diagCallback();
        g_diagCallback = NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////

void setQuesBits(int bit_mask, bool on) {
    reg_set_ques_bit(bit_mask, on);
}

void setOperBits(int bit_mask, bool on) {
    reg_set_oper_bit(bit_mask, on);
}

////////////////////////////////////////////////////////////////////////////////

const char *getCpuModel() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return "Simulator, " FIRMWARE;
#elif defined(EEZ_PLATFORM_STM32)
    return "STM32, " FIRMWARE;
#endif
}

const char *getCpuType() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return "Simulator";
#elif defined(EEZ_PLATFORM_STM32)
    return "STM32";
#endif
}

bool isMaxCurrentLimited() {
    return g_maxCurrentLimitCause != MAX_CURRENT_LIMIT_CAUSE_NONE;
}

void limitMaxCurrent(MaxCurrentLimitCause cause) {
    if (g_maxCurrentLimitCause != cause) {
        g_maxCurrentLimitCause = cause;

        if (isMaxCurrentLimited()) {
            for (int i = 0; i < CH_NUM; ++i) {
                if (Channel::get(i).getCurrentLimit() > ERR_MAX_CURRENT) {
                    channel_dispatcher::setCurrentLimit(Channel::get(i), ERR_MAX_CURRENT);
                }
            }
        }
    }
}

void unlimitMaxCurrent() {
    limitMaxCurrent(MAX_CURRENT_LIMIT_CAUSE_NONE);
}

MaxCurrentLimitCause getMaxCurrentLimitCause() {
    return g_maxCurrentLimitCause;
}

#if defined(EEZ_PLATFORM_SIMULATOR)

namespace simulator {

static float g_temperature[temp_sensor::NUM_TEMP_SENSORS];
static bool g_pwrgood[CH_MAX];
static bool g_rpol[CH_MAX];
static bool g_cv[CH_MAX];
static bool g_cc[CH_MAX];
float g_uSet[CH_MAX];
float g_iSet[CH_MAX];

void init() {
    for (int i = 0; i < temp_sensor::NUM_TEMP_SENSORS; ++i) {
        g_temperature[i] = 25.0f;
    }

    for (int i = 0; i < CH_MAX; ++i) {
        g_pwrgood[i] = true;
        g_rpol[i] = false;
    }
}

void tick() {
    psu::tick();
}

void setTemperature(int sensor, float value) {
    g_temperature[sensor] = value;
}

float getTemperature(int sensor) {
    return g_temperature[sensor];
}

bool getPwrgood(int pin) {
    return g_pwrgood[pin];
}

void setPwrgood(int pin, bool on) {
    g_pwrgood[pin] = on;
}

bool getRPol(int pin) {
    return g_rpol[pin];
}

void setRPol(int pin, bool on) {
    g_rpol[pin] = on;
}

bool getCV(int pin) {
    return g_cv[pin];
}

void setCV(int pin, bool on) {
    g_cv[pin] = on;
}

bool getCC(int pin) {
    return g_cc[pin];
}

void setCC(int pin, bool on) {
    g_cc[pin] = on;
}

////////////////////////////////////////////////////////////////////////////////

void exit() {
}

} // namespace simulator

#endif

} // namespace psu
} // namespace eez
