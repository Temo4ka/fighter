#include "../include/vec2.hpp"

Vec2::Vec2():
    x (0),
    y (0) {}

Vec2::Vec2(double x_, double y_):
    x (x_),
    y (y_) {}

Vec2 Vec2::operator-() const {
    return Vec2(-x, -y);
}

double Vec2::GetLen() const {
    return std::sqrt(x * x + y * y);
}

void Vec2::Normalize() {
    *this /= GetLen();
}

Vec2 Vec2::operator!() const {
    Vec2 ret = *this;
    ret.Normalize();
    return ret;
}

// Новые методы для улучшенной физики
Vec2 Vec2::normalized() const {
    double len = GetLen();
    if (len < 0.0001) return Vec2(0, 0);
    return Vec2(x / len, y / len);
}

double Vec2::dot(const Vec2& other) const {
    return x * other.x + y * other.y;
}

void Vec2::Rotate (const double angle) {
    double sin_ = std::sin (angle);
    double cos_ = std::cos (angle);

    double new_x = x * cos_ - y * sin_;
    y = x * sin_ + y * cos_;
    x = new_x;
}

double Vec2::GetAngle() {
    Vec2 norm = !(*this);
    if (norm.x == 0) {
        if (norm.y > 0) return M_PI_2;
        else return 3. * M_PI_2;
    }

    if (norm.y >= 0) return std::acos(norm.x);
    else return (M_PI * 2 - std::acos(norm.x));
}

// Операторы сравнения для сортировки
bool Vec2::operator<(const Vec2& other) const {
    if (x != other.x) return x < other.x;
    return y < other.y;
}

bool Vec2::operator<=(const Vec2& other) const {
    return !(other < *this);
}

bool Vec2::operator>(const Vec2& other) const {
    return other < *this;
}

bool Vec2::operator>=(const Vec2& other) const {
    return !(*this < other);
}

Vec2 operator+= (Vec2& vec1, const Vec2& vec2) {
    vec1.x += vec2.x;
    vec1.y += vec2.y;
    return vec1;
}

Vec2 operator-= (Vec2& vec1, const Vec2& vec2) {
    vec1.x -= vec2.x;
    vec1.y -= vec2.y;
    return vec1;
}

Vec2 operator*= (Vec2& vec, const double scalar) {
    vec.x *= scalar;
    vec.y *= scalar;
    return vec;
}

Vec2 operator/= (Vec2& vec, const double scalar) {
    vec.x /= scalar;
    vec.y /= scalar;
    return vec;
}

Vec2 operator+ (Vec2 vec1, const Vec2& vec2) {
    return vec1 += vec2;
}

Vec2 operator- (Vec2 vec1, const Vec2& vec2) {
    return vec1 -= vec2;
}

Vec2 operator* (Vec2 vec, const double scalar) {
    return vec *= scalar;
}

Vec2 operator* (const double scalar, Vec2 vec) {
    return vec *= scalar;
}

Vec2 operator/ (Vec2 vec, const double scalar) {
    return vec /= scalar;
}

double operator, (const Vec2& vec1, const Vec2& vec2) {
    return vec1.x * vec2.x + vec1.y * vec2.y;
}

Vec2 operator^ (const Vec2& vec1, const Vec2& vec2) {
    return Vec2(vec1.x * vec2.x, vec1.y * vec2.y);
}

bool operator== (const Vec2& vec1, const Vec2& vec2) {
    return vec1.x == vec2.x && vec1.y == vec2.y;
}

double GetRandAngle () {
    return std::rand() / (RAND_MAX / (M_PI * 2));
}