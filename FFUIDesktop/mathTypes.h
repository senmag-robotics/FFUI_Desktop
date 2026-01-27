#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>

const float DEG2RAD = M_PI / 180.0f;
const float RAD2DEG = 180.0f / M_PI;


struct Vector2 {
    float x, y;

    // Constructor
    Vector2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}

    // Unary minus
    Vector2 operator-() const {
        return Vector2(-x, -y);
    }

    // Vector + Vector
    Vector2 operator+(const Vector2& rhs) const {
        return Vector2(x + rhs.x, y + rhs.y);
    }

    // Vector - Vector
    Vector2 operator-(const Vector2& rhs) const {
        return Vector2(x - rhs.x, y - rhs.y);
    }

    // Scalar * Vector
    Vector2 operator*(float scalar) const {
        return Vector2(x * scalar, y * scalar);
    }

    // Vector * Scalar (friend so scalar * v works too)
    friend Vector2 operator*(float scalar, const Vector2& v) {
        return v * scalar;
    }

    // Scalar division
    Vector2 operator/(float scalar) const {
        return Vector2(x / scalar, y / scalar);
    }

    // Compound operators (+=, -=, *=, /=)
    Vector2& operator+=(const Vector2& rhs) {
        x += rhs.x; y += rhs.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& rhs) {
        x -= rhs.x; y -= rhs.y;
        return *this;
    }

    Vector2& operator*=(float scalar) {
        x *= scalar; y *= scalar;
        return *this;
    }

    Vector2& operator/=(float scalar) {
        x /= scalar; y /= scalar;
        return *this;
    }

    // Dot product
    float dot(const Vector2& rhs) const {
        return x * rhs.x + y * rhs.y;
    }

    // 2D "cross product" (returns scalar = magnitude of 3D z-component)
    float cross(const Vector2& rhs) const {
        return x * rhs.y - y * rhs.x;
    }

    // Length (magnitude)
    float length() const {
        return std::sqrt(x * x + y * y);
    }

    // Normalized vector (unit vector)
    Vector2 normalized() const {
        float len = length();
        return (len > 0) ? *this / len : Vector2(0, 0);
    }
};


struct Vector3 {
    float x, y, z;

    // Constructors
    Vector3(float x_ = 0, float y_ = 0, float z_ = 0) : x(x_), y(y_), z(z_) {}

    // Unary minus
    Vector3 operator-() const {
        return Vector3(-x, -y, -z);
    }

    // Vector + Vector
    Vector3 operator+(const Vector3& rhs) const {
        return Vector3(x + rhs.x, y + rhs.y, z + rhs.z);
    }

    // Vector - Vector
    Vector3 operator-(const Vector3& rhs) const {
        return Vector3(x - rhs.x, y - rhs.y, z - rhs.z);
    }

    // Scalar * Vector
    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    // Vector * Scalar (friend so scalar * v works too)
    friend Vector3 operator*(float scalar, const Vector3& v) {
        return v * scalar;
    }

    // Scalar division
    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    // Compound operators (+=, -=, *=, /=)
    Vector3& operator+=(const Vector3& rhs) {
        x += rhs.x; y += rhs.y; z += rhs.z;
        return *this;
    }

    Vector3& operator-=(const Vector3& rhs) {
        x -= rhs.x; y -= rhs.y; z -= rhs.z;
        return *this;
    }

    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }

    Vector3& operator/=(float scalar) {
        x /= scalar; y /= scalar; z /= scalar;
        return *this;
    }

    // Dot product
    float dot(const Vector3& rhs) const {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    // Cross product
    Vector3 cross(const Vector3& rhs) const {
        return Vector3(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }

    // Length (magnitude)
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    // Normalized vector (unit vector)
    Vector3 normalized() const {
        float len = length();
        return (len > 0) ? *this / len : Vector3(0, 0, 0);
    }
};

struct Quaternion {
    float w, i, j, k;

    Quaternion(float w_ = 1, float i_ = 0, float j_ = 0, float k_ = 0)
        : w(w_), i(i_), j(j_), k(k_) {}

    // Normalize quaternion
    Quaternion normalized() const {
        float mag = std::sqrt(w * w + i * i + j * j + k * k);
        return (mag > 0) ? Quaternion(w / mag, i / mag, j / mag, k / mag) : Quaternion(1, 0, 0, 0);
    }

    Quaternion& normalize() {
        float mag = std::sqrt(w * w + i * i + j * j + k * k);
        if (mag > 0) {
            w /= mag; i /= mag; j /= mag; k /= mag;
        }
        return *this;
    }

    // Conjugate (inverse for unit quaternion)
    Quaternion conjugate() const {
        return Quaternion(w, -i, -j, -k);
    }

    // True inverse (for non-unit quaternions)
    Quaternion inverse() const {
        float normSq = w * w + i * i + j * j + k * k;
        if (normSq > 0) {
            float invNorm = 1.0f / normSq;
            return Quaternion(w * invNorm, -i * invNorm, -j * invNorm, -k * invNorm);
        }
        return Quaternion(1, 0, 0, 0);
    }

    // Quaternion multiplication (rotation composition)
    Quaternion operator*(const Quaternion& rhs) const {
        return Quaternion(
            w * rhs.w - i * rhs.i - j * rhs.j - k * rhs.k,
            w * rhs.i + i * rhs.w + j * rhs.k - k * rhs.j,
            w * rhs.j - i * rhs.k + j * rhs.w + k * rhs.i,
            w * rhs.k + i * rhs.j - j * rhs.i + k * rhs.w
        );
    }

    // Rotate a vector by this quaternion
    Vector3 rotate(const Vector3& v) const {
        Quaternion qv(0, v.x, v.y, v.z);
        Quaternion res = (*this) * qv * this->inverse();
        return Vector3(res.i, res.j, res.k);
    }

    // ------------------------------------------------------------
    // 🧭 Set quaternion from Euler angles (yaw, pitch, roll)
    // yaw (Y axis), pitch (X axis), roll (Z axis)
    // ------------------------------------------------------------
    Quaternion& setFromEuler(float yawDeg, float pitchDeg, float rollDeg) {
        
        float yaw = yawDeg * DEG2RAD;
        float pitch = pitchDeg * DEG2RAD;
        float roll = rollDeg * DEG2RAD;

        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);

        w = cr * cp * cy + sr * sp * sy;
        i = sr * cp * cy - cr * sp * sy;
        j = cr * sp * cy + sr * cp * sy;
        k = cr * cp * sy - sr * sp * cy;

        return *this;
    }

    // ------------------------------------------------------------
    // 🧭 Convert quaternion back to Euler angles in DEGREES
    // Returns Vector3(yaw, pitch, roll)
    // ------------------------------------------------------------
    Vector3 toEuler() const {
        

        float sinr_cosp = 2 * (w * i + j * k);
        float cosr_cosp = 1 - 2 * (i * i + j * j);
        float roll = std::atan2(sinr_cosp, cosr_cosp);

        float sinp = 2 * (w * j - k * i);
        float pitch;
        if (std::fabs(sinp) >= 1)
            pitch = std::copysign(M_PI / 2, sinp);
        else
            pitch = std::asin(sinp);

        float siny_cosp = 2 * (w * k + i * j);
        float cosy_cosp = 1 - 2 * (j * j + k * k);
        float yaw = std::atan2(siny_cosp, cosy_cosp);

        // Convert all to degrees
        return Vector3(yaw * RAD2DEG, pitch * RAD2DEG, roll * RAD2DEG);
    }
};


struct Location {
    Vector3		position;
    Quaternion	orientation;
};