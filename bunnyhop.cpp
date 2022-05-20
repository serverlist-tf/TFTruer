/*
 * Copyright (C) 2015 AnAkkk <anakin.cs@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "bunnyhop.h"
#include "utils.h"
#include "editablecommands.h"

#include "in_buttons.h"

CBunnyHop g_BunnyHop;

ConVar tftrue_bunnyhop("tftrue_bunnyhop", "0", FCVAR_NOTIFY,
	"Turn on/off Bunny hopping. Hold space to pogo jump.",
	true, 0, true, 1,
	&CBunnyHop::Callback);


// duckjump offsets from https://github.com/Mikusch/tf-bhop
bool CBunnyHop::Init(const CModuleScanner& ServerModule)
{
	char* os;

#ifdef _LINUX

	os = (char*)"Linux";

	// "BumperCar.Jump"
	PreventBunnyJumpingAddr                 = ServerModule.FindSymbol(
	"_ZN15CTFGameMovement19PreventBunnyJumpingEv");
	CheckJumpButtonAddr                     = ServerModule.FindSymbol(
	"_ZN15CTFGameMovement15CheckJumpButtonEv");

	duckbuttonOffset 						= 0xD7;

#else

	os = (char*)"Windows";

	// sub_10415370
	PreventBunnyJumpingAddr                 = ServerModule.FindSignature((unsigned char *)
	"\x56\x8B\xF1\x6A\x52\x8B\x8E\xA8\x07\x00\x00\x81\xC1\xB0\x19\x00\x00\xE8\x00\x00\x00\x00\x00\xC0\x75\x00", "xxxxxxxxxxxxxxxxx????xxx?");
	// sub_10412260:
	CheckJumpButtonAddr                     = ServerModule.FindSignature((unsigned char *)
	"\x55\x8B\xEC\x83\xEC\x0C\x57\x8B\xF9\x8B\x47\x04\x80\xB8\x54\x0A\x00\x00\x00", "xxxxxxxxxxxxxxxxxxx");

	duckbuttonOffset 						= 0x1FB;

#endif


	if (!PreventBunnyJumpingAddr)
	{
		Warning("Couldn't get sig for PreventBunnyJumpingAddr! OS: %s\n", os);
	}
	if (!CheckJumpButtonAddr)
	{
		Warning("Couldn't get sig for CheckJumpButtonAddr! OS: %s\n", os);
	}

	gameeventmanager->AddListener(this, "teamplay_round_start", true);

	return true;
}

void CBunnyHop::OnJoinClass(edict_t *pEntity)
{
	if(tftrue_bunnyhop.GetBool())
	{
		Message(IndexOfEdict(pEntity),"\003[TFTruer] Bunny hopping is enabled on this server. Type !speedmeter [on/off] to see your speed while bunny hopping.");
	}
}

void CBunnyHop::OnPlayerDisconnect(edict_t *pEntity)
{
	m_bSpeedMeter[IndexOfEdict(pEntity)] = false;
}

void CBunnyHop::OnUnload()
{
	gameeventmanager->RemoveListener(this);
}

void CBunnyHop::FireGameEvent(IGameEvent *pEvent)
{
	if( !strcmp(pEvent->GetName(), "teamplay_round_start"))
	{
		if(tftrue_bunnyhop.GetBool())
		{
			CBaseEntity *pEntity = nullptr;

			while((pEntity = g_pServerTools->FindEntityByClassname(pEntity, "func_door")) != nullptr)
			{
				g_pServerTools->SetKeyValue(pEntity, "speed", 1999);
			}
		}
	}
}

void CBunnyHop::Callback( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVarRef sv_airaccelerate("sv_airaccelerate");

	ConVar* v = (ConVar*)var;
	if (v->GetBool() && !flOldValue)
	{
		((EditableConVar*)sv_airaccelerate.GetLinkedConVar())->m_nFlags &= ~FCVAR_NOTIFY;
		// resurf uses 150 for surf and 1000 for bhop
		// 1000 seems to be the standard for bhop
		// thx joinedsenses for this info
		sv_airaccelerate.SetValue(1000);

		CBaseEntity *pEntity = nullptr;

		while((pEntity = g_pServerTools->FindEntityByClassname(pEntity, "func_door")) != nullptr)
		{
			g_pServerTools->SetKeyValue(pEntity, "speed", 1999);
		}

		g_BunnyHop.PreventBunnyJumpingRoute.RouteFunction(g_BunnyHop.PreventBunnyJumpingAddr, (void*)CBunnyHop::PreventBunnyJumping);
		g_BunnyHop.CheckJumpButtonRoute.RouteFunction(g_BunnyHop.CheckJumpButtonAddr, (void*)CBunnyHop::CheckJumpButton);

		// jmp
		PatchAddress((void*)g_BunnyHop.CheckJumpButtonAddr, duckbuttonOffset, 1, (unsigned char*)"\xEB");
	}
	else if (!v->GetBool() && flOldValue)
	{
		((ConVar*)sv_airaccelerate.GetLinkedConVar())->Revert();
		((ConVar*)sv_airaccelerate.GetLinkedConVar())->AddFlags(FCVAR_NOTIFY);

		g_BunnyHop.PreventBunnyJumpingRoute.RestoreFunction();
		g_BunnyHop.CheckJumpButtonRoute.RestoreFunction();

		// jz
		PatchAddress((void*)g_BunnyHop.CheckJumpButtonAddr, duckbuttonOffset, 1, (unsigned char*)"\x74");
	}
}

bool CBunnyHop::CheckJumpButton(CGameMovement *pMovement EDX2)
{
	if (!pMovement)
	{
		return g_BunnyHop.CheckJumpButtonRoute.CallOriginalFunction<CheckJumpButton_t>()(pMovement);
	}

	CBasePlayer *pPlayer = pMovement->player;
	if (!pPlayer)
	{
		return g_BunnyHop.CheckJumpButtonRoute.CallOriginalFunction<CheckJumpButton_t>()(pMovement);
	}
	CMoveData *pMoveData = pMovement->GetMoveData();

	int icl = pPlayer->entindex();

	if (pMoveData)
	{
		if (tftrue_bunnyhop.GetBool())
		{
			// logic ported from https://github.com/sapphonie/tf-autohop
			if (*g_EntityProps.GetSendProp<char>(pPlayer, "m_lifeState") == LIFE_ALIVE)
			{
				int flags = *g_EntityProps.GetDataMapProp<int>(pPlayer, "m_fFlags");
				if ((flags & FL_ONGROUND) && (pMoveData->m_nButtons & IN_JUMP))
				{
					pMoveData->m_nOldButtons    &= ~(IN_JUMP | IN_DUCK);
				}
				if (g_BunnyHop.m_bSpeedMeter[icl])
				{
					Vector velocity = *g_EntityProps.GetSendProp<Vector>(pPlayer, "m_vecVelocity[0]");
					TextMessage(icl, "Speed meter: %.2fhu/s", sqrt(velocity.x*velocity.x + velocity.y*velocity.y));
				}
			}
		}
	}
	return g_BunnyHop.CheckJumpButtonRoute.CallOriginalFunction<CheckJumpButton_t>()(pMovement);
}

void CBunnyHop::PreventBunnyJumping()
{

}
