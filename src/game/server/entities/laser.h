/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_LASER_H
#define GAME_SERVER_ENTITIES_LASER_H

#include <game/server/entity.h>

class CLaser : public CEntity
{
public:
	CLaser(CGameWorld *pGameWorld, rvec2 Pos, rvec2 Direction, real_t StartEnergy, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

protected:
	bool HitCharacter(rvec2 From, rvec2 To);
	void DoBounce();

private:
	rvec2 m_From;
	rvec2 m_Dir;
	real_t m_Energy;
	int m_Bounces;
	int m_EvalTick;
	int m_Owner;
};

#endif
