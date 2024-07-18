//
// Based on balls.c from SPD
//
#include "config.h"

#include <iomanip>

#ifndef EPSILON
#define EPSILON 1.0e-8
#endif

#ifndef PI
#define PI 3.141592653589793
#endif

typedef double MATRIX[4][4]; /* row major form */

typedef double COORD3[3];
typedef double COORD4[4];

/* COORD3/COORD4 indices */
#define X 0
#define Y 1
#define Z 2
#define W 3

#define SQR(A) ((A) * (A))

#define COPY_COORD4(r, a) \
    {                     \
        (r)[X] = (a)[X];  \
        (r)[Y] = (a)[Y];  \
        (r)[Z] = (a)[Z];  \
        (r)[W] = (a)[W];  \
    }

#define CROSS(r, a, b)                              \
    {                                               \
        (r)[X] = (a)[Y] * (b)[Z] - (a)[Z] * (b)[Y]; \
        (r)[Y] = (a)[Z] * (b)[X] - (a)[X] * (b)[Z]; \
        (r)[Z] = (a)[X] * (b)[Y] - (a)[Y] * (b)[X]; \
    }
#define DOT_PRODUCT(a, b) ((a)[X] * (b)[X] + (a)[Y] * (b)[Y] + (a)[Z] * (b)[Z])

#define SET_COORD3(r, A, B, C) \
    {                          \
        (r)[X] = (A);          \
        (r)[Y] = (B);          \
        (r)[Z] = (C);          \
    }
#define SET_COORD4(r, A, B, C, D) \
    {                             \
        (r)[X] = (A);             \
        (r)[Y] = (B);             \
        (r)[Z] = (C);             \
        (r)[W] = (D);             \
    }

#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

/*
 * Normalize the vector (X,Y,Z) so that X*X + Y*Y + Z*Z = 1.
 *
 * The normalization divisor is returned.  If the divisor is zero, no
 * normalization occurs.
 *
 */
double lib_normalize_vector(COORD3 cvec)
{
    double divisor;

    divisor = sqrt((double)DOT_PRODUCT(cvec, cvec));
    if (divisor > 0.0)
    {
        cvec[X] /= divisor;
        cvec[Y] /= divisor;
        cvec[Z] /= divisor;
    }
    return divisor;
}

/* Find two vectors, basis1 and basis2, that form an orthogonal basis with
   the vector axis.  It is assumed that axis is non-zero.  */
void lib_create_orthogonal_vectors(COORD3 axis, COORD3 basis1, COORD3 basis2)
{
    if (fabs(axis[Z]) < EPSILON)
    {
        SET_COORD3(basis1, 0.0, 0.0, 1.0);
    }
    else if (fabs(axis[Y]) < EPSILON)
    {
        SET_COORD3(basis1, 0.0, 1.0, 0.0);
    }
    else
    {
        SET_COORD3(basis1, 1.0, 0.0, 0.0);
    }
    CROSS(basis2, axis, basis1);
    (void)lib_normalize_vector(basis2);
    CROSS(basis1, basis2, axis);
    (void)lib_normalize_vector(basis1);
}

/*
 * Create a rotation matrix along the given axis by the given angle in radians.
 * The axis is a set of direction cosines.
 */
void lib_create_axis_rotate_matrix(MATRIX mx, COORD3 axis, double angle)
{
    double cosine, one_minus_cosine, sine;

    cosine           = cos((double)angle);
    sine             = sin((double)angle);
    one_minus_cosine = 1.0 - cosine;

    mx[0][0] = SQR(axis[X]) + (1.0 - SQR(axis[X])) * cosine;
    mx[0][1] = axis[X] * axis[Y] * one_minus_cosine + axis[Z] * sine;
    mx[0][2] = axis[X] * axis[Z] * one_minus_cosine - axis[Y] * sine;
    mx[0][3] = 0.0;

    mx[1][0] = axis[X] * axis[Y] * one_minus_cosine - axis[Z] * sine;
    mx[1][1] = SQR(axis[Y]) + (1.0 - SQR(axis[Y])) * cosine;
    mx[1][2] = axis[Y] * axis[Z] * one_minus_cosine + axis[X] * sine;
    mx[1][3] = 0.0;

    mx[2][0] = axis[X] * axis[Z] * one_minus_cosine + axis[Y] * sine;
    mx[2][1] = axis[Y] * axis[Z] * one_minus_cosine - axis[X] * sine;
    mx[2][2] = SQR(axis[Z]) + (1.0 - SQR(axis[Z])) * cosine;
    mx[2][3] = 0.0;

    mx[3][0] = 0.0;
    mx[3][1] = 0.0;
    mx[3][2] = 0.0;
    mx[3][3] = 1.0;
}

/*
 * Multiply a 4 element vector by a matrix.  Typically used for
 * homogenous transformation from world space to screen space.
 */
void lib_transform_coord(COORD4 vres, COORD4 vec, MATRIX mx)
{
    COORD4 vtemp;
    vtemp[X] = vec[X] * mx[0][0] + vec[Y] * mx[1][0] + vec[Z] * mx[2][0] + vec[W] * mx[3][0];
    vtemp[Y] = vec[X] * mx[0][1] + vec[Y] * mx[1][1] + vec[Z] * mx[2][1] + vec[W] * mx[3][1];
    vtemp[Z] = vec[X] * mx[0][2] + vec[Y] * mx[1][2] + vec[Z] * mx[2][2] + vec[W] * mx[3][2];
    vtemp[W] = vec[X] * mx[0][3] + vec[Y] * mx[1][3] + vec[Z] * mx[2][3] + vec[W] * mx[3][3];
    COPY_COORD4(vres, vtemp);
}

/*
 * Set all matrix elements to zero.
 */
void lib_zero_matrix(MATRIX mx)
{
    int i, j;

    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            mx[i][j] = 0.0;
}

/*
 * Create a rotation matrix along the given axis by the given angle in radians.
 */
void lib_create_rotate_matrix(MATRIX mx, int axis, double angle)
{
    double cosine, sine;

    lib_zero_matrix(mx);
    cosine = cos((double)angle);
    sine   = sin((double)angle);
    switch (axis)
    {
        case X_AXIS:
            mx[0][0] = 1.0;
            mx[1][1] = cosine;
            mx[2][2] = cosine;
            mx[1][2] = sine;
            mx[2][1] = -sine;
            break;
        case Y_AXIS:
            mx[1][1] = 1.0;
            mx[0][0] = cosine;
            mx[2][2] = cosine;
            mx[2][0] = sine;
            mx[0][2] = -sine;
            break;
        case Z_AXIS:
            mx[2][2] = 1.0;
            mx[0][0] = cosine;
            mx[1][1] = cosine;
            mx[0][1] = sine;
            mx[1][0] = -sine;
            break;
        default:
            fprintf(stderr, "Internal Error: bad call to lib_create_rotate_matrix\n");
            exit(1);
            break;
    }
    mx[3][3] = 1.0;
}

static COORD4 objset[9];

/* Create the set of 9 vectors needed to generate the sphere set. */
/* Uses global 'objset' */
static void
create_objset()
{
    COORD4 axis, temp_pt, trio_dir[3];
    double dist;
    MATRIX mx;
    long   num_set, num_vert;

    dist = 1.0 / sqrt((double)2.0);

    SET_COORD4(trio_dir[0], dist, dist, 0.0, 0.0);
    SET_COORD4(trio_dir[1], dist, 0.0, -dist, 0.0);
    SET_COORD4(trio_dir[2], 0.0, dist, -dist, 0.0);

    SET_COORD3(axis, 1.0, -1.0, 0.0);
    lib_normalize_vector(axis);
    lib_create_axis_rotate_matrix(mx, axis, asin((double)(2.0 / sqrt((double)6.0))));

    for (num_vert = 0; num_vert < 3; ++num_vert)
    {
        lib_transform_coord(temp_pt, trio_dir[num_vert], mx);
        COPY_COORD4(trio_dir[num_vert], temp_pt);
    }

    for (num_set = 0; num_set < 3; ++num_set)
    {
        lib_create_rotate_matrix(mx, Z_AXIS, num_set * 2.0 * PI / 3.0);
        for (num_vert = 0; num_vert < 3; ++num_vert)
        {
            lib_transform_coord(objset[num_set * 3 + num_vert], trio_dir[num_vert], mx);
        }
    }
}

int main(int argc, char** argv)
{
    create_objset();

    std::stringstream ss;
    ss << std::fixed << std::setprecision(9);
    for (int i = 0; i < 9; ++i)
    {
        lib_normalize_vector(objset[i]);
        ss << "[" << i << "]"
           << " = {" << objset[i][X] << ", " << objset[i][Y] << ", " << objset[i][Z] << "}\n";
    }
    Print(ss.str().c_str());

    return EXIT_SUCCESS;
}