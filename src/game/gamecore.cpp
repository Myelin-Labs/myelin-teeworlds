/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "gamecore.h"

const char *CTuningParams::s_apNames[] =
{
	#define MACRO_TUNING_PARAM(Name,ScriptName,Value) #ScriptName,
	#include "tuning.h"
	#undef MACRO_TUNING_PARAM
};


bool CTuningParams::Set(int Index, real_t Value)
{
	if(Index < 0 || Index >= Num())
		return false;
	((CTuneParam *)this)[Index] = Value;
	return true;
}

bool CTuningParams::Get(int Index, real_t *pValue) const
{
	if(Index < 0 || Index >= Num())
		return false;
	*pValue = ((CTuneParam *)this)[Index];
	return true;
}

bool CTuningParams::Set(const char *pName, real_t Value)
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, GetName(i)) == 0)
			return Set(i, Value);
	return false;
}

bool CTuningParams::Get(const char *pName, real_t *pValue) const
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, GetName(i)) == 0)
			return Get(i, pValue);
	return false;
}

int CTuningParams::PossibleTunings(const char *pStr, IConsole::FPossibleCallback pfnCallback, void *pUser)
{
	int Index = 0;
	for(int i = 0; i < Num(); i++)
	{
		if(str_find_nocase(GetName(i), pStr))
		{
			pfnCallback(Index, GetName(i), pUser);
			Index++;
		}
	}
	return Index;
}


real_t VelocityRamp(real_t Value, real_t Start, real_t Range, real_t Curvature)
{
	if(Value < Start)
		return REAL(1.0f);
	return REAL(1.0f)/power(Curvature, (Value-Start)/Range);
}

const real_t CCharacterCore::PHYS_SIZE = REAL(28.0f);

void CCharacterCore::Init(CWorldCore *pWorld, CCollision *pCollision)
{
	m_pWorld = pWorld;
	m_pCollision = pCollision;
}

void CCharacterCore::Reset()
{
	m_Pos = rvec2(REAL(0),REAL(0));
	m_Vel = rvec2(REAL(0),REAL(0));
	m_HookDragVel = rvec2(REAL(0),REAL(0));
	m_HookPos = rvec2(REAL(0),REAL(0));
	m_HookDir = rvec2(REAL(0),REAL(0));
	m_HookTick = 0;
	m_HookState = HOOK_IDLE;
	m_HookedPlayer = -1;
	m_Jumped = 0;
	m_TriggeredEvents = 0;
	m_Death = false;
}

void CCharacterCore::Tick(bool UseInput)
{
	m_TriggeredEvents = 0;

	// get ground state
	const bool Grounded =
		m_pCollision->CheckPoint(m_Pos.x+PHYS_SIZE/2, m_Pos.y+PHYS_SIZE/2+5)
		|| m_pCollision->CheckPoint(m_Pos.x-PHYS_SIZE/2, m_Pos.y+PHYS_SIZE/2+5);

	rvec2 TargetDirection = normalize(rvec2(REAL(m_Input.m_TargetX), REAL(m_Input.m_TargetY)));

	m_Vel.y += m_pWorld->m_Tuning.m_Gravity;

	real_t MaxSpeed = Grounded ? m_pWorld->m_Tuning.m_GroundControlSpeed : m_pWorld->m_Tuning.m_AirControlSpeed;
	real_t Accel = Grounded ? m_pWorld->m_Tuning.m_GroundControlAccel : m_pWorld->m_Tuning.m_AirControlAccel;
	real_t Friction = Grounded ? m_pWorld->m_Tuning.m_GroundFriction : m_pWorld->m_Tuning.m_AirFriction;

	// handle input
	if(UseInput)
	{
		m_Direction = m_Input.m_Direction;
		m_Angle = static_cast<int>(angle(rvec2(REAL(m_Input.m_TargetX), REAL(m_Input.m_TargetY)))*REAL(256.0f));

		// handle jump
		if(m_Input.m_Jump)
		{
			if(!(m_Jumped&1))
			{
				if(Grounded)
				{
					m_TriggeredEvents |= COREEVENTFLAG_GROUND_JUMP;
					m_Vel.y = - ((real_t) m_pWorld->m_Tuning.m_GroundJumpImpulse);
					m_Jumped |= 1;
				}
				else if(!(m_Jumped&2))
				{
					m_TriggeredEvents |= COREEVENTFLAG_AIR_JUMP;
					m_Vel.y = - ((real_t) m_pWorld->m_Tuning.m_AirJumpImpulse);
					m_Jumped |= 3;
				}
			}
		}
		else
			m_Jumped &= ~1;

		// handle hook
		if(m_Input.m_Hook)
		{
			if(m_HookState == HOOK_IDLE)
			{
				m_HookState = HOOK_FLYING;
				m_HookPos = m_Pos+TargetDirection*PHYS_SIZE*REAL(1.5f);
				m_HookDir = TargetDirection;
				m_HookedPlayer = -1;
				m_HookTick = 0;
				//m_TriggeredEvents |= COREEVENTFLAG_HOOK_LAUNCH;
			}
		}
		else
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_IDLE;
			m_HookPos = m_Pos;
		}
	}

	// add the speed modification according to players wanted direction
	if(m_Direction < 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, -Accel);
	if(m_Direction > 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, Accel);
	if(m_Direction == 0)
		m_Vel.x *= Friction;

	// handle jumping
	// 1 bit = to keep track if a jump has been made on this input
	// 2 bit = to keep track if a air-jump has been made
	if(Grounded)
		m_Jumped &= ~2;

	// do hook
	if(m_HookState == HOOK_IDLE)
	{
		m_HookedPlayer = -1;
		m_HookState = HOOK_IDLE;
		m_HookPos = m_Pos;
	}
	else if(m_HookState >= HOOK_RETRACT_START && m_HookState < HOOK_RETRACT_END)
	{
		m_HookState++;
	}
	else if(m_HookState == HOOK_RETRACT_END)
	{
		m_HookState = HOOK_RETRACTED;
		//m_TriggeredEvents |= COREEVENTFLAG_HOOK_RETRACT;
	}
	else if(m_HookState == HOOK_FLYING)
	{
		rvec2 NewPos = m_HookPos+m_HookDir*m_pWorld->m_Tuning.m_HookFireSpeed;

		if(distance(m_Pos, NewPos) > (real_t) m_pWorld->m_Tuning.m_HookLength)
		{
			m_HookState = HOOK_RETRACT_START;
			NewPos = m_Pos + normalize(NewPos-m_Pos) * m_pWorld->m_Tuning.m_HookLength;
		}

		// make sure that the hook doesn't go though the ground
		bool GoingToHitGround = false;
		bool GoingToRetract = false;
		int Hit = m_pCollision->IntersectLine(m_HookPos, NewPos, &NewPos, 0);
		if(Hit)
		{
			if(Hit&CCollision::COLFLAG_NOHOOK)
				GoingToRetract = true;
			else
				GoingToHitGround = true;
		}

		// Check against other players first
		if(m_pWorld && m_pWorld->m_Tuning.m_PlayerHooking)
		{
			real_t Distance = REAL(0);
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
				if(!pCharCore || pCharCore == this)
					continue;

				rvec2 ClosestPoint = closest_point_on_line(m_HookPos, NewPos, pCharCore->m_Pos);

				if(distance(pCharCore->m_Pos, ClosestPoint) < PHYS_SIZE+REAL(2.0f))
				{
  				real_t Current = distance(m_HookPos, pCharCore->m_Pos);
					if(m_HookedPlayer == -1 || Current < Distance)
					{
						m_TriggeredEvents |= COREEVENTFLAG_HOOK_ATTACH_PLAYER;
						m_HookState = HOOK_GRABBED;
						m_HookedPlayer = i;
						Distance = Current;
					}
				}
			}
		}

		if(m_HookState == HOOK_FLYING)
		{
			// check against ground
			if(GoingToHitGround)
			{
				m_TriggeredEvents |= COREEVENTFLAG_HOOK_ATTACH_GROUND;
				m_HookState = HOOK_GRABBED;
			}
			else if(GoingToRetract)
			{
				m_TriggeredEvents |= COREEVENTFLAG_HOOK_HIT_NOHOOK;
				m_HookState = HOOK_RETRACT_START;
			}

			m_HookPos = NewPos;
		}
	}

	if(m_HookState == HOOK_GRABBED)
	{
		if(m_HookedPlayer != -1)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[m_HookedPlayer];
			if(pCharCore)
				m_HookPos = pCharCore->m_Pos;
			else
			{
				// release hook
				m_HookedPlayer = -1;
				m_HookState = HOOK_RETRACTED;
				m_HookPos = m_Pos;
			}

			// keep players hooked for a max of 1.5sec
			//if(Server()->Tick() > hook_tick+(Server()->TickSpeed()*3)/2)
				//release_hooked();
		}

		// don't do this hook routine when we are already hooked to a player
		if(m_HookedPlayer == -1 && distance(m_HookPos, m_Pos) > REAL(46.0f))
		{
			rvec2 HookVel = normalize(m_HookPos-m_Pos)*m_pWorld->m_Tuning.m_HookDragAccel;
			// the hook as more power to drag you up then down.
			// this makes it easier to get on top of an platform
			if(HookVel.y > REAL(0))
				HookVel.y *= REAL(0.3f);

			// the hook will boost it's power if the player wants to move
			// in that direction. otherwise it will dampen everything abit
			if((HookVel.x < REAL(0) && m_Direction < 0) || (HookVel.x > REAL(0) && m_Direction > 0))
				HookVel.x *= REAL(0.95f);
			else
				HookVel.x *= REAL(0.75f);

			rvec2 NewVel = m_Vel+HookVel;

			// check if we are under the legal limit for the hook
			if(length(NewVel) < (real_t) m_pWorld->m_Tuning.m_HookDragSpeed || length(NewVel) < length(m_Vel))
				m_Vel = NewVel; // no problem. apply

		}

		// release hook (max hook time is 1.25
		m_HookTick++;
		if(m_HookedPlayer != -1 && (m_HookTick > SERVER_TICK_SPEED+SERVER_TICK_SPEED/5 || !m_pWorld->m_apCharacters[m_HookedPlayer]))
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_RETRACTED;
			m_HookPos = m_Pos;
		}
	}

	if(m_pWorld)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
			if(!pCharCore)
				continue;

			//player *p = (player*)ent;
			if(pCharCore == this) // || !(p->flags&FLAG_ALIVE)
				continue; // make sure that we don't nudge our self

			// handle player <-> player collision

      // This is actually:
      //
      // real_t Distance = distance(m_Pos, pCharCore->m_Pos);
      // rvec2 Dir = normalize(m_Pos - pCharCore->m_Pos);
      //
			// We break the calculation down, so we can reuse intermediate values.
			real_t Distance;
			rvec2 Dir;
			{
			  rvec2 Diff = m_Pos - pCharCore->m_Pos;
  			Distance = length(Diff);
  			Dir = Diff / Distance;
			}

			if(m_pWorld->m_Tuning.m_PlayerCollision &&
  			Distance < PHYS_SIZE*REAL(1.25f) && Distance > REAL(0.0f))
			{
				real_t a = (PHYS_SIZE*REAL(1.45f) - Distance);
				real_t Velocity = REAL(0.5f);

				// make sure that we don't add excess force by checking the
				// direction against the current velocity. if not zero.
				if(length(m_Vel) > REAL(0.0001))
					Velocity = 1-(dot(normalize(m_Vel), Dir)+1)/2;

				m_Vel += Dir*a*(Velocity*REAL(0.75f));
				m_Vel *= REAL(0.85f);
			}

			// handle hook influence
			if(m_HookedPlayer == i && m_pWorld->m_Tuning.m_PlayerHooking)
			{
				if(Distance > PHYS_SIZE*REAL(1.50f)) // TODO: fix tweakable variable
				{
					real_t Accel = ((real_t) m_pWorld->m_Tuning.m_HookDragAccel) * (Distance/((real_t) m_pWorld->m_Tuning.m_HookLength));

					// add force to the hooked player
					pCharCore->m_HookDragVel += Dir*Accel*REAL(1.5f);

					// add a little bit force to the guy who has the grip
					m_HookDragVel -= Dir*Accel*REAL(0.25f);
				}
			}
		}
	}

	// clamp the velocity to something sane
	if(length(m_Vel) > REAL(6000))
		m_Vel = normalize(m_Vel) * REAL(6000);
}

void CCharacterCore::AddDragVelocity()
{
	// Apply hook interaction velocity
	real_t DragSpeed = m_pWorld->m_Tuning.m_HookDragSpeed;

	m_Vel.x = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.x, m_HookDragVel.x);
	m_Vel.y = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.y, m_HookDragVel.y);
}

void CCharacterCore::ResetDragVelocity()
{
	m_HookDragVel = rvec2(REAL(0),REAL(0));
}

void CCharacterCore::Move()
{
	if(!m_pWorld)
		return;

	real_t RampValue = VelocityRamp(length(m_Vel)*50, m_pWorld->m_Tuning.m_VelrampStart, m_pWorld->m_Tuning.m_VelrampRange, m_pWorld->m_Tuning.m_VelrampCurvature);

	m_Vel.x = m_Vel.x*RampValue;

	rvec2 NewPos = m_Pos;
	m_pCollision->MoveBox(&NewPos, &m_Vel, rvec2(PHYS_SIZE, PHYS_SIZE), REAL(0), &m_Death);

	m_Vel.x = m_Vel.x*(REAL(1.0f)/RampValue);

	if(m_pWorld->m_Tuning.m_PlayerCollision)
	{
		// check player collision
		// We are only using distance as iteration steps, an approximated version works.
		real_t Distance = distance_approximate(m_Pos, NewPos);
		int End = static_cast<int>(Distance+1);
		rvec2 LastPos = m_Pos;
		for(int i = 0; i < End; i++)
		{
			real_t a = division(REAL(i), Distance);
			rvec2 Pos = mix(m_Pos, NewPos, a);
			for(int p = 0; p < MAX_CLIENTS; p++)
			{
				CCharacterCore *pCharCore = m_pWorld->m_apCharacters[p];
				if(!pCharCore || pCharCore == this)
					continue;
				real_t D = distance(Pos, pCharCore->m_Pos);
				if(D < PHYS_SIZE && D >= REAL(0.0f))
				{
					if(a > REAL(0.0f))
						m_Pos = LastPos;
					else if(distance(NewPos, pCharCore->m_Pos) > D)
						m_Pos = NewPos;
					return;
				}
			}
			LastPos = Pos;
		}
	}

	m_Pos = NewPos;
}

void CCharacterCore::Write(CNetObj_CharacterCore *pObjCore) const
{
	pObjCore->m_X = round_to_int(m_Pos.x);
	pObjCore->m_Y = round_to_int(m_Pos.y);

	pObjCore->m_VelX = round_to_int(m_Vel.x*REAL(256.0f));
	pObjCore->m_VelY = round_to_int(m_Vel.y*REAL(256.0f));
	pObjCore->m_HookState = m_HookState;
	pObjCore->m_HookTick = m_HookTick;
	pObjCore->m_HookX = round_to_int(m_HookPos.x);
	pObjCore->m_HookY = round_to_int(m_HookPos.y);
	pObjCore->m_HookDx = round_to_int(m_HookDir.x*REAL(256.0f));
	pObjCore->m_HookDy = round_to_int(m_HookDir.y*REAL(256.0f));
	pObjCore->m_HookedPlayer = m_HookedPlayer;
	pObjCore->m_Jumped = m_Jumped;
	pObjCore->m_Direction = m_Direction;
	pObjCore->m_Angle = m_Angle;
}

void CCharacterCore::Read(const CNetObj_CharacterCore *pObjCore)
{
	m_Pos.x = REAL(pObjCore->m_X);
	m_Pos.y = REAL(pObjCore->m_Y);
	m_Vel.x = REAL(pObjCore->m_VelX)/REAL(256.0f);
	m_Vel.y = REAL(pObjCore->m_VelY)/REAL(256.0f);
	m_HookState = pObjCore->m_HookState;
	m_HookTick = pObjCore->m_HookTick;
	m_HookPos.x = REAL(pObjCore->m_HookX);
	m_HookPos.y = REAL(pObjCore->m_HookY);
	m_HookDir.x = REAL(pObjCore->m_HookDx)/REAL(256.0f);
	m_HookDir.y = REAL(pObjCore->m_HookDy)/REAL(256.0f);
	m_HookedPlayer = pObjCore->m_HookedPlayer;
	m_Jumped = pObjCore->m_Jumped;
	m_Direction = pObjCore->m_Direction;
	m_Angle = pObjCore->m_Angle;
}

void CCharacterCore::Quantize()
{
	CNetObj_CharacterCore Core;
	Write(&Core);
	Read(&Core);
}
