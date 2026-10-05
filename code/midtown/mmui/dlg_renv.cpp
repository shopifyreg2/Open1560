/*
    Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
    Copyright (C) 2020 Brick

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

define_dummy_symbol(mmui_dlg_renv);

#include "dlg_renv.h"

#include "mmcityinfo/state.h"
#include "mmwidget/slider.h"

void Dialog_RaceEnvironment::DoneCallback()
{
    MMSTATE.Weather = static_cast<mmWeather>(WeatherField - 1);
    MMSTATE.TimeOfDay = static_cast<mmTimeOfDay>(TimeOfDayField - 1);
    MMSTATE.CopDensity = CopDensity;
    MMSTATE.PedDensity = PedDensity;
    MMSTATE.AmbientDensity = AmbientDensity;
    MMSTATE.EnablePaging = EnablePagingField;

    // mmGameMulti::Init clears the ambient and cop densities before it calls mmGame::Init, so
    // remember what the host just picked. Not every way of starting a multiplayer race reads
    // the session description back, so the menu is the only place that always sees the change.
    MMSTATE.HostAmbientDensity = AmbientDensity;
    MMSTATE.HostCopDensity = CopDensity;
}

void Dialog_RaceEnvironment::SetMultiRaceOptions(i32 /*arg1*/)
{
    // Note that the argument is inverted in the original game: a non-zero value greys the sliders
    // out. The host race menu and the lobby always pass a non-zero value, which is why the
    // traffic and police density could not be changed in multiplayer. Both are part of the
    // session description now, so keep them editable everywhere. The single player menu passes
    // zero, so nothing changes there.
    AmbientDensitySlider->Enable();
    CopDensitySlider->Enable();
}