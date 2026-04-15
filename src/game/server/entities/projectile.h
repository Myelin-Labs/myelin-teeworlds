/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PROJECTILE_H
#define GAME_SERVER_ENTITIES_PROJECTILE_H

#include <game/server/entity.h>

enum
{
	PLAYER_TEAM_BLUE = -2,
	PLAYER_TEAM_RED = -1
};

class CProjectile : public CEntity
{
public:
	CProjectile(CGameWorld *pGameWorld, int Type, int Owner, rvec2 Pos, rvec2 Dir, int Span,
		int Damage, bool Explosive, real_t Force, int SoundImpact, int Weapon);

	rvec2 GetPos(real_t Time);
	void FillInfo(CNetObj_Projectile *pProj);

	int GetOwner() const { return m_Owner; }
	void LoseOwner();

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

private:
	rvec2 m_Direction;
	int m_LifeSpan;
	int m_Owner;
	int m_OwnerTeam;
	int m_Type;
	int m_Damage;
	int m_SoundImpact;
	int m_Weapon;
  real_t m_Force;
	int m_StartTick;
	bool m_Explosive;
};

#endif
