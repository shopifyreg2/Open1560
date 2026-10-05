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
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

define_dummy_symbol(mmgame_interface);

#include "interface.h"

#include "agi/rsys.h"
#include "agisw/swrend.h"
#include "agiworld/quality.h"
#include "agiworld/texsheet.h"
#include "data7/memstat.h"
#include "data7/timer.h"
#include "memory/allocator.h"
#include "midtown.h"
#include "mmaudio/manager.h"
#include "mmcar/carsim.h"
#include "mmcityinfo/citylist.h"
#include "mmcityinfo/playerdata.h"
#include "mmcityinfo/state.h"
#include "mmcityinfo/vehlist.h"
#include "mmnetwork/network.h"
#include "mmui/cr_settings.h"
#include "mmwidget/manager.h"
#include "mmwidget/menu.h"

// ?IsModemDialin@@YA_NXZ
ARTS_IMPORT /*static*/ bool IsModemDialin();

// ?ZoneWatcher@@YGKPAX@Z
ARTS_IMPORT /*static*/ ulong ARTS_STDCALL ZoneWatcher(void* arg1);

mmInterface::~mmInterface()
{
    agiCurState.SetTexFilter(agiRQ.TexFilter ? agiTexFilter::Trilinear : agiTexFilter::Bilinear);
    swSetInterlace(MMSTATE.Interlaced);
    EnableSmoke = ForceSmoke | !agiCurState.GetSoftwareRendering();
    ALLOCATOR.SanityCheck();

    {
        ARTS_MEM_STAT("mmInterface Destructor");

        AudMgr()->DeallocateUIADF();
        NETMGR.Deallocate();

        if (MenuManager::Instance)
        {
            delete MenuManager::Instance;

            MenuManager::Instance = nullptr;
        }

        std::memset(PlayerIDs, 0, sizeof(PlayerIDs));

        ALLOCATOR.SanityCheck();
    }

    TEXSHEET.Kill();

    if (VehicleListPtr)
    {
        delete VehicleListPtr;
        VehicleListPtr = nullptr;
    }

    if (CityListPtr)
    {
        delete CityListPtr;
        CityListPtr = nullptr;
    }

    ALLOCATOR.SanityCheck();
}

void mmInterface::SetStateRace(i32 /*arg1*/)
{}

namespace
{
    // The densities are transferred as a single byte each, exactly like the original game
    // does for MMSTATE::PedDensity. Note that this loses precision for values outside [0, 1].
    u8 PackDensity(f32 density)
    {
        return static_cast<u8>(density * 255.0f);
    }

    f32 UnpackDensity(u32 density)
    {
        return static_cast<f32>(density & 0xFF) / 255.0f;
    }
} // namespace

void mmInterface::SetSessionData(NETSESSION_DESC* arg1)
{
    arg1->GameVersion = 10;

    arg1->dword4 = (static_cast<i32>(MMSTATE.GameMode) << 12) | (MMSTATE.EventId << 8) |
        (static_cast<i32>(MMSTATE.Weather) << 4) | static_cast<i32>(MMSTATE.TimeOfDay);

    const i32 race_flags = static_cast<i32>(MMSTATE.Difficulty) << 2 | MMSTATE.DisableDamage;

    if (MMSTATE.GameMode == mmGameMode::CnR)
    {
        // Only the low byte of the encoded data is ever read back.
        arg1->dword8 = race_flags | (MenuCRSettings->EncodeCRData() & 0xFF) << 8;
    }
    else
    {
        arg1->dword8 = race_flags | MMSTATE.NumLaps << 4 | static_cast<i32>(MMSTATE.TimeLimit) << 8;
    }

    arg1->dwordC = PackDensity(MMSTATE.PedDensity) | PackDensity(MMSTATE.AmbientDensity) << 8 |
        PackDensity(MMSTATE.CopDensity) << 16;

    // mmGameMulti::Init clears both densities before it calls mmGame::Init, so keep a copy of
    // what the host wants here. Not every way of starting a multiplayer race comes back through
    // GetSessionData, so this cannot rely on reading the description back.
    MMSTATE.HostAmbientDensity = MMSTATE.AmbientDensity;
    MMSTATE.HostCopDensity = MMSTATE.CopDensity;
}

void mmInterface::GetSessionData(NETSESSION_DESC arg1)
{
    MMSTATE.GameMode = static_cast<mmGameMode>(arg1.dword4 >> 12 & 0xF);
    MMSTATE.Weather = static_cast<mmWeather>(arg1.dword4 >> 4 & 0xF);
    MMSTATE.TimeOfDay = static_cast<mmTimeOfDay>(arg1.dword4 & 0xF);
    MMSTATE.EventId = arg1.dword4 >> 8 & 0xF;

    MMSTATE.PedDensity = UnpackDensity(arg1.dwordC);

    // Hosts which do not know about these densities leave both bytes zero.
    // Keep our own values in that case instead of ending up with no traffic and no cops.
    if (arg1.dwordC & 0xFFFF0000)
    {
        MMSTATE.AmbientDensity = UnpackDensity(arg1.dwordC >> 8);
        MMSTATE.CopDensity = UnpackDensity(arg1.dwordC >> 16);

        // mmGameMulti::Init clears both before it calls mmGame::Init, so keep our own copy too.
        MMSTATE.HostAmbientDensity = MMSTATE.AmbientDensity;
        MMSTATE.HostCopDensity = MMSTATE.CopDensity;
    }

    if (MMSTATE.GameMode == mmGameMode::CnR)
    {
        MenuCRSettings->DecodeCRData(arg1.dword8 >> 8 & 0xFF);
        SetCRStateData();
    }
    else
    {
        MMSTATE.NumLaps = arg1.dword8 >> 4 & 0xF;
        MMSTATE.TimeLimit = static_cast<f32>(arg1.dword8 >> 8 & 0xFF);
    }

    MMSTATE.Difficulty = static_cast<mmSkillLevel>(arg1.dword8 >> 2 & 3);
    MMSTATE.DisableDamage = arg1.dword8 & 1;
}

void ReportTimeAlloc(f32 time)
{
    Displayf(
        "*********Load time %f = %f seconds, %dK Allocated", time, LoadTimer.Time(), ALLOCATOR.GetHeapUsed() >> 10);
}

// ?JoinViaZone@@3HA
ARTS_IMPORT extern b32 JoinViaZone;

void mmInterface::InitLobby()
{
    NETMGR.InitializeLobby(8, false);

    if (!JoinViaZone)
    {
        if (NETMGR.JoinLobbySession())
        {
            NETMGR.SetSysCallback([this](void* param) { MessageCallback(nullptr, param); });
            NETMGR.SetAppCallback([this](void* param) { MessageCallback2(nullptr, param); });
        }
        else
        {
            MMSTATE.NetworkStatus = 0;
            JoinViaZone = true;
        }
    }
}

void mmInterface::PlayerResolveCars()
{
    // NOTE: Use MMCURRPLAYER to check passes/score, because PlayerResolveScore may not have been called yet.

    mmVehList* vehlist = VehList();
    mmCityInfo* cityinfo = DefaultCityInfo();

    for (i32 i = 0; i < vehlist->NumVehicles; ++i)
    {
        mmVehInfo* vehinfo = vehlist->GetVehicleInfo(i);
        bool locked = false;

        if (u32 flags = vehinfo->UnlockFlags)
        {
            if (flags & VEH_INFO_UNLOCK_ANY_TWO)
                locked = locked || (MMCURRPLAYER.GetTotalPassed() < 2);

            if (flags & VEH_INFO_UNLOCK_BLITZ)
                locked = locked || (MMCURRPLAYER.GetBlitzPassed() < cityinfo->BlitzCount / 2);

            if (flags & VEH_INFO_UNLOCK_CIRCUIT)
                locked = locked || (MMCURRPLAYER.GetCircuitPassed() < cityinfo->CircuitCount / 2);

            if (flags & VEH_INFO_UNLOCK_CHECKPOINT)
                locked = locked || (MMCURRPLAYER.GetCheckpointPassed() < cityinfo->CheckpointCount / 2);

            if (flags & VEH_INFO_UNLOCK_COMPLETE)
                locked = locked ||
                    (MMCURRPLAYER.GetTotalPassed() <
                        cityinfo->BlitzCount + cityinfo->CircuitCount + cityinfo->CheckpointCount);

            if (flags & VEH_INFO_UNLOCK_PRO)
                locked = locked || (MMCURRPLAYER.Difficulty != 1);
        }

        if (i32 score = vehinfo->UnlockScore)
            locked = locked || (MMCURRPLAYER.GetTotalScore() < score);

        vehinfo->IsLocked = locked && !AllCars;
    }
}

void mmInterface::SetStateDefaults()
{
    MMSTATE.TimeOfDay = mmTimeOfDay::Noon;
    MMSTATE.GameMode = mmGameMode::Cruise;
    MMSTATE.Weather = mmWeather::Sun;
    MMSTATE.EventId = 0;
}

void mmInterface::SetNavigationOrders()
{
    // TODO: Do this during menu construction instead
    const auto fixup = [this](UIMenu* menu, std::initializer_list<const char*> labels) {
        menu->SetNavigationOrder(labels.begin(), labels.size());
    };

    fixup((UIMenu*) MenuMgr()->GetNavBar(), {"mnav_prev", "mnav_opt", "mnav_help", "mnav_stow", "mnav_exit"});
    fixup((UIMenu*) MenuRace,
        {"race_roam", "race_blitz", "race_waypt", "race_circ", "RACE NAME", "race_drop_frame", "race desc icons",
            "race_cenv", "LAPS", "race_laps", "CHECKPOINTS", "race_checkpoints", "OPPONENTS", "race_oppo", "race_env",
            "TOD Icons", "Weather Icons", "TRAFFIC DENSITY", "PEDESTRIAN DENSITY", "COP DENSITY", "race_next"});
    fixup((UIMenu*) MenuHostRace,
        {"race_roam", "race_blitz", "race_waypt", "race_circ", "race_cops", "RACE NAME", "host_drop_frame",
            "race desc icons", "LAPS", "host_laps", "CHECKPOINTS", "race_checkpoints", "host_checkpoints", "OPPONENTS",
            "race_oppo", "race_cenv", "Password", "Max Players", "race_env", "TOD Icons", "Weather Icons",
            "PEDESTRIAN DENSITY", "host_cont"});

    fixup((UIMenu*) DlgDriverRec, {"compscroll", "drec_bltz", "drec_circ", "drec_chck", "dlg_done"});

    fixup((UIMenu*) DlgHallOfFame,
        {"compscroll", "drec_bltz", "drec_circ", "drec_chck", "hoff_amap", "hoff_prop", "hoff_pros", "dlg_done"});
}