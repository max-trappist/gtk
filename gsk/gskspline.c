/*
 * Copyright © 2002 University of Southern California
 *             2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 *          Carl D. Worth <cworth@cworth.org>
 */

#include "config.h"

#include "gsksplineprivate.h"

#include <math.h>

#define MIN_PROGRESS (1/1024.f)

typedef struct
{
  graphene_point_t last_point;
  float last_progress;
  GskSplineAddPointFunc func;
  gpointer user_data;
} GskSplineDecompose;

static void
gsk_spline_decompose_add_point (GskSplineDecompose     *decomp,
                                const graphene_point_t *pt,
                                float                   progress)
{
  if (graphene_point_equal (&decomp->last_point, pt))
    return;

  decomp->func (&decomp->last_point, pt, decomp->last_progress, decomp->last_progress + progress, decomp->user_data);
  decomp->last_point = *pt;
  decomp->last_progress += progress;
}

static void
gsk_spline_decompose_finish (GskSplineDecompose     *decomp,
                             const graphene_point_t *end_point)
{
  g_assert (graphene_point_equal (&decomp->last_point, end_point));
  g_assert (decomp->last_progress == 1.0f || decomp->last_progress == 0.0f);
}

typedef struct
{
  GskSplineDecompose decomp;
  float tolerance;
} GskCubicDecomposition;

static void
gsk_spline_cubic_get_coefficients (graphene_point_t       coeffs[4],
                                   const graphene_point_t pts[4])
{
  coeffs[0] = GRAPHENE_POINT_INIT (pts[3].x - 3.0f * pts[2].x + 3.0f * pts[1].x - pts[0].x,
                                   pts[3].y - 3.0f * pts[2].y + 3.0f * pts[1].y - pts[0].y);
  coeffs[1] = GRAPHENE_POINT_INIT (3.0f * pts[2].x - 6.0f * pts[1].x + 3.0f * pts[0].x,
                                   3.0f * pts[2].y - 6.0f * pts[1].y + 3.0f * pts[0].y);
  coeffs[2] = GRAPHENE_POINT_INIT (3.0f * pts[1].x - 3.0f * pts[0].x,
                                   3.0f * pts[1].y - 3.0f * pts[0].y);
  coeffs[3] = pts[0];
}

void
gsk_spline_get_point_cubic (const graphene_point_t  pts[4],
                            float                   progress,
                            graphene_point_t       *pos,
                            graphene_vec2_t        *tangent)
{
  graphene_point_t c[4];

  gsk_spline_cubic_get_coefficients (c, pts);

  if (pos)
    *pos = GRAPHENE_POINT_INIT (((c[0].x * progress + c[1].x) * progress +c[2].x) * progress + c[3].x,
                                ((c[0].y * progress + c[1].y) * progress +c[2].y) * progress + c[3].y);
  if (tangent)
    {
      graphene_vec2_init (tangent,
                          (3.0f * c[0].x * progress + 2.0f * c[1].x) * progress + c[2].x,
                          (3.0f * c[0].y * progress + 2.0f * c[1].y) * progress + c[2].y);
      graphene_vec2_normalize (tangent, tangent);
    }
}

void
gsk_spline_split_cubic (const graphene_point_t pts[4],
                        graphene_point_t       result1[4],
                        graphene_point_t       result2[4],
                        float                  progress)
{
    graphene_point_t ab, bc, cd;
    graphene_point_t abbc, bccd;
    graphene_point_t final;

    graphene_point_interpolate (&pts[0], &pts[1], progress, &ab);
    graphene_point_interpolate (&pts[1], &pts[2], progress, &bc);
    graphene_point_interpolate (&pts[2], &pts[3], progress, &cd);
    graphene_point_interpolate (&ab, &bc, progress, &abbc);
    graphene_point_interpolate (&bc, &cd, progress, &bccd);
    graphene_point_interpolate (&abbc, &bccd, progress, &final);

    memcpy (result1, (graphene_point_t[4]) { pts[0], ab, abbc, final }, sizeof (graphene_point_t[4]));
    memcpy (result2, (graphene_point_t[4]) { final, bccd, cd, pts[3] }, sizeof (graphene_point_t[4]));
}

#if 0
/* Return an upper bound on the error (squared) that could result from
 * approximating a spline as a line segment connecting the two endpoints. */
static float
gsk_spline_error_squared (const graphene_point_t pts[4])
{
  float bdx, bdy, berr;
  float cdx, cdy, cerr;

  /* We are going to compute the distance (squared) between each of the the b
   * and c control points and the segment a-b. The maximum of these two
   * distances will be our approximation error. */

  bdx = pts[1].x - pts[0].x;
  bdy = pts[1].y - pts[0].y;

  cdx = pts[2].x - pts[0].x;
  cdy = pts[2].y - pts[0].y;

  if (!graphene_point_equal (&pts[0], &pts[3]))
    {
      float dx, dy, u, v;

      /* Intersection point (px):
       *     px = p1 + u(p2 - p1)
       *     (p - px) ∙ (p2 - p1) = 0
       * Thus:
       *     u = ((p - p1) ∙ (p2 - p1)) / ∥p2 - p1∥²;
       */

      dx = pts[3].x - pts[0].x;
      dy = pts[3].y - pts[0].y;
      v = dx * dx + dy * dy;

      u = bdx * dx + bdy * dy;
      if (u <= 0)
        {
          /* bdx -= 0;
           * bdy -= 0;
           */
        }
      else if (u >= v)
        {
          bdx -= dx;
          bdy -= dy;
        }
      else
        {
          bdx -= u/v * dx;
          bdy -= u/v * dy;
        }

      u = cdx * dx + cdy * dy;
      if (u <= 0)
        {
          /* cdx -= 0;
           * cdy -= 0;
           */
        }
      else if (u >= v)
        {
          cdx -= dx;
          cdy -= dy;
        }
      else
        {
          cdx -= u/v * dx;
          cdy -= u/v * dy;
        }
    }

    berr = bdx * bdx + bdy * bdy;
    cerr = cdx * cdx + cdy * cdy;
    if (berr > cerr)
      return berr;
    else
      return cerr;
}
#endif

/* taken from Skia, including the very descriptive name */
static gboolean
gsk_spline_cubic_too_curvy (const graphene_point_t pts[4],
                            float                  tolerance)
{
  graphene_point_t p;

  graphene_point_interpolate (&pts[0], &pts[3], 1.0f / 3, &p);
  if (ABS (p.x - pts[1].x) + ABS (p.y - pts[1].y)  > tolerance)
    return TRUE;

  graphene_point_interpolate (&pts[0], &pts[3], 2.0f / 3, &p);
  if (ABS (p.x - pts[2].x) + ABS (p.y - pts[2].y)  > tolerance)
    return TRUE;

  return FALSE;
}

static void
gsk_spline_cubic_decompose (GskCubicDecomposition  *d,
                            const graphene_point_t  pts[4],
                            float                   progress)
{
  graphene_point_t left[4], right[4];

  if (!gsk_spline_cubic_too_curvy (pts, d->tolerance) || progress < MIN_PROGRESS)
    {
      gsk_spline_decompose_add_point (&d->decomp, &pts[3], progress);
      return;
    }

  gsk_spline_split_cubic (pts, left, right, 0.5);

  gsk_spline_cubic_decompose (d, left, progress / 2);
  gsk_spline_cubic_decompose (d, right, progress / 2);
}

void 
gsk_spline_decompose_cubic (const graphene_point_t pts[4],
                            float                  tolerance,
                            GskSplineAddPointFunc  add_point_func,
                            gpointer               user_data)
{
  GskCubicDecomposition decomp = { { pts[0], 0.0f, add_point_func, user_data }, tolerance };

  gsk_spline_cubic_decompose (&decomp, pts, 1.0f);

  gsk_spline_decompose_finish (&decomp.decomp, &pts[3]);
}

/* CONIC */

typedef struct {
  graphene_point_t num[3];
  graphene_point_t denom[3];
} ConicCoefficients;

typedef struct
{
  GskSplineDecompose decomp;
  float tolerance;
  ConicCoefficients c;
} GskConicDecomposition;


static void
gsk_spline_conic_get_coefficents (ConicCoefficients      *c,
                                  const graphene_point_t  pts[4])
{
  float w = pts[2].x;
  graphene_point_t pw = GRAPHENE_POINT_INIT (w * pts[1].x, w * pts[1].y);

  c->num[2] = pts[0];
  c->num[1] = GRAPHENE_POINT_INIT (2 * (pw.x - pts[0].x),
                                   2 * (pw.y - pts[0].y));
  c->num[0] = GRAPHENE_POINT_INIT (pts[3].x - 2 * pw.x + pts[0].x,
                                   pts[3].y - 2 * pw.y + pts[0].y);

  c->denom[2] = GRAPHENE_POINT_INIT (1, 1);
  c->denom[1] = GRAPHENE_POINT_INIT (2 * (w - 1), 2 * (w - 1));
  c->denom[0] = GRAPHENE_POINT_INIT (-c->denom[1].x, -c->denom[1].y);
}

static inline void
gsk_spline_eval_quad (const graphene_point_t quad[3],
                      float                  progress,
                      graphene_point_t      *result)
{
  *result = GRAPHENE_POINT_INIT ((quad[0].x * progress + quad[1].x) * progress + quad[2].x,
                                 (quad[0].y * progress + quad[1].y) * progress + quad[2].y);
}

static inline void
gsk_spline_eval_conic (const ConicCoefficients *c,
                       float                    progress,
                       graphene_point_t        *result)
{
  graphene_point_t num, denom;

  gsk_spline_eval_quad (c->num, progress, &num);
  gsk_spline_eval_quad (c->denom, progress, &denom);
  *result = GRAPHENE_POINT_INIT (num.x / denom.x, num.y / denom.y);
}
                       
void
gsk_spline_get_point_conic (const graphene_point_t  pts[4],
                            float                   progress,
                            graphene_point_t       *pos,
                            graphene_vec2_t        *tangent)
{
  ConicCoefficients c;

  gsk_spline_conic_get_coefficents (&c, pts);

  if (pos)
    gsk_spline_eval_conic (&c, progress, pos);

  if (tangent)
    {
      graphene_point_t tmp;
      float w = pts[2].x;

      /* The tangent will be 0 in these corner cases, just
       * treat it like a line here. */
      if ((progress <= 0.f && graphene_point_equal (&pts[0], &pts[1])) ||
          (progress >= 1.f && graphene_point_equal (&pts[1], &pts[3])))
        {
          graphene_vec2_init (tangent, pts[3].x - pts[0].x, pts[3].y - pts[0].y);
          return;
        }

      gsk_spline_eval_quad ((graphene_point_t[3]) {
                              GRAPHENE_POINT_INIT ((w - 1) * (pts[3].x - pts[0].x),
                                                   (w - 1) * (pts[3].y - pts[0].y)),
                              GRAPHENE_POINT_INIT (pts[3].x - pts[0].x - 2 * w * (pts[1].x - pts[0].x),
                                                   pts[3].y - pts[0].y - 2 * w * (pts[1].y - pts[0].y)),
                              GRAPHENE_POINT_INIT (w * (pts[1].x - pts[0].x),
                                                   w * (pts[1].y - pts[0].y))
                            },
                            progress,
                            &tmp);
      graphene_vec2_init (tangent, tmp.x, tmp.y);
      graphene_vec2_normalize (tangent, tangent);
    }
}

void
gsk_spline_split_conic (const graphene_point_t pts[4],
                        graphene_point_t       result1[4],
                        graphene_point_t       result2[4],
                        float                  progress)
{
  g_warning ("FIXME: Stop treating conics as lines");
}

/* taken from Skia, including the very descriptive name */
static gboolean
gsk_spline_conic_too_curvy (const graphene_point_t *start,
                            const graphene_point_t *mid,
                            const graphene_point_t *end,
                            float                  tolerance)
{
  return fabs ((start->x + end->x) * 0.5 - mid->x) > tolerance
      || fabs ((start->y + end->y) * 0.5 - mid->y) > tolerance;
}

static void
gsk_spline_decompose_conic_subdivide (GskConicDecomposition  *d,
                                      const graphene_point_t *start,
                                      float                   start_progress,
                                      const graphene_point_t *end,
                                      float                   end_progress)
{
  graphene_point_t mid;
  float mid_progress;

  mid_progress = (start_progress + end_progress) / 2;
  gsk_spline_eval_conic (&d->c, mid_progress, &mid);

  if (end_progress - start_progress < MIN_PROGRESS ||
      !gsk_spline_conic_too_curvy (start, &mid, end, d->tolerance))
    {
      gsk_spline_decompose_add_point (&d->decomp, end, end_progress - start_progress);
      return;
    }

  gsk_spline_decompose_conic_subdivide (d, start, start_progress, &mid, mid_progress);
  gsk_spline_decompose_conic_subdivide (d, &mid, mid_progress, end, end_progress);
}

void
gsk_spline_decompose_conic (const graphene_point_t pts[4],
                            float                  tolerance,
                            GskSplineAddPointFunc  add_point_func,
                            gpointer               user_data)
{
  GskConicDecomposition d = { { pts[0], 0.0f, add_point_func, user_data }, tolerance, };

  gsk_spline_conic_get_coefficents (&d.c, pts);

  gsk_spline_decompose_conic_subdivide (&d, &pts[0], 0.0f, &pts[3], 1.0f);

  gsk_spline_decompose_finish (&d.decomp, &pts[3]);
}

/* Spline deviation from the circle in radius would be given by:

        error = sqrt (x**2 + y**2) - 1

   A simpler error function to work with is:

        e = x**2 + y**2 - 1

   From "Good approximation of circles by curvature-continuous Bezier
   curves", Tor Dokken and Morten Daehlen, Computer Aided Geometric
   Design 8 (1990) 22-41, we learn:

        abs (max(e)) = 4/27 * sin**6(angle/4) / cos**2(angle/4)

   and
        abs (error) =~ 1/2 * e

   Of course, this error value applies only for the particular spline
   approximation that is used in _cairo_gstate_arc_segment.
*/
static float
arc_error_normalized (float angle)
{
  return 2.0/27.0 * pow (sin (angle / 4), 6) / pow (cos (angle / 4), 2);
}

static float
arc_max_angle_for_tolerance_normalized (float tolerance)
{
  float angle, error;
  guint i;

  /* Use table lookup to reduce search time in most cases. */
  struct {
    float angle;
    float error;
  } table[] = {
    { G_PI / 1.0,   0.0185185185185185036127 },
    { G_PI / 2.0,   0.000272567143730179811158 },
    { G_PI / 3.0,   2.38647043651461047433e-05 },
    { G_PI / 4.0,   4.2455377443222443279e-06 },
    { G_PI / 5.0,   1.11281001494389081528e-06 },
    { G_PI / 6.0,   3.72662000942734705475e-07 },
    { G_PI / 7.0,   1.47783685574284411325e-07 },
    { G_PI / 8.0,   6.63240432022601149057e-08 },
    { G_PI / 9.0,   3.2715520137536980553e-08 },
    { G_PI / 10.0,  1.73863223499021216974e-08 },
    { G_PI / 11.0,  9.81410988043554039085e-09 },
  };

  for (i = 0; i < G_N_ELEMENTS (table); i++)
    {
      if (table[i].error < tolerance)
        return table[i].angle;
    }

  i++;
  do {
    angle = G_PI / i++;
    error = arc_error_normalized (angle);
  } while (error > tolerance);

  return angle;
}

static guint
arc_segments_needed (float angle,
                     float radius,
                     float tolerance)
{
  float max_angle;

  /* the error is amplified by at most the length of the
   * major axis of the circle; see cairo-pen.c for a more detailed analysis
   * of this. */
  max_angle = arc_max_angle_for_tolerance_normalized (tolerance / radius);

  return ceil (fabs (angle) / max_angle);
}

/* We want to draw a single spline approximating a circular arc radius
   R from angle A to angle B. Since we want a symmetric spline that
   matches the endpoints of the arc in position and slope, we know
   that the spline control points must be:

        (R * cos(A), R * sin(A))
        (R * cos(A) - h * sin(A), R * sin(A) + h * cos (A))
        (R * cos(B) + h * sin(B), R * sin(B) - h * cos (B))
        (R * cos(B), R * sin(B))

   for some value of h.

   "Approximation of circular arcs by cubic polynomials", Michael
   Goldapp, Computer Aided Geometric Design 8 (1991) 227-238, provides
   various values of h along with error analysis for each.

   From that paper, a very practical value of h is:

        h = 4/3 * R * tan(angle/4)

   This value does not give the spline with minimal error, but it does
   provide a very good approximation, (6th-order convergence), and the
   error expression is quite simple, (see the comment for
   _arc_error_normalized).
*/
static gboolean
gsk_spline_decompose_arc_segment (const graphene_point_t *center,
                                  float                   radius,
                                  float                   angle_A,
                                  float                   angle_B,
                                  GskSplineAddCurveFunc   curve_func,
                                  gpointer                user_data)
{
  float r_sin_A, r_cos_A;
  float r_sin_B, r_cos_B;
  float h;

  r_sin_A = radius * sin (angle_A);
  r_cos_A = radius * cos (angle_A);
  r_sin_B = radius * sin (angle_B);
  r_cos_B = radius * cos (angle_B);

  h = 4.0/3.0 * tan ((angle_B - angle_A) / 4.0);

  return curve_func ((graphene_point_t[4]) {
                       GRAPHENE_POINT_INIT (
                         center->x + r_cos_A,
                         center->y + r_sin_A
                       ),
                       GRAPHENE_POINT_INIT (
                         center->x + r_cos_A - h * r_sin_A,
                         center->y + r_sin_A + h * r_cos_A
                       ),
                       GRAPHENE_POINT_INIT (
                         center->x + r_cos_B + h * r_sin_B,
                         center->y + r_sin_B - h * r_cos_B
                       ),
                       GRAPHENE_POINT_INIT (
                         center->x + r_cos_B,
                         center->y + r_sin_B
                       )
                     },
                     user_data);
}

gboolean
gsk_spline_decompose_arc (const graphene_point_t *center,
                          float                   radius,
                          float                   tolerance,
                          float                   start_angle,
                          float                   end_angle,
                          GskSplineAddCurveFunc   curve_func,
                          gpointer                user_data)
{
  float step = start_angle - end_angle;
  guint i, n_segments;

  /* Recurse if drawing arc larger than pi */
  if (ABS (step) > G_PI)
    {
      float mid_angle = (start_angle + end_angle) / 2.0;

      return gsk_spline_decompose_arc (center, radius, tolerance, start_angle, mid_angle, curve_func, user_data)
          && gsk_spline_decompose_arc (center, radius, tolerance, mid_angle, end_angle, curve_func, user_data);
    }
  else if (ABS (step) < tolerance)
    {
      return TRUE;
    }

  n_segments = arc_segments_needed (ABS (step), radius, tolerance);
  step = (end_angle - start_angle) / n_segments;

  for (i = 0; i < n_segments - 1; i++, start_angle += step)
    {
      if (!gsk_spline_decompose_arc_segment (center, radius, start_angle, start_angle + step, curve_func, user_data))
        return FALSE;
    }
  return gsk_spline_decompose_arc_segment (center, radius, start_angle, end_angle, curve_func, user_data);
}
