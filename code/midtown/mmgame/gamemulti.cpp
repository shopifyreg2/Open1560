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

define_dummy_symbol(mmgame_gamemulti);

#include "gamemulti.h"

#include "arts7/sim.h"
#include "mmcar/car.h"
#include "mmcityinfo/state.h"
#include "mmdyna/isect.h"
#include "mmnetwork/network.h"

// ?GameMultiTickRate@@3MA
ARTS_EXPORT f32 GameMultiTickRate = (1.0f / 1000.0f);

void mmGameMulti::NextRace()
{
    // adding a comment here for error checking
}

void mmGameMulti::QuitNetwork()
{
    if (NETMGR.InLobby())
    {
        NETMGR.Logout();
    }
    else
    {
        NETMGR.DestroyPlayer();
        NETMGR.CloseSession();
        NETMGR.Deallocate();
    }
}

void mmGameMulti::HandleCarImpact(NETIMPACT_MSG* msg)
{
    if (!msg || msg->MessageId != CarImpact || !NETMGR.InSession())
        return;

    const DPID sender_id = static_cast<DPID>(msg->SenderId);
    if (sender_id == 0 || sender_id == 0xFFFFFFFFu || sender_id == NETMGR.GetLocalPlayerID() ||
        msg->AudioId != MM_IMPACT_AUDIO_9 || !std::isfinite(msg->Energy) || !std::isfinite(msg->Position.x) ||
        !std::isfinite(msg->Position.y) || !std::isfinite(msg->Position.z) || !std::isfinite(msg->Normal.x) ||
        !std::isfinite(msg->Normal.y) || !std::isfinite(msg->Normal.z) || !std::isfinite(msg->Velocity.x) ||
        !std::isfinite(msg->Velocity.y) || !std::isfinite(msg->Velocity.z))
    {
        return;
    }

    mmIntersection impact {};
    impact.Position = msg->Position;
    impact.Normal = msg->Normal;

    Vector3 velocity = msg->Velocity;

    const f32 deflection = -((velocity ^ impact.Normal));
    if (!std::isfinite(deflection))
        return;

    const f32 abs_deflection = std::abs(deflection);
    const f32 spark_deflection = abs_deflection < 100.0f ? abs_deflection : 100.0f;
    static f32 last_play_time[8] = {};

    for (i32 i = 0; i < 8; ++i)
    {
        mmNetObject& object = NetObjects[i];
        if (!object.IsEnabled || object.PlayerID != sender_id || !object.Car || !object.Car->Sim.NetworkCarAudio)
        {
            continue;
        }

        const f32 now = ::Sim()->GetElapsed();
        if (last_play_time[i] > 0.0f && (now - last_play_time[i]) < 0.1f)
            return;

        last_play_time[i] = now;

        if (spark_deflection > 0.25f && object.Car->Sim.Model)
        {
            object.Car->Sim.Model->Sparks.RadialBlast(
                static_cast<i32>(spark_deflection * 0.5f), impact.Position, impact.Normal);
        }

        object.Car->Sim.PlayImpactAudio(msg->AudioId, &impact, &velocity);
        return;
    }
}

void mmGameMulti::UpdateDebugKeyInput(i32 /*arg1*/)
{
    // adding a comment here for error checking
}

void mmGameMulti::ActivateMapNetObject(i32 player)
{
    OppIconInfo& icon = OppIcons[player];

    icon.Position = &Cars[player]->GetICS()->Matrix;
    icon.Enabled = true;
}

void mmGameMulti::DeactivateMapNetObject(i32 player)
{
    OppIconInfo& icon = OppIcons[player];

    icon.Position = 0;
    icon.Place = OPP_ICON_BLANK;
    icon.Enabled = false;
}

void mmGameMulti::StartXYZ(i32 index, Vector3& out_result, Vector3& start_position, f32 rotation, f32 length)
{
    Matrix34 transform = IDENTITY;
    transform.Rotate(YAXIS, rotation);
    transform.m3 = start_position;

    Vector3 offset = {};

    if (length >= 7.0f)
    {
        switch (index)
        {
            case 0: offset = {2.75, 0.0, 16.0}; break;
            case 1: offset = {-2.75, 0.0, 16.0}; break;
            case 2: offset = {5.5, 0.0, 16.0}; break;
            case 3: offset = {0.0, 0.0, 16.0}; break;
            case 4: offset = {2.75, 0.0, 34.0}; break;
            case 5: offset = {-2.75, 0.0, 34.0}; break;
            case 6: offset = {0.0, 0.0, 34.0}; break;
            case 7: offset = {5.5, 0.0, 34.0}; break;
        }

        if (MMSTATE.GameMode == mmGameMode::Blitz && !MMSTATE.EventId)
            offset.z *= -1.0;
    }
    else
    {
        switch (index)
        {
            case 0: offset = {2.25, 0.0, 6.0}; break;
            case 1: offset = {-2.25, 0.0, 6.0}; break;
            case 2: offset = {4.5, 0.0, 6.0}; break;
            case 4: offset = {-4.5, 0.0, 0.0}; break;
            case 5: offset = {4.5, 0.0, -6.0}; break;
            case 6: offset = {0.0, 0.0, -6.0}; break;
            case 7: offset = {-4.5, 0.0, -6.0}; break;
        }
    }

    out_result = offset ^ transform;
}
