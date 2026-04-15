/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BASE_VMATH_H
#define BASE_VMATH_H

#include <math.h>

#include "math.h"	// mix

#ifdef CONF_FIXED_POINT_MATH
typedef fixed_t real_t;
#define REAL(v) (fixed_t { v })
#else
typedef float real_t;
#define REAL(v) ((float) v)
#endif

// ------------------------------------

template<typename T>
class vector2_base
{
public:
	union { T x,u; };
	union { T y,v; };

	vector2_base() {}
	vector2_base(T nx, T ny)
	{
		x = nx;
		y = ny;
	}

	vector2_base operator -() const { return vector2_base(-x, -y); }
	vector2_base operator -(const vector2_base &v) const { return vector2_base(x-v.x, y-v.y); }
	vector2_base operator +(const vector2_base &v) const { return vector2_base(x+v.x, y+v.y); }
	vector2_base operator *(const T v) const { return vector2_base(x*v, y*v); }
	vector2_base operator *(const vector2_base &v) const { return vector2_base(x*v.x, y*v.y); }
	vector2_base operator /(const T v) const { return vector2_base(x/v, y/v); }
	vector2_base operator /(const vector2_base &v) const { return vector2_base(x/v.x, y/v.y); }

	const vector2_base &operator +=(const vector2_base &v) { x += v.x; y += v.y; return *this; }
	const vector2_base &operator -=(const vector2_base &v) { x -= v.x; y -= v.y; return *this; }
	const vector2_base &operator *=(const T v) { x *= v; y *= v; return *this;	}
	const vector2_base &operator *=(const vector2_base &v) { x *= v.x; y *= v.y; return *this; }
	const vector2_base &operator /=(const T v) { x /= v; y /= v; return *this;	}
	const vector2_base &operator /=(const vector2_base &v) { x /= v.x; y /= v.y; return *this; }

	bool operator ==(const vector2_base &v) const { return x == v.x && y == v.y; } //TODO: do this with an eps instead
	bool operator !=(const vector2_base &v) const { return x != v.x || y != v.y; }

	operator const T* () { return &x; }
};

template<typename T>
inline vector2_base<T> rotate(const vector2_base<T> &a, float angle)
{
	angle = angle * pi / 180.0f;
	float s = sinf(angle);
	float c = cosf(angle);
	return vector2_base<T>((T)(c*a.x - s*a.y), (T)(s*a.x + c*a.y));
}

template<typename T>
inline T dot(const vector2_base<T> &a, const vector2_base<T> &b)
{
	return a.x*b.x + a.y*b.y;
}

//
inline float length(const vector2_base<float> &a)
{
	return sqrtf(dot(a, a));
}

inline float length_approximate(const vector2_base<float> &a)
{
  return length(a);
}

inline float angle(const vector2_base<float> &a)
{
	return atan2f(a.y, a.x);
}

inline vector2_base<float> normalize(const vector2_base<float> &v)
{
	return v / length(v);
}

inline vector2_base<float> direction(float angle)
{
	return vector2_base<float>(cosf(angle), sinf(angle));
}

inline float power(float x, float y)
{
  return powf(x, y);
}

inline float cosine(float x)
{
  return cosf(x);
}

inline float sine(float x)
{
  return sinf(x);
}

inline float division(float x, float y)
{
  return x / y;
}

#ifdef CONF_FIXED_POINT_MATH
inline vector2_base<fixed_t> rotate(const vector2_base<fixed_t> &a, fixed_t angle)
{
  angle = angle * fixed_t::pi() / fixed_t { 180.0f };
	fixed_t s = fpm::sin(angle);
	fixed_t c = fpm::cos(angle);
	return vector2_base<fixed_t>(c*a.x - s*a.y, s*a.x + c*a.y);
}

/**
 * Approximates the Euclidean distance using Alpha=1, Beta=0.375.
 * Max error: +6.8% / -1.5%.
 * * This is optimized for environments where sqrt and division are costly.
 * It uses raw bitwise shifts to perform the 0.375 (3/8) multiplication.
 * See https://en.wikipedia.org/wiki/Alpha_max_plus_beta_min_algorithm
 */
inline fixed_t length_approximate(const vector2_base<fixed_t> &a)
{
  fixed_t x = fpm::abs(a.x);
  fixed_t y = fpm::abs(a.y);

  if (x < y) {
    std::swap(x, y);
  }

  auto raw_max = maximum(x, y).raw_value();
  auto raw_min = minimum(x, y).raw_value();

  // Beta = 0.375 = 3/8 = (1/4 + 1/8)
  // (raw_min >> 2) is 0.25 * min
  // (raw_min >> 3) is 0.125 * min
  auto raw_dist = raw_max + (raw_min >> 2) + (raw_min >> 3);

  return fixed_t::from_raw_value(raw_dist);
}

/*
 * After some testing, a full approximated version is fine from game experience,
 * we are switching to approximation of dot length here. This saves us ~47 million cycles.
 */
inline fixed_t length(const vector2_base<fixed_t> &a)
{
  // More precise version
  // return fpm::sqrt(dot(a, a));
	return length_approximate(a);
}

inline fixed_t angle(const vector2_base<fixed_t> &a)
{
  if ((a.x == REAL(0)) && (a.y == REAL(0))) {
    return REAL(0);
  }
	return fpm::atan2(a.y, a.x);
}

inline vector2_base<fixed_t> normalize(const vector2_base<fixed_t> &v)
{
  fixed_t l = length(v);
  if (l != REAL(0)) {
    return v / l;
  }
  return vector2_base<fixed_t>(REAL(0), REAL(0));
}

inline vector2_base<fixed_t> direction(fixed_t angle)
{
	return vector2_base<fixed_t>(fpm::cos(angle), fpm::sin(angle));
}

inline fixed_t power(fixed_t x, fixed_t y)
{
  return fpm::pow(x, y);
}

inline fixed_t cosine(fixed_t x)
{
  return fpm::cos(x);
}

inline fixed_t sine(fixed_t x)
{
  return fpm::sin(x);
}

inline fixed_t division(fixed_t x, fixed_t y)
{
  if (y == REAL(0)) {
    return REAL(0);
  }
  return x / y;
}
#endif

inline real_t random_real()
{
  return REAL(vm_rand()) / REAL(RAND_MAX);
}

typedef vector2_base<real_t> rvec2;
typedef vector2_base<float> vec2;
typedef vector2_base<bool> bvec2;
typedef vector2_base<int> ivec2;

inline rvec2 fvec2_to_rvec2(vec2 v) {
  return rvec2(REAL(v.x), REAL(v.y));
}

inline vec2 rvec2_to_fvec2(rvec2 v) {
  return vec2(static_cast<float>(v.x), static_cast<float>(v.y));
}

template<typename T>
inline T distance(const vector2_base<T> &a, const vector2_base<T> &b)
{
	return length(a - b);
}

template<typename T>
inline T distance_approximate(const vector2_base<T> &a, const vector2_base<T> &b)
{
	return length_approximate(a - b);
}

template<typename T>
inline vector2_base<T> closest_point_on_line(vector2_base<T> line_point0, vector2_base<T> line_point1, vector2_base<T> target_point)
{
	vector2_base<T> c = target_point - line_point0;
	vector2_base<T> v = line_point1 - line_point0;
	v = normalize(v);
	T d = length(line_point0 - line_point1);
	T t = division(dot(v, c), d);
	return mix(line_point0, line_point1, clamp(t, (T)0, (T)1));
}

// ------------------------------------
template<typename T>
class vector3_base
{
public:
	union { T x,r,h; };
	union { T y,g,s; };
	union { T z,b,v,l; };

	vector3_base() {}
	vector3_base(T nx, T ny, T nz)
	{
		x = nx;
		y = ny;
		z = nz;
	}

	vector3_base operator -(const vector3_base &v) const { return vector3_base(x-v.x, y-v.y, z-v.z); }
	vector3_base operator -() const { return vector3_base(-x, -y, -z); }
	vector3_base operator +(const vector3_base &v) const { return vector3_base(x+v.x, y+v.y, z+v.z); }
	vector3_base operator *(const T v) const { return vector3_base(x*v, y*v, z*v); }
	vector3_base operator *(const vector3_base &v) const { return vector3_base(x*v.x, y*v.y, z*v.z); }
	vector3_base operator /(const T v) const { return vector3_base(x/v, y/v, z/v); }
	vector3_base operator /(const vector3_base &v) const { return vector3_base(x/v.x, y/v.y, z/v.z); }

	const vector3_base &operator +=(const vector3_base &v) { x += v.x; y += v.y; z += v.z; return *this; }
	const vector3_base &operator -=(const vector3_base &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
	const vector3_base &operator *=(const T v) { x *= v; y *= v; z *= v; return *this;	}
	const vector3_base &operator *=(const vector3_base &v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
	const vector3_base &operator /=(const T v) { x /= v; y /= v; z /= v; return *this;	}
	const vector3_base &operator /=(const vector3_base &v) { x /= v.x; y /= v.y; z /= v.z; return *this; }

	bool operator ==(const vector3_base &v) const { return x == v.x && y == v.y && z == v.z; } //TODO: do this with an eps instead
	bool operator !=(const vector3_base &v) const { return x != v.x || y != v.y || z != v.z; }

	operator const T* () { return &x; }
};

template<typename T>
inline T distance(const vector3_base<T> &a, const vector3_base<T> &b)
{
	return length(a - b);
}

template<typename T>
inline T dot(const vector3_base<T> &a, const vector3_base<T> &b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

template<typename T>
inline vector3_base<T> cross(const vector3_base<T> &a, const vector3_base<T> &b)
{
	return vector3_base<T>(
		a.y*b.z - a.z*b.y,
		a.z*b.x - a.x*b.z,
		a.x*b.y - a.y*b.x);
}

//
inline float length(const vector3_base<float> &a)
{
	return sqrtf(dot(a, a));
}

inline vector3_base<float> normalize(const vector3_base<float> &v)
{
	return v / length(v);
}

typedef vector3_base<float> vec3;
typedef vector3_base<bool> bvec3;
typedef vector3_base<int> ivec3;

// ------------------------------------

template<typename T>
class vector4_base
{
public:
	union { T x,r; };
	union { T y,g; };
	union { T z,b; };
	union { T w,a; };

	vector4_base() {}
	vector4_base(T nx, T ny, T nz, T nw)
	{
		x = nx;
		y = ny;
		z = nz;
		w = nw;
	}

	vector4_base operator +(const vector4_base &v) const { return vector4_base(x+v.x, y+v.y, z+v.z, w+v.w); }
	vector4_base operator -(const vector4_base &v) const { return vector4_base(x-v.x, y-v.y, z-v.z, w-v.w); }
	vector4_base operator -() const { return vector4_base(-x, -y, -z, -w); }
	vector4_base operator *(const vector4_base &v) const { return vector4_base(x*v.x, y*v.y, z*v.z, w*v.w); }
	vector4_base operator *(const T v) const { return vector4_base(x*v, y*v, z*v, w*v); }
	vector4_base operator /(const vector4_base &v) const { return vector4_base(x/v.x, y/v.y, z/v.z, w/v.w); }
	vector4_base operator /(const T v) const { return vector4_base(x/v, y/v, z/v, w/v); }

	const vector4_base &operator +=(const vector4_base &v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
	const vector4_base &operator -=(const vector4_base &v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
	const vector4_base &operator *=(const T v) { x *= v; y *= v; z *= v; w *= v; return *this;	}
	const vector4_base &operator *=(const vector4_base &v) { x *= v.x; y *= v.y; z *= v.z; w *= v.w; return *this; }
	const vector4_base &operator /=(const T v) { x /= v; y /= v; z /= v; w /= v; return *this;	}
	const vector4_base &operator /=(const vector4_base &v) { x /= v.x; y /= v.y; z /= v.z; w /= v.w; return *this; }

	bool operator ==(const vector4_base &v) const { return x == v.x && y == v.y && z == v.z && w == v.w; } //TODO: do this with an eps instead
	bool operator !=(const vector4_base &v) const { return x != v.x || y != v.y || z != v.z || w != v.w; }

	operator const T* () { return &x; }
};

typedef vector4_base<float> vec4;
typedef vector4_base<bool> bvec4;
typedef vector4_base<int> ivec4;

#endif
