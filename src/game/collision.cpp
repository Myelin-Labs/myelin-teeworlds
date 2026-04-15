/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <base/math.h>
#include <base/vmath.h>

#include <math.h>
#include <engine/map.h>
#include <engine/kernel.h>

#include <game/mapitems.h>
#include <game/layers.h>
#include <game/collision.h>

CCollision::CCollision()
{
	m_pTiles = 0;
	m_Width = 0;
	m_Height = 0;
	m_pLayers = 0;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	m_Width = m_pLayers->GameLayer()->m_Width;
	m_Height = m_pLayers->GameLayer()->m_Height;
	m_pTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->GameLayer()->m_Data));

	for(int i = 0; i < m_Width*m_Height; i++)
	{
		int Index = m_pTiles[i].m_Index;

		if(Index > 128)
			continue;

		switch(Index)
		{
		case TILE_DEATH:
			m_pTiles[i].m_Index = COLFLAG_DEATH;
			break;
		case TILE_SOLID:
			m_pTiles[i].m_Index = COLFLAG_SOLID;
			break;
		case TILE_NOHOOK:
			m_pTiles[i].m_Index = COLFLAG_SOLID|COLFLAG_NOHOOK;
			break;
		default:
			m_pTiles[i].m_Index = 0;
		}
	}
}

int CCollision::GetTile(int x, int y) const
{
	int Nx = clamp(x/32, 0, m_Width-1);
	int Ny = clamp(y/32, 0, m_Height-1);

	return m_pTiles[Ny*m_Width+Nx].m_Index > 128 ? 0 : m_pTiles[Ny*m_Width+Nx].m_Index;
}

bool CCollision::IsTile(int x, int y, int Flag) const
{
	return GetTile(x, y)&Flag;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(rvec2 Pos0, rvec2 Pos1, rvec2 *pOutCollision, rvec2 *pOutBeforeCollision) const
{
	const int End = static_cast<int>(distance_approximate(Pos0, Pos1)+1);
	const real_t InverseEnd = REAL(1.0f)/End;
	rvec2 Last = Pos0;

	for(int i = 0; i <= End; i++)
	{
		rvec2 Pos = mix(Pos0, Pos1, i*InverseEnd);
		if(CheckPoint(Pos.x, Pos.y))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(rvec2 *pInoutPos, rvec2 *pInoutVel, real_t Elasticity, int *pBounces) const
{
	if(pBounces)
		*pBounces = 0;

	rvec2 Pos = *pInoutPos;
	rvec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

// Optimized TestBox implementation written by Gemini
bool CCollision::TestBox(rvec2 Pos, rvec2 Size, int Flag) const
{
  rvec2 HalfSize = rvec2(Size.x / REAL(2.0), Size.y / REAL(2.0));

  // Calculate the 4 absolute corners
  int x1 = round_to_int(Pos.x - HalfSize.x);
  int x2 = round_to_int(Pos.x + HalfSize.x);
  int y1 = round_to_int(Pos.y - HalfSize.y);
  int y2 = round_to_int(Pos.y + HalfSize.y);

  // Check ONLY the 4 corners directly via IsTile.
  // Skipping CheckPoint avoids redundant float multiplication/division inside the engine.
  if(IsTile(x1, y1, Flag)) return true;
  if(IsTile(x2, y1, Flag)) return true;
  if(IsTile(x1, y2, Flag)) return true;
  if(IsTile(x2, y2, Flag)) return true;

  return false;
}

void CCollision::MoveBox(rvec2 *pInoutPos, rvec2 *pInoutVel, rvec2 Size, real_t Elasticity, bool *pDeath) const
{
	// do the move
	rvec2 Pos = *pInoutPos;
	rvec2 Vel = *pInoutVel;

	const real_t Distance = length(Vel);
	const int Max = (int)Distance;

	if(pDeath)
		*pDeath = false;

	if(Distance > REAL(0.00001f))
	{
		const real_t Fraction = REAL(1.0f)/(Max+1);
		for(int i = 0; i <= Max; i++)
		{
			rvec2 NewPos = Pos + Vel*Fraction; // TODO: this row is not nice

			//You hit a deathtile, congrats to that :)
			//Deathtiles are a bit smaller
			if(pDeath && TestBox(rvec2(NewPos.x, NewPos.y), Size*REAL(2.0f/3.0f), COLFLAG_DEATH))
			{
				*pDeath = true;
			}

			if(TestBox(rvec2(NewPos.x, NewPos.y), Size))
			{
				int Hits = 0;

				if(TestBox(rvec2(Pos.x, NewPos.y), Size))
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}

				if(TestBox(rvec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}
