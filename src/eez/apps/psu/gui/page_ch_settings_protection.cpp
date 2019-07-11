/*
 * EEZ PSU Firmware
 * Copyright (C) 2016-present, Envox d.o.o.
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

#include <eez/apps/psu/psu.h>

#if OPTION_DISPLAY

#include <eez/apps/psu/channel_dispatcher.h>
#include <eez/apps/psu/profile.h>
#include <eez/apps/psu/temperature.h>

#include <eez/apps/psu/gui/data.h>
#include <eez/apps/psu/gui/numeric_keypad.h>
#include <eez/apps/psu/gui/page_ch_settings_protection.h>
#include <eez/apps/psu/gui/psu.h>
#include <eez/gui/dialogs.h>

namespace eez {
namespace psu {
namespace gui {

////////////////////////////////////////////////////////////////////////////////

void ChSettingsProtectionPage::clear() {
    channel_dispatcher::clearProtection(*g_channel);

    g_actionExecFunctions[ACTION_ID_SHOW_CH_SETTINGS_PROT]();
    infoMessage("Cleared!");
}

void onClearAndDisableYes() {
    channel_dispatcher::clearProtection(*g_channel);
    channel_dispatcher::disableProtection(*g_channel);
    profile::save();

    g_actionExecFunctions[ACTION_ID_SHOW_CH_SETTINGS_PROT]();
    infoMessage("Cleared and disabled!");
}

void ChSettingsProtectionPage::clearAndDisable() {
    areYouSure(onClearAndDisableYes);
}

////////////////////////////////////////////////////////////////////////////////

int ChSettingsProtectionSetPage::getDirty() {
    return (origState != state || origLimit != limit || origLevel != level || origDelay != delay)
               ? 1
               : 0;
}

void ChSettingsProtectionSetPage::onSetFinish(bool showInfo) {
    profile::save();
    g_actionExecFunctions[ACTION_ID_SHOW_CH_SETTINGS_PROT]();
    if (showInfo) {
        infoMessage("Protection params changed!");
    }
}

void ChSettingsProtectionSetPage::set() {
    if (getDirty()) {
        setParams(true);
    }
}

void ChSettingsProtectionSetPage::toggleState() {
    state = state ? 0 : 1;
}

void ChSettingsProtectionSetPage::onLimitSet(float value) {
    popPage();
    ChSettingsProtectionSetPage *page = (ChSettingsProtectionSetPage *)getActivePage();
    page->limit = MakeValue(value, page->limit.getUnit(), g_channel->index - 1);
}

void ChSettingsProtectionSetPage::editLimit() {
    NumericKeypadOptions options;

    options.channelIndex = g_channel->index - 1;

    options.editValueUnit = limit.getUnit();

    options.min = minLimit;
    options.max = maxLimit;
    options.def = defLimit;

    options.enableMaxButton();
    options.enableDefButton();
    options.flags.signButtonEnabled = true;
    options.flags.dotButtonEnabled = true;

    NumericKeypad::start(0, limit, options, onLimitSet, 0, 0);
}

void ChSettingsProtectionSetPage::onLevelSet(float value) {
    popPage();
    ChSettingsProtectionSetPage *page = (ChSettingsProtectionSetPage *)getActivePage();
    page->level = MakeValue(value, page->level.getUnit(), g_channel->index - 1);
}

void ChSettingsProtectionSetPage::editLevel() {
    NumericKeypadOptions options;

    options.channelIndex = g_channel->index - 1;

    options.editValueUnit = level.getUnit();

    options.min = minLevel;
    options.max = maxLevel;
    options.def = defLevel;

    options.enableMaxButton();
    options.enableDefButton();
    options.flags.signButtonEnabled = true;
    options.flags.dotButtonEnabled = true;

    NumericKeypad::start(0, level, options, onLevelSet, 0, 0);
}

void ChSettingsProtectionSetPage::onDelaySet(float value) {
    popPage();
    ChSettingsProtectionSetPage *page = (ChSettingsProtectionSetPage *)getActivePage();
    page->delay = MakeValue(value, page->delay.getUnit());
}

void ChSettingsProtectionSetPage::editDelay() {
    NumericKeypadOptions options;

    options.editValueUnit = delay.getUnit();

    options.min = minDelay;
    options.max = maxDelay;
    options.def = defaultDelay;

    options.enableMaxButton();
    options.enableDefButton();
    options.flags.signButtonEnabled = true;
    options.flags.dotButtonEnabled = true;

    NumericKeypad::start(0, delay, options, onDelaySet, 0, 0);
}

////////////////////////////////////////////////////////////////////////////////

void ChSettingsOvpProtectionPage::pageAlloc() {
    origState = state = g_channel->prot_conf.flags.u_state ? 1 : 0;

    origLimit = limit =
        MakeValue(channel_dispatcher::getULimit(*g_channel), UNIT_VOLT, g_channel->index - 1);
    minLimit = channel_dispatcher::getUMin(*g_channel);
    maxLimit = channel_dispatcher::getUMax(*g_channel);
    defLimit = channel_dispatcher::getUMax(*g_channel);

    origLevel = level = MakeValue(channel_dispatcher::getUProtectionLevel(*g_channel), UNIT_VOLT,
                                  g_channel->index - 1);
    minLevel = channel_dispatcher::getUSet(*g_channel);
    maxLevel = channel_dispatcher::getUMax(*g_channel);
    defLevel = channel_dispatcher::getUMax(*g_channel);

    origDelay = delay = MakeValue(g_channel->prot_conf.u_delay, UNIT_SECOND);
    minDelay = g_channel->OVP_MIN_DELAY;
    maxDelay = g_channel->OVP_MAX_DELAY;
    defaultDelay = g_channel->OVP_DEFAULT_DELAY;
}

void ChSettingsOvpProtectionPage::onSetParamsOk() {
    ((ChSettingsOvpProtectionPage *)getActivePage())->setParams(false);
}

void ChSettingsOvpProtectionPage::setParams(bool checkLoad) {
    if (checkLoad && g_channel->isOutputEnabled() &&
        eez::less(limit.getFloat(), channel_dispatcher::getUMon(*g_channel),
                  getPrecision(UNIT_VOLT)) &&
        eez::greaterOrEqual(channel_dispatcher::getIMon(*g_channel), 0, getPrecision(UNIT_AMPER))) {
        areYouSureWithMessage("This change will affect current load.", onSetParamsOk);
    } else {
        channel_dispatcher::setVoltageLimit(*g_channel, limit.getFloat());
        channel_dispatcher::setOvpParameters(*g_channel, state, level.getFloat(), delay.getFloat());
        onSetFinish(checkLoad);
    }
}

////////////////////////////////////////////////////////////////////////////////

void ChSettingsOcpProtectionPage::pageAlloc() {
    origState = state = g_channel->prot_conf.flags.i_state ? 1 : 0;

    origLimit = limit =
        MakeValue(channel_dispatcher::getILimit(*g_channel), UNIT_AMPER, g_channel->index - 1);
    minLimit = channel_dispatcher::getIMin(*g_channel);
    maxLimit = channel_dispatcher::getIMaxLimit(*g_channel);
    defLimit = maxLimit;

    origLevel = level = 0;

    origDelay = delay = MakeValue(g_channel->prot_conf.i_delay, UNIT_SECOND);
    minDelay = g_channel->OCP_MIN_DELAY;
    maxDelay = g_channel->OCP_MAX_DELAY;
    defaultDelay = g_channel->OCP_DEFAULT_DELAY;
}

void ChSettingsOcpProtectionPage::onSetParamsOk() {
    ((ChSettingsOcpProtectionPage *)getActivePage())->setParams(false);
}

void ChSettingsOcpProtectionPage::setParams(bool checkLoad) {
    if (checkLoad && g_channel->isOutputEnabled() &&
        limit.getFloat() < channel_dispatcher::getIMon(*g_channel)) {
        areYouSureWithMessage("This change will affect current load.", onSetParamsOk);
    } else {
        channel_dispatcher::setCurrentLimit(*g_channel, limit.getFloat());
        channel_dispatcher::setOcpParameters(*g_channel, state, delay.getFloat());
        onSetFinish(checkLoad);
    }
}

////////////////////////////////////////////////////////////////////////////////

void ChSettingsOppProtectionPage::pageAlloc() {
    origState = state = g_channel->prot_conf.flags.p_state ? 1 : 0;

    origLimit = limit =
        MakeValue(channel_dispatcher::getPowerLimit(*g_channel), UNIT_WATT, g_channel->index - 1);
    minLimit = channel_dispatcher::getPowerMinLimit(*g_channel);
    maxLimit = channel_dispatcher::getPowerMaxLimit(*g_channel);
    defLimit = channel_dispatcher::getPowerDefaultLimit(*g_channel);

    origLevel = level = MakeValue(channel_dispatcher::getPowerProtectionLevel(*g_channel),
                                  UNIT_WATT, g_channel->index - 1);
    minLevel = channel_dispatcher::getOppMinLevel(*g_channel);
    maxLevel = channel_dispatcher::getOppMaxLevel(*g_channel);
    defLevel = channel_dispatcher::getOppDefaultLevel(*g_channel);

    origDelay = delay = MakeValue(g_channel->prot_conf.p_delay, UNIT_SECOND);
    minDelay = g_channel->OPP_MIN_DELAY;
    maxDelay = g_channel->OPP_MAX_DELAY;
    defaultDelay = g_channel->OPP_DEFAULT_DELAY;
}

void ChSettingsOppProtectionPage::onSetParamsOk() {
    ((ChSettingsOppProtectionPage *)getActivePage())->setParams(false);
}

void ChSettingsOppProtectionPage::setParams(bool checkLoad) {
    if (checkLoad && g_channel->isOutputEnabled()) {
        float pMon =
            channel_dispatcher::getUMon(*g_channel) * channel_dispatcher::getIMon(*g_channel);
        if (eez::less(limit.getFloat(), pMon, getPrecision(UNIT_WATT)) &&
            eez::greaterOrEqual(channel_dispatcher::getIMon(*g_channel), 0,
                                getPrecision(UNIT_AMPER))) {
            areYouSureWithMessage("This change will affect current load.", onSetParamsOk);
            return;
        }
    }

    channel_dispatcher::setPowerLimit(*g_channel, limit.getFloat());
    channel_dispatcher::setOppParameters(*g_channel, state, level.getFloat(), delay.getFloat());
    onSetFinish(checkLoad);
}

////////////////////////////////////////////////////////////////////////////////

void ChSettingsOtpProtectionPage::pageAlloc() {
#if EEZ_PSU_SELECTED_REVISION == EEZ_PSU_REVISION_R3B4 ||                                          \
    EEZ_PSU_SELECTED_REVISION == EEZ_PSU_REVISION_R5B12
    origState = state = temperature::getChannelSensorState(g_channel) ? 1 : 0;

    origLimit = limit = 0;
    minLimit = 0;
    maxLimit = 0;
    defLimit = 0;

    origLevel = level = MakeValue(temperature::getChannelSensorLevel(g_channel), UNIT_CELSIUS);
    minLevel = OTP_AUX_MIN_LEVEL;
    maxLevel = OTP_AUX_MAX_LEVEL;
    defLevel = OTP_AUX_DEFAULT_LEVEL;

    origDelay = delay = MakeValue(temperature::getChannelSensorDelay(g_channel), UNIT_SECOND);
    minDelay = OTP_AUX_MIN_DELAY;
    maxDelay = OTP_AUX_MAX_DELAY;
    defaultDelay = OTP_CH_DEFAULT_DELAY;
#endif
}

void ChSettingsOtpProtectionPage::setParams(bool checkLoad) {
#if EEZ_PSU_SELECTED_REVISION == EEZ_PSU_REVISION_R3B4 ||                                          \
    EEZ_PSU_SELECTED_REVISION == EEZ_PSU_REVISION_R5B12
    channel_dispatcher::setOtpParameters(*g_channel, state, level.getFloat(), delay.getFloat());
    onSetFinish(checkLoad);
#endif
}

} // namespace gui
} // namespace psu
} // namespace eez

#endif