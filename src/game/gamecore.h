/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_GAMECORE_H
#define GAME_GAMECORE_H

#include <base/system.h>
#include <base/math.h>

#include <math.h>
#include "collision.h"
#include <engine/console.h>
#include <engine/shared/protocol.h>
#include <generated/protocol.h>

class CTuneParam
{
	int m_Value;
public:
	void Set(int v) { m_Value = v; }
	int Get() const { return m_Value; }
	CTuneParam &operator = (int v) { m_Value = v*100; return *this; }
	CTuneParam &operator = (real_t v) { m_Value = static_cast<int>(v*REAL(100.0f)); return *this; }
	operator real_t() const { return m_Value / REAL(100.0f); }
	operator bool() const { return m_Value != 0; }
};

class CTuningParams
{
	static const char *s_apNames[];
public:
	CTuningParams()
	{
		const real_t TicksPerSecond = REAL(50.0f);
		#define MACRO_TUNING_PARAM(Name,ScriptName,Value) m_##Name.Set(static_cast<int>(Value*REAL(100.0f)));
		#include "tuning.h"
		#undef MACRO_TUNING_PARAM
	}

	#define MACRO_TUNING_PARAM(Name,ScriptName,Value) CTuneParam m_##Name;
	#include "tuning.h"
	#undef MACRO_TUNING_PARAM

	static int Num() { return sizeof(CTuningParams)/sizeof(CTuneParam); }
	bool Set(int Index, real_t Value);
	bool Set(const char *pName, real_t Value);
	bool Get(int Index, real_t *pValue) const;
	bool Get(const char *pName, real_t *pValue) const;
	const char *GetName(int Index) const { return s_apNames[Index]; }
	int PossibleTunings(const char *pStr, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = 0);
};

inline void StrToInts(int *pInts, int Num, const char *pStr)
{
	int Index = 0;
	while(Num)
	{
		char aBuf[4] = {0,0,0,0};
		for(int c = 0; c < 4 && pStr[Index]; c++, Index++)
			aBuf[c] = pStr[Index];
		*pInts = ((aBuf[0]+128)<<24)|((aBuf[1]+128)<<16)|((aBuf[2]+128)<<8)|(aBuf[3]+128);
		pInts++;
		Num--;
	}

	// null terminate
	pInts[-1] &= 0xffffff00;
}

inline void IntsToStr(const int *pInts, int Num, char *pStr)
{
	while(Num)
	{
		pStr[0] = (((*pInts)>>24)&0xff)-128;
		pStr[1] = (((*pInts)>>16)&0xff)-128;
		pStr[2] = (((*pInts)>>8)&0xff)-128;
		pStr[3] = ((*pInts)&0xff)-128;
		pStr += 4;
		pInts++;
		Num--;
	}

	// null terminate
	pStr[-1] = 0;
}



inline rvec2 CalcPos(rvec2 Pos, rvec2 Velocity, real_t Curvature, real_t Speed, real_t Time)
{
	rvec2 n;
	Time *= Speed;
	n.x = Pos.x + Velocity.x*Time;
	n.y = Pos.y + Velocity.y*Time + Curvature/10000*(Time*Time);
	return n;
}


template<typename T>
inline T SaturatedAdd(T Min, T Max, T Current, T Modifier)
{
	if(Modifier < REAL(0))
	{
		if(Current < Min)
			return Current;
		Current += Modifier;
		if(Current < Min)
			Current = Min;
		return Current;
	}
	else
	{
		if(Current > Max)
			return Current;
		Current += Modifier;
		if(Current > Max)
			Current = Max;
		return Current;
	}
}


real_t VelocityRamp(real_t Value, real_t Start, real_t Range, real_t Curvature);

// hooking stuff
enum
{
	HOOK_RETRACTED=-1,
	HOOK_IDLE=0,
	HOOK_RETRACT_START=1,
	HOOK_RETRACT_END=3,
	HOOK_FLYING,
	HOOK_GRABBED,
};

class CWorldCore
{
public:
	CWorldCore()
	{
		mem_zero(m_apCharacters, sizeof(m_apCharacters));
	}

	CTuningParams m_Tuning;
	class CCharacterCore *m_apCharacters[MAX_CLIENTS];
};

class CCharacterCore
{
	CWorldCore *m_pWorld;
	CCollision *m_pCollision;
public:
	static const real_t PHYS_SIZE;
	rvec2 m_Pos;
	rvec2 m_Vel;

	rvec2 m_HookDragVel;

	rvec2 m_HookPos;
	rvec2 m_HookDir;
	int m_HookTick;
	int m_HookState;
	int m_HookedPlayer;

	int m_Jumped;

	int m_Direction;
	int m_Angle;

	bool m_Death;

	CNetObj_PlayerInput m_Input;

	int m_TriggeredEvents;

	void Init(CWorldCore *pWorld, CCollision *pCollision);
	void Reset();
	void Tick(bool UseInput);
	void Move();

	void AddDragVelocity();
	void ResetDragVelocity();

	void Read(const CNetObj_CharacterCore *pObjCore);
	void Write(CNetObj_CharacterCore *pObjCore) const;
	void Quantize();
};

#endif
