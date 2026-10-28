// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/**********************************************************************
cadler fork: unclamped NURBS clamped where they are read (SmClampNurbs.h). Boehm's knot insertion
on the homogeneous control points: the domain's start knot is inserted until it is there degree
times, the control points and knots ahead of it are dropped, and the same at the end.
**********************************************************************/

#include "StdAfx.h"
#include <SmClampNurbs.h>
#include <NL_Geometry.h>

#include <algorithm>
#include <vector>

namespace
{

// control points along one direction: count() rows of `stride` doubles (x, y, z, w of each point
// in the other direction's order)
struct Rows
{
    std::vector<double> values;
    size_t stride;
    size_t count() const { return values.size() / stride; }
};

// an affine combination that keeps NLib's markers (NL_NOZ, NL_NOW): equal values stay as they are
double mix(double first, double second, double alpha)
{
    return first == second ? first : alpha * first + (1.0 - alpha) * second;
}

long multiplicity(const std::vector<double> & knots, double knot)
{
    return (long)std::count(knots.begin(), knots.end(), knot);
}

void insertKnot(long degree, Rows & rows, std::vector<double> & knots, double knot)
{
    const long span = (long)(std::upper_bound(knots.begin(), knots.end(), knot) - knots.begin()) - 1;
    const long times = multiplicity(knots, knot);
    const size_t stride = rows.stride;
    std::vector<double> inserted((rows.count() + 1) * stride);
    for (long i = 0; i <= (long)rows.count(); ++i)
    {
        double * target = &inserted[i * stride];
        if (i <= span - degree)
            std::copy_n(&rows.values[i * stride], stride, target);
        else if (i >= span - times + 1)
            std::copy_n(&rows.values[(i - 1) * stride], stride, target);
        else
        {
            const double alpha = (knot - knots[i]) / (knots[i + degree] - knots[i]);
            for (size_t k = 0; k < stride; ++k)
                target[k] = mix(rows.values[i * stride + k], rows.values[(i - 1) * stride + k], alpha);
        }
    }
    rows.values.swap(inserted);
    knots.insert(knots.begin() + span + 1, knot);
}

void clampStart(long degree, Rows & rows, std::vector<double> & knots)
{
    const double start = knots[degree];
    while (multiplicity(knots, start) < degree)
        insertKnot(degree, rows, knots, start);
    const long run = (long)(std::lower_bound(knots.begin(), knots.end(), start) - knots.begin());
    const long times = multiplicity(knots, start);
    // with degree copies the curve there is the control point ahead of them, and the knot ahead of
    // them no longer counts on the domain: it becomes the start too
    const long first = times >= degree + 1 ? run + times - degree - 1 : run - 1;
    if (first > 0)
    {
        rows.values.erase(rows.values.begin(), rows.values.begin() + first * rows.stride);
        knots.erase(knots.begin(), knots.begin() + first);
    }
    knots[0] = start;
}

void reverse(Rows & rows, std::vector<double> & knots)
{
    const size_t count = rows.count();
    for (size_t i = 0; i < count / 2; ++i)
        std::swap_ranges(&rows.values[i * rows.stride], &rows.values[(i + 1) * rows.stride], &rows.values[(count - 1 - i) * rows.stride]);
    std::reverse(knots.begin(), knots.end());
    for (double & knot : knots)
        knot = -knot;  // exact, unlike a + b - t
}

// Clamps both ends; FALSE when already clamped or when the knots do not describe these points.
bool clampAxis(long degree, Rows & rows, std::vector<double> & knots)
{
    const size_t size = knots.size();
    if (degree < 1 || size != rows.count() + degree + 1 || !std::is_sorted(knots.begin(), knots.end()) || !(knots[degree] < knots[size - degree - 1]))
        return false;
    if (std::count(knots.begin(), knots.begin() + degree + 1, knots[degree]) == degree + 1
        && std::count(knots.end() - degree - 1, knots.end(), knots[size - degree - 1]) == degree + 1)
        return false;
    clampStart(degree, rows, knots);
    reverse(rows, knots);
    clampStart(degree, rows, knots);
    reverse(rows, knots);
    return true;
}

void put(double * values, const NL_CPOINT & point)
{
    values[0] = point.x;
    values[1] = point.y;
    values[2] = point.z;
    values[3] = point.w;
}

void take(NL_CPOINT & point, const double * values)
{
    point.x = values[0];
    point.y = values[1];
    point.z = values[2];
    point.w = values[3];
}

} // namespace

SmBoolean sm_ClampNurbCurve(NL_CURVE * pCurve)
{
    NL_CPOLYGON * pPolygon = pCurve->pol;
    NL_KNOTVECTOR * pKnots = pCurve->knt;
    Rows rows{std::vector<double>((pPolygon->n + 1) * 4), 4};
    for (NL_INDEX i = 0; i <= pPolygon->n; ++i)
        put(&rows.values[i * 4], pPolygon->Pw[i]);
    std::vector<double> knots(pKnots->U, pKnots->U + pKnots->m + 1);
    if (!clampAxis(pCurve->p, rows, knots) || (NL_INDEX)rows.count() > pPolygon->n + 1)
        return FALSE;
    pPolygon->n = (NL_INDEX)rows.count() - 1;
    for (NL_INDEX i = 0; i <= pPolygon->n; ++i)
        take(pPolygon->Pw[i], &rows.values[i * 4]);
    pKnots->m = (NL_INDEX)knots.size() - 1;
    std::copy(knots.begin(), knots.end(), pKnots->U);
    return TRUE;
}

SmBoolean sm_ClampNurbSurface(NL_SURFACE * pSurface)
{
    NL_CNET * pNet = pSurface->net;
    const NL_INDEX uCount = pNet->n + 1, vCount = pNet->m + 1;
    std::vector<double> uKnots(pSurface->knu->U, pSurface->knu->U + pSurface->knu->m + 1);
    std::vector<double> vKnots(pSurface->knv->U, pSurface->knv->U + pSurface->knv->m + 1);

    // along u: one row per u index, holding its v points
    Rows alongU{std::vector<double>(uCount * vCount * 4), (size_t)vCount * 4};
    for (NL_INDEX i = 0; i < uCount; ++i)
        for (NL_INDEX j = 0; j < vCount; ++j)
            put(&alongU.values[(i * vCount + j) * 4], pNet->Pw[i][j]);
    const bool uChanged = clampAxis(pSurface->p, alongU, uKnots);
    const NL_INDEX newUCount = (NL_INDEX)alongU.count();

    // along v: the same points, one row per v index
    Rows alongV{std::vector<double>(newUCount * vCount * 4), (size_t)newUCount * 4};
    for (NL_INDEX i = 0; i < newUCount; ++i)
        for (NL_INDEX j = 0; j < vCount; ++j)
            std::copy_n(&alongU.values[(i * vCount + j) * 4], 4, &alongV.values[(j * newUCount + i) * 4]);
    const bool vChanged = clampAxis(pSurface->q, alongV, vKnots);
    const NL_INDEX newVCount = (NL_INDEX)alongV.count();

    if ((!uChanged && !vChanged) || newUCount > uCount || newVCount > vCount)
        return FALSE;
    pNet->n = newUCount - 1;
    pNet->m = newVCount - 1;
    for (NL_INDEX i = 0; i < newUCount; ++i)
        for (NL_INDEX j = 0; j < newVCount; ++j)
            take(pNet->Pw[i][j], &alongV.values[(j * newUCount + i) * 4]);
    pSurface->knu->m = (NL_INDEX)uKnots.size() - 1;
    std::copy(uKnots.begin(), uKnots.end(), pSurface->knu->U);
    pSurface->knv->m = (NL_INDEX)vKnots.size() - 1;
    std::copy(vKnots.begin(), vKnots.end(), pSurface->knv->U);
    return TRUE;
}
