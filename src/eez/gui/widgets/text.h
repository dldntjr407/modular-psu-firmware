/*
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

#pragma once

#include <eez/gui/widget.h>

namespace eez {
namespace gui {

#define IGNORE_LUMINOSITY_FLAG 1

struct TextWidgetSpecific {
#if OPTION_SDRAM    
    const char *text;
#else
    uint32_t textOffset;
#endif
    uint16_t focusStyle;
    uint8_t flags;
};

struct TextWidget {
    Widget common;
    TextWidgetSpecific specific;
}; 

void TextWidget_fixPointers(Widget *widget);
void TextWidget_autoSize(TextWidget& widget);
void TextWidget_draw(const WidgetCursor &widgetCursor);

} // namespace gui
} // namespace eez