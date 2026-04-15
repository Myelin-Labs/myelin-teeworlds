/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BASE_MATH_H
#define BASE_MATH_H

#include <stdlib.h>

#include "system.h"

template<typename T>
inline T clamp(T val, T min, T max)
{
	if(val < min)
		return min;
	if(val > max)
		return max;
	return val;
}

inline float sign(float f)
{
	return f < 0.0f ? -1.0f : 1.0f;
}

inline int round_to_int(float f)
{
	if(f > 0.0f)
		return (int)(f + 0.5f);
	return (int)(f - 0.5f);
}

template<typename T, typename TB>
inline T mix(const T a, const T b, TB amount)
{
	return a + (b - a) * amount;
}

template<typename T, typename TB>
inline T bezier(const T p0, const T p1, const T p2, const T p3, TB amount)
{
	// De-Casteljau Algorithm
	const T c10 = mix(p0, p1, amount);
	const T c11 = mix(p1, p2, amount);
	const T c12 = mix(p2, p3, amount);

	const T c20 = mix(c10, c11, amount);
	const T c21 = mix(c11, c12, amount);

	return mix(c20, c21, amount); // c30
}

inline int random_int()
{
	return ((vm_rand() & 0x7fff) << 16) | (vm_rand() & 0xffff);
}

inline float random_float()
{
	return vm_rand() / (float)RAND_MAX;
}

const int fxpscale = 1 << 10;

// float to fixed
inline int f2fx(float v)
{
	return (int)(v * fxpscale);
}
inline float fx2f(int v)
{
	return v / (float)fxpscale;
}

// int to fixed
inline int i2fx(int v)
{
	return v * fxpscale;
}
inline int fx2i(int v)
{
	return v / fxpscale;
}

inline int gcd(int a, int b)
{
	while(b != 0)
	{
		int c = a % b;
		a = b;
		b = c;
	}
	return a;
}

class fxp
{
	int value;
public:
	void set(int v)
	{
		value = v;
	}
	int get() const
	{
		return value;
	}
	fxp &operator=(int v)
	{
		value = i2fx(v);
		return *this;
	}
	fxp &operator=(float v)
	{
		value = f2fx(v);
		return *this;
	}
	operator int() const
	{
		return fx2i(value);
	}
	operator float() const
	{
		return fx2f(value);
	}
};

const float pi = 3.1415926535897932384626433f;

template<typename T>
inline T minimum(T a, T b)
{
	return a < b ? a : b;
}

template<typename T>
inline T maximum(T a, T b)
{
	return a > b ? a : b;
}

template<typename T>
inline T absolute(T a)
{
	return a < T(0) ? -a : a;
}

#ifdef CONF_FIXED_POINT_MATH
#include "fpm/fixed.hpp"
#include "fpm/math.hpp"

typedef fpm::fixed<std::int64_t, __int128_t, 16> fixed_t;
const fixed_t FIXED_ZERO = fixed_t { 0 };
const fixed_t FIXED_HALF = fixed_t { 0.5f };

inline fixed_t sign(fixed_t f)
{
	return f < FIXED_ZERO ? fixed_t { -1 } : fixed_t { 1 };
}

inline int round_to_int(fixed_t f)
{
	if(f > FIXED_ZERO)
		return static_cast<int>(f + FIXED_HALF);
	return static_cast<int>(f - FIXED_HALF);
}
#endif

#endif // BASE_MATH_H
