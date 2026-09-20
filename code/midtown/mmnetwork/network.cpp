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

define_dummy_symbol(mmnetwork_network);

#include "network.h"

static GUID MM_GUID {0x6C9983A7, 0xC037, 0x11D2, {0xA8, 0xDA, 0x00, 0xA0, 0xC9, 0x70, 0xAF, 0x5D}};

static mem::cmd_param PARAM_dplay {"dplay"};

static HRESULT(WINAPI* Orig_DirectPlayCreate)(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnk);

// ?netDirectPlayCreate@@YGJPAU_GUID@@PAPAUIDirectPlay@@PAUIUnknown@@@Z
ARTS_EXPORT HRESULT WINAPI netDirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnk)
{
    return Orig_DirectPlayCreate ? Orig_DirectPlayCreate(lpGUID, lplpDP, pUnk) : ERROR_INVALID_FUNCTION;
}

static bool LoadDirectPlay()
{
    if (!PARAM_dplay.get_or(false))
    {
        Displayf("DirectPlay disabled. Use `-dplay` cmd argument to enable multiplayer");
        return false;
    }

    if (!Orig_DirectPlayCreate)
    {
        HMODULE hdplayx = LoadLibraryA("DPLAYX.DLL");

        if (hdplayx)
        {
            Orig_DirectPlayCreate =
                mem::bit_cast<decltype(Orig_DirectPlayCreate)>(GetProcAddress(hdplayx, "DirectPlayCreate"));
        }
    }

    return Orig_DirectPlayCreate;
}

b32 asNetwork::Initialize(i32 max_players, b32 secure, i32 game_version)
{
    Displayf("Initializing network");

    if (!LoadDirectPlay())
    {
        Errorf("Failed to load DirectPlay");
        return false;
    }

    if (dplay_)
        return true;

    app_guid_ = &MM_GUID;
    game_version_ = static_cast<f32>(game_version);
    max_players_ = max_players;
    secure_ = secure;
    CoInitialize(NULL);

    if (CreateInterface() != DP_OK)
    {
        in_session_ = false;
        return false;
    }

    return true;
}

namespace
{
    // The per-player data block exchanged through DirectPlay is 180 bytes in builds 1560/1588, but 188 bytes in 1589 (XP patch).
    // The rest of the game still uses the 180 byte layout, so the block is converted to/from the 1589 layout here.
    //
    // 1560: [0, 172) common data, [172, 176) int, [176, 180) int
    // 1589: [0, 172) common data, [172, 176) unused, [176, 180) int, [180, 184) int, [184, 188) int (always 1)
    constexpr i32 PLAYER_DATA_SIZE_1560 = 180;
    constexpr i32 PLAYER_DATA_SIZE_1589 = 188;
    constexpr i32 PLAYER_DATA_COMMON_SIZE = 172;
    constexpr i32 PLAYER_DATA_MAX_SIZE = 512;
} // namespace

i32 asNetwork::GetPlayerData(ulong id, void* data, i32 size)
{
    if (!dplay_)
        return false;

    HRESULT error = DP_OK;

    if (size == PLAYER_DATA_SIZE_1560)
    {
        u8 wire[PLAYER_DATA_MAX_SIZE] {};
        DWORD wire_size = sizeof(wire);

        error = dplay_->GetPlayerData(id, wire, &wire_size, DPGET_REMOTE);

        if (error == DP_OK)
        {
            u8* dest = static_cast<u8*>(data);

            if (wire_size == static_cast<DWORD>(PLAYER_DATA_SIZE_1589))
            {
                std::memcpy(dest, wire, PLAYER_DATA_COMMON_SIZE);
                std::memcpy(dest + PLAYER_DATA_COMMON_SIZE, wire + PLAYER_DATA_COMMON_SIZE + 4, 8);
            }
            else
            {
                std::memcpy(dest, wire, std::min<DWORD>(wire_size, static_cast<DWORD>(PLAYER_DATA_SIZE_1560)));
            }
        }
    }
    else
    {
        DWORD wire_size = static_cast<DWORD>(size);

        error = dplay_->GetPlayerData(id, data, &wire_size, DPGET_REMOTE);
    }

    if (error != DP_OK)
        Errorf("DPLAY::GetPlayerData -- error %08X", static_cast<u32>(error));

    return error == DP_OK;
}

i32 asNetwork::GetEnumPlayerData(i32 index, void* data, i32 size)
{
    if (!dplay_)
        return false;

    const ulong id = GetPlayerID(index);

    if (id == 0)
        return false;

    return GetPlayerData(id, data, size);
}

void asNetwork::SetPlayerData(ulong id, void* data, i32 size)
{
    if (!dplay_)
        return;

    HRESULT error = DP_OK;

    if (size == PLAYER_DATA_SIZE_1560)
    {
        u8 wire[PLAYER_DATA_SIZE_1589] {};

        std::memcpy(wire, data, PLAYER_DATA_COMMON_SIZE);
        std::memcpy(wire + PLAYER_DATA_COMMON_SIZE + 4, static_cast<const u8*>(data) + PLAYER_DATA_COMMON_SIZE, 8);

        const i32 unknown = 1;
        std::memcpy(wire + PLAYER_DATA_COMMON_SIZE + 12, &unknown, sizeof(unknown));

        error = dplay_->SetPlayerData(id, wire, sizeof(wire), DPSET_REMOTE);
    }
    else
    {
        error = dplay_->SetPlayerData(id, data, static_cast<DWORD>(size), DPSET_REMOTE);
    }

    if (error != DP_OK)
        Errorf("DPLAY::SetPlayerData -- error %08X", static_cast<u32>(error));
}

b32 asNetwork::InitializeLobby(i32 max_players, b32 secure)
{
    Displayf("Initializing lobby");

    if (!LoadDirectPlay())
    {
        Errorf("Failed to load DirectPlay");
        return false;
    }

    CoInitialize(NULL);

    if (dp_lobby_ == nullptr)
    {
        if (CoCreateInstance(CLSID_DirectPlayLobby, 0, 1, IID_IDirectPlayLobby3A, (LPVOID*) &dp_lobby_))
        {
            in_lobby_ = false;
            CoUninitialize();
            return false;
        }
    }

    max_players_ = max_players;
    secure_ = secure;

    Debugf("Lobby interface created or already created.");
    return true;
}

b32 asNetwork::JoinLobbySession()
{
    Debugf("asNetwork::JoinLobbySession()");
    in_lobby_ = false;

    if (dp_lobby_ == nullptr)
    {
        Errorf("Couldn't get Lobby interface.");
        return false;
    }

    DWORD conn_size = 0;
    if (HRESULT error = dp_lobby_->GetConnectionSettings(0, NULL, &conn_size); error != DPERR_BUFFERTOOSMALL)
    {
        Debugf("Couldn't get connection settings: %08X", error);
        return false;
    }

    if (dp_connection_)
        arts_free(dp_connection_);
    dp_connection_ = (DPLCONNECTION*) arts_malloc(conn_size);

    if (HRESULT error = dp_lobby_->GetConnectionSettings(0, dp_connection_, &conn_size))
    {
        Debugf("Couldn't get connection settings: %08X", error);
        return false;
    }

    max_players_ = 8;
    secure_ = false;
    dp_connection_->lpSessionDesc->dwFlags = DPSESSION_MIGRATEHOST | DPSESSION_KEEPALIVE;
    dp_connection_->lpSessionDesc->dwMaxPlayers = max_players_;

    if (HRESULT error = dp_lobby_->SetConnectionSettings(0, 0, dp_connection_))
    {
        Errorf("Couldn't set lobby connection: %08X", error);
        return false;
    }

    if (dplay_)
    {
        dplay_->Release();
        dplay_ = nullptr;
    }

    if (HRESULT error = dp_lobby_->ConnectEx(0, IID_IDirectPlay4A, (LPVOID*) &dplay_, NULL))
    {
        Errorf("asNetwork::JoinLobbySession: Couldn't join lobby: %08X", error);
        return false;
    }

    if (dp_connection_->dwFlags & DPLCONNECTION_CREATESESSION)
        is_host_ = true;

    in_lobby_ = true;
    in_session_ = true;
    Debugf("asNetwork::JoinLobbySession successful.");

    return true;
}

void asNetwork::Logout()
{
    if (dp_lobby_)
    {
        dp_lobby_->Release();
        dp_lobby_ = nullptr;
    }

    if (dplay_)
    {
        if (recv_buffer_)
        {
            arts_free(recv_buffer_);
            recv_buffer_ = 0;
        }
        if (dplay_->DestroyPlayer(player_id_) == DP_OK)
            Warningf("DPLAY: DestroyPlayer.");

        if (dplay_->Close() == DP_OK)
            Warningf("DPLAY: Closing session.");

        if (dplay_->Release() == 0)
            Warningf("DPLAY: DirectPlay object pointer released.");

        dplay_ = nullptr;
        CoUninitialize();
    }

    player_id_ = 0;
    in_session_ = false;
    in_lobby_ = false;
    is_host_ = false;
    sys_message_cb_ = nullptr;
    app_message_cb_ = nullptr;
    caps_ = 0;
}

void asNetwork::SetSessionData(NETSESSION_DESC* desc, char* name)
{
    if (!dplay_ || !is_host_)
        return;

    DPSESSIONDESC2 session_desc;
    DPSESSIONDESC2* p_session_desc = &session_desc;
    DWORD session_size = sizeof(session_desc);

    if (HRESULT error = dplay_->GetSessionDesc(p_session_desc, &session_size))
    {
        if (error != DPERR_BUFFERTOOSMALL)
        {
            Errorf("DPLAY: GetSessionDesc %08X", error);
            return;
        }

        p_session_desc = (DPSESSIONDESC2*) arts_malloc(session_size);
        error = dplay_->GetSessionDesc(p_session_desc, &session_size);

        if (error)
        {
            Errorf("DPLAY: GetSessionDesc %08X", error);
            return;
        }
    }

    std::memcpy(&p_session_desc->dwUser1, desc, sizeof(*desc));

    if (name)
        p_session_desc->lpszSessionNameA = name;

    HRESULT error = dplay_->SetSessionDesc(p_session_desc, 0);

    if (p_session_desc != &session_desc)
        arts_free(p_session_desc);

    if (error)
    {
        Errorf("DPLAY: SetSessionDesc %08X", error);
    }
    else
    {
        Debugf("asNetwork::Sealing play session...");
    }
}

i32 asNetwork::CreateInterface()
{
    if (!LoadDirectPlay())
    {
        Errorf("Failed to load DirectPlay");
        return ERROR_INVALID_FUNCTION;
    }

    return CoCreateInstance(CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay4A, (void**) &dplay_);
}

void asNetwork::Deallocate()
{
    if (recv_buffer_)
    {
        arts_free(recv_buffer_);
        recv_buffer_ = nullptr;
    }

    if (dp_connection_)
    {
        arts_free(dp_connection_);
        dp_connection_ = nullptr;
    }
}

void asNetwork::DestroyPlayer()
{
    if (dplay_)
    {
        if (dplay_->DestroyPlayer(player_id_) == DP_OK)
            Warningf("DPLAY: DestroyPlayer.");

        player_id_ = 0;
        is_host_ = false;
    }
}

void asNetwork::Disconnect()
{
    if (dplay_)
    {
        if (dp_connection_)
        {
            arts_free(dp_connection_);
            dp_connection_ = nullptr;
        }

        if (!dplay_->Release())
            Warningf("DPLAY: DirectPlay object pointer released.");
    }

    in_session_ = false;
    CreateInterface();
}

hook_func(INIT_main, [] {
    // DPLAYX on Wine uses a shared memory allocation at a fixed address (see DPLAYX_ConstructData).
    // Load the DLL early to improve the chance it can use that adddress.
    LoadDirectPlay();
});