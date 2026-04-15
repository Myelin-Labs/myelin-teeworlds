/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <generated/server_data.h>
#include <game/server/gamecontext.h>

#include "character.h"
#include "laser.h"

CLaser::CLaser(CGameWorld *pGameWorld, rvec2 Pos, rvec2 Direction, real_t StartEnergy, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, Pos)
{
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	GameWorld()->InsertEntity(this);
	DoBounce();
}


bool CLaser::HitCharacter(rvec2 From, rvec2 To)
{
	rvec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameWorld()->IntersectCharacter(m_Pos, To, REAL(0.f), At, pOwnerChar);
	if(!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = REAL(-1);
	pHit->TakeDamage(rvec2(REAL(0), REAL(0)), normalize(To-From), g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage, m_Owner, WEAPON_LASER);
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < REAL(0))
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	rvec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			rvec2 TempPos = m_Pos;
			rvec2 TempDir = m_Dir * REAL(4.0f);

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, REAL(1.0f), 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + (real_t) GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = REAL(-1);

			GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = REAL(-1);
		}
	}
}

void CLaser::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CLaser::Tick()
{
	if((Server()->Tick() - m_EvalTick) > (Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(m_Pos.x);
	pObj->m_Y = round_to_int(m_Pos.y);
	pObj->m_FromX = round_to_int(m_From.x);
	pObj->m_FromY = round_to_int(m_From.y);
	pObj->m_StartTick = m_EvalTick;
}
