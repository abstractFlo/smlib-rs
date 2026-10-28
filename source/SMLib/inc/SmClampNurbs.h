// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/**********************************************************************
cadler fork: NURBS read with an unclamped knot vector (STEP allows them; OpenCASCADE, and with it
FreeCAD, writes closed periodic B-splines so) are clamped where they are read: the end knots of the
domain repeated degree + 1 times, the same curve or surface on the domain. SMLib reads unclamped
ones, then overruns its arrays splitting them into Beziers (sm_CreateBezSegments, sm_DecomposeSrf)
and cannot build the NURB of an extrusion made from one.
**********************************************************************/

#ifndef __SmClampNurbs_H__
#define __SmClampNurbs_H__

#include <NL_Nurbsdef.h>

// Clamp in place; the result never needs more control points or knots than the NURB holds.
// Returns TRUE when something changed. A knot vector that is not non-decreasing, or does not
// match the control points, is left as it is.
SM_EXPORT SmBoolean sm_ClampNurbCurve(NL_CURVE * pCurve);
SM_EXPORT SmBoolean sm_ClampNurbSurface(NL_SURFACE * pSurface);

#endif
