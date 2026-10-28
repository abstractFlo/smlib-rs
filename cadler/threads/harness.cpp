// SM_API in several threads of one process: every operation cadler uses runs in 1 to N threads at
// once, each thread building its own objects, and each result is compared bit for bit with the
// same sequence run alone in a fresh thread (volume, area, topology, tessellation). Then bodies
// crossing threads: one handed on once its builder rests, one taken as a boolean's tool, and
// copies taken under a lock while the builder keeps working. Built twice by run.sh: against the
// release libraries (results and speedup) and from source under ThreadSanitizer (data races).
//
//   harness [--threads 1,2,4,8] [--passes 3]
//
// Exit code 0 when every result matched in every thread, 1 otherwise.

#include "StdAfx.h"

#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiFillets.h>
#include <SmApiGeneral.h>
#include <SmApiHeal.h>
#include <SmApiImportExport.h>
#include <SmApiIntersectors.h>
#include <SmApiPolygons.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmBSplineCurve.h>
#include <SmAxis2Placement.h>
#include <SmEdge.h>
#include <SmEllipse.h>
#include <SmFace.h>
#include <SmLine.h>
#include <SmPoly.h>
#include <SmSurface.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct Fingerprint
{
    int status = 0;
    unsigned long faces = 0;
    unsigned long edges = 0;
    double volume = 0.0;
    double area = 0.0;
    unsigned long meshVertices = 0;
    std::uint64_t meshHash = 0;

    bool operator==(const Fingerprint& other) const
    {
        // bit for bit: the doubles are compared as stored
        return status == other.status && faces == other.faces && edges == other.edges
            && std::memcmp(&volume, &other.volume, sizeof volume) == 0
            && std::memcmp(&area, &other.area, sizeof area) == 0
            && meshVertices == other.meshVertices && meshHash == other.meshHash;
    }
};

std::uint64_t hashBytes(std::uint64_t hash, const void* data, std::size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    return hash;
}

const std::uint64_t emptyHash = 1469598103934665603ull;

std::uint64_t hashPoint(std::uint64_t hash, const SmPoint3d& point)
{
    hash = hashBytes(hash, &point.x, sizeof point.x);
    hash = hashBytes(hash, &point.y, sizeof point.y);
    return hashBytes(hash, &point.z, sizeof point.z);
}

// The tessellation of a body into `result` (vertex count and a hash of their points).
void tessellateInto(Fingerprint& result, SmBrep* body, const SmTessellationParams& params = SmTessellationParams())
{
    SmPolyBrep* mesh = NULL;
    if (SmApiTessellate(body, mesh, params) == SM_SUCCESS && mesh != NULL)
    {
        SmTArray<SmPolyVertex*> vertices;
        mesh->GetPolyVertices(vertices);
        result.meshVertices = vertices.GetSize();
        std::uint64_t hash = emptyHash;
        for (ULONG index = 0; index < vertices.GetSize(); ++index)
            hash = hashPoint(hash, vertices[index]->GetPoint());
        result.meshHash = hash;
        delete mesh;
    }
}

// Volume, area, topology and the tessellation of a body; the body is deleted.
Fingerprint fingerprintOf(SmApiStatus status, SmBrep* body, const SmTessellationParams& params = SmTessellationParams())
{
    Fingerprint result;
    result.status = static_cast<int>(status);
    if (status != SM_SUCCESS || body == NULL)
    {
        delete body;
        return result;
    }
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    body->GetFaces(faces);
    body->GetEdges(edges);
    result.faces = faces.GetSize();
    result.edges = edges.GetSize();
    SmApiBrepComputeVolume(body, 1.0e-6, result.volume);
    SmApiBrepComputeArea(body, 1.0e-6, result.area);
    tessellateInto(result, body, params);
    delete body;
    return result;
}

// Points an operation found: how many and a hash of them.
Fingerprint pointsPrint(SmApiStatus status, const SmTArray<SmPoint3d>& points)
{
    Fingerprint result;
    result.status = static_cast<int>(status);
    result.meshVertices = points.GetSize();
    result.meshHash = emptyHash;
    for (ULONG index = 0; index < points.GetSize(); ++index)
        result.meshHash = hashPoint(result.meshHash, points[index]);
    return result;
}

// Curves an operation made: how many and a hash of their ends; the curves are deleted.
Fingerprint curvesPrint(SmApiStatus status, SmTArray<SmCurve*>& curves)
{
    Fingerprint result;
    result.status = static_cast<int>(status);
    result.edges = curves.GetSize();
    result.meshHash = emptyHash;
    for (ULONG index = 0; index < curves.GetSize(); ++index)
    {
        SmPoint3d start, end;
        curves[index]->GetEnds(start, end);
        result.meshHash = hashPoint(hashPoint(result.meshHash, start), end);
        delete curves[index];
    }
    return result;
}

SmBrep* box(double x, double y, double z, double length, double width, double height)
{
    SmBrep* body = NULL;
    SmApiCreateBox(SmVector3d(x, y, z), length, width, height, body);
    return body;
}

SmBrep* cylinder(double x, double y, double z, double radius, double height)
{
    SmBrep* body = NULL;
    SmApiCreateCylinder(SmVector3d(x, y, z), radius, height, body);
    return body;
}

// A boolean: on success the result is `first`, changed in place, and `second` is deleted by
// SM_API; on failure both are deleted here.
SmApiStatus combine(SmApiStatus (*operation)(SmBrep*, SmBrep*, SmBrep*&), SmBrep* first, SmBrep* second, SmBrep*& result)
{
    result = NULL;
    const SmApiStatus status = operation(first, second, result);
    if (status != SM_SUCCESS)
    {
        delete first;
        delete second;
        result = NULL;
    }
    return status;
}

SmTArray<SmCurve*> rectangle(double x, double y, double length, double width, double z = 0.0)
{
    SmTArray<SmBSplineCurve*> sides;
    SmApiCreateRectangle(SmPoint3d(x, y, z), length, width, sides);
    SmTArray<SmCurve*> curves;
    for (ULONG index = 0; index < sides.GetSize(); ++index)
        curves.Add(sides[index]);
    return curves;
}

void deleteCurves(SmTArray<SmCurve*>& curves)
{
    for (ULONG index = 0; index < curves.GetSize(); ++index)
        delete curves[index];
}

SmApiStatus filletAll(SmBrep* body, double radius)
{
    SmTArray<SmEdge*> edges;
    body->GetEdges(edges);
    return SmApiFilletEdges(body, edges, radius, 1, 1, 1.0);
}

// A rounded box with a hole through it: what most of cadler's models are made of.
SmBrep* roundedBoxWithHole()
{
    SmBrep* body = box(0, 0, 0, 40, 30, 20);
    filletAll(body, 3.0);
    SmBrep* result = NULL;
    combine(SmApiBooleanDifference, body, cylinder(20, 15, -10, 8, 40), result);
    return result;
}

// A native SMLib file of a rounded box with a hole, as cadler's STEP import writes one to read it
// back (`Brep.read_from_file`); deleted at the end.
std::string writeImportFile()
{
    const std::string path = (std::filesystem::temp_directory_path() / ("sm-threads-" + std::to_string(getpid()) + ".txt")).string();
    std::thread([&] {
        SmBrep* body = roundedBoxWithHole();
        SmApiWriteBrepToFile(body, path.c_str(), TRUE);
        delete body;
        SmApiDeleteContext();
    }).join();
    return path;
}

std::string importFile;

struct Operation
{
    const char* name;
    std::function<Fingerprint()> run;
};

// The operations cadler's kernel uses, each building what it needs in the calling thread.
std::vector<Operation> operations()
{
    return {
        {"fillet all edges", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             return fingerprintOf(filletAll(body, 3.0), body);
         }},
        {"fillet per edge, variable", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             SmTArray<SmEdge*> edges, one;
             body->GetEdges(edges);
             one.Add(edges[0]);
             return fingerprintOf(SmApiVariableRadiusFillet(body, one, 1.0, 3.0, 1, 1), body);
         }},
        {"chamfer", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             return fingerprintOf(SmApiChamferFillet(body, 2.0), body);
         }},
        {"boolean difference", [] {
             SmBrep* result = NULL;
             const SmApiStatus status = combine(SmApiBooleanDifference, box(0, 0, 0, 40, 30, 20), cylinder(20, 15, -10, 8, 40), result);
             return fingerprintOf(status, result);
         }},
        {"boolean union with sphere", [] {
             SmBrep* sphere = NULL;
             SmApiCreateSphere(SmVector3d(40, 30, 20), 12.0, sphere);
             SmBrep* result = NULL;
             const SmApiStatus status = combine(SmApiBooleanUnion, box(0, 0, 0, 40, 30, 20), sphere, result);
             return fingerprintOf(status, result);
         }},
        {"boolean intersection with torus", [] {
             SmBrep* torus = NULL;
             SmApiCreateTorus(SmVector3d(20, 15, 10), 15.0, 5.0, torus);
             SmBrep* result = NULL;
             const SmApiStatus status = combine(SmApiBooleanIntersection, box(0, 0, 0, 40, 30, 20), torus, result);
             return fingerprintOf(status, result);
         }},
        {"many unions (lettering)", [] {
             SmBrep* plate = box(0, 0, 0, 60, 20, 2);
             SmApiStatus status = SM_SUCCESS;
             for (int index = 0; index < 10 && status == SM_SUCCESS; ++index)
             {
                 SmBrep* result = NULL;
                 status = combine(SmApiBooleanUnion, plate, cylinder(4.0 + 5.5 * index, 10.0 + (index % 3) - 1.0, 1.0, 2.2, 3.0), result);
                 plate = result;
             }
             return fingerprintOf(status, plate);
         }},
        {"shell", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             SmTArray<SmFace*> faces;
             body->GetFaces(faces);
             SmTArray<const SmFace*> open;
             open.Add(faces[0]);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiShellBrepFull(body, -2.0, TRUE, TRUE, TRUE, open, result);
             if (result != body)
                 delete body;
             return fingerprintOf(status, result);
         }},
        {"offset", [] {
             SmBrep* sphere = NULL;
             SmApiCreateSphere(SmVector3d(0, 0, 0), 10.0, sphere);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiOffsetBrepFull(sphere, 2.0, TRUE, TRUE, result);
             if (result != sphere)
                 delete sphere;
             return fingerprintOf(status, result);
         }},
        {"extrude", [] {
             SmTArray<SmCurve*> profile = rectangle(0, 0, 30, 12);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateLinearSweep(profile, SmVector3d(0, 0, 1), 15.0, TRUE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"revolve", [] {
             SmTArray<SmCurve*> profile = rectangle(20, 0, 6, 10);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateRotationalSweep(profile, SmPoint3d(0, 0, 0), SmVector3d(0, 1, 0), 360.0, TRUE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"taper extrude", [] {
             SmTArray<SmCurve*> profile = rectangle(0, 0, 30, 12);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateTaperExtrude(profile, 10.0, 5.0, 3, FALSE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"helix pipe (thread)", [] {
             SmBSplineCurve* helix = NULL;
             SmApiStatus status = SmApiCreateHelix(SmPoint3d(0, 0, 0), 30.0, 10.0, 10.0, 5.0, TRUE, 1.0e-4, helix);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
                 status = SmApiCreatePipeSweep(1.0, helix, TRUE, result);
             delete helix;
             return fingerprintOf(status, result);
         }},
        {"pipe along a curve", [] {
             SmTArray<SmPoint3d> points;
             points.Add(SmPoint3d(0, 0, 0));
             points.Add(SmPoint3d(5, 5, 0));
             points.Add(SmPoint3d(10, 0, 3));
             points.Add(SmPoint3d(15, 5, 0));
             SmBSplineCurve* path = NULL;
             SmApiStatus status = SmApiCreateCurve(points, path);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
                 status = SmApiCreatePipeSweep(1.0, path, TRUE, result);
             delete path;
             return fingerprintOf(status, result);
         }},
        {"cut by a plane", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             const SmApiStatus status = SmApiCut(body, SmVector3d(10, 0, 0), SmVector3d(1, 0.2, 0.1));
             return fingerprintOf(status, body);
         }},
        {"copy, move, turn and heal", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             filletAll(body, 2.0);
             SmBrep* copy = NULL;
             SmApiStatus status = SmApiBrepCopy(body, copy);
             delete body;
             if (status == SM_SUCCESS)
             {
                 SmApiTranslate(copy, SmVector3d(5, -3, 2));
                 SmApiRotate(copy, SmVector3d(0, 0, 0), SmVector3d(0, 0, 1), 30.0);
                 status = SmApiHealBrep(copy);
             }
             return fingerprintOf(status, copy);
         }},
        {"closest point", [] {
             SmBrep* result = NULL;
             SmApiStatus status = combine(SmApiBooleanDifference, box(0, 0, 0, 40, 30, 20), cylinder(20, 15, -10, 8, 40), result);
             Fingerprint print;
             if (status == SM_SUCCESS)
             {
                 SmPoint3d closest;
                 double distance = 0.0;
                 status = SmApiBrepClosestPoint(result, SmPoint3d(21, 14, 30), closest, distance);
                 print.volume = distance;
                 print.area = closest.x + closest.y + closest.z;
             }
             delete result;
             print.status = static_cast<int>(status);
             return print;
         }},
        {"cone", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreateCone(SmVector3d(0, 0, 0), 10.0, 4.0, 20.0, body), body);
         }},
        {"partial cylinder", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreatePartialCylinder(SmVector3d(0, 0, 0), 10.0, 20.0, 30.0, 250.0, body), body);
         }},
        {"partial cone", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreatePartialCone(SmVector3d(0, 0, 0), 10.0, 4.0, 20.0, 0.0, 270.0, body), body);
         }},
        {"partial sphere", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreatePartialSphere(SmVector3d(0, 0, 0), 10.0, 0.0, 120.0, body), body);
         }},
        {"partial torus", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreatePartialTorus(SmVector3d(0, 0, 0), 15.0, 4.0, 0.0, 200.0, body), body);
         }},
        {"cylindrical box", [] {
             SmBrep* body = NULL;
             return fingerprintOf(SmApiCreateCylindricalBox(SmVector3d(0, 0, 0), 10.0, 6.0, 12.0, 0.0, 300.0, body), body);
         }},
        {"fillet per edge", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             SmTArray<SmEdge*> edges, chosen;
             body->GetEdges(edges);
             SmTArray<double> radii;
             SmTArray<SmFilletSurfaceGeneratorType> sections;
             for (ULONG index = 0; index < 4; ++index)
             {
                 chosen.Add(edges[index]);
                 radii.Add(1.0 + 0.5 * index);
                 sections.Add(SM_FSG_CIRCULAR);
             }
             return fingerprintOf(SmApiFilletEdgesPerEdge(body, chosen, radii, sections, 1, 1.0), body);
         }},
        {"merge many (merge_breps)", [] {
             SmTArray<SmBrep*> bodies;
             bodies.Add(box(0, 0, 0, 60, 20, 2));
             for (int index = 0; index < 6; ++index)
                 bodies.Add(cylinder(5.0 + 9.5 * index, 10.0, 1.0, 3.0, 4.0));
             SmBrep* result = NULL;
             // the bodies are consumed, also on failure
             return fingerprintOf(SmApiMergeBreps(bodies, SM_BO_UNION, result), result);
         }},
        {"imprint (boolean with options)", [] {
             SmBrep* body = box(0, 0, 0, 40, 30, 20);
             SmBrep* tool = cylinder(20, 15, 15, 6, 10);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiBooleanWithOptions(body, tool, SM_BO_UNION, FALSE, TRUE, TRUE, FALSE, FALSE, result);
             if (status != SM_SUCCESS)
             {
                 delete body;
                 delete tool;
                 result = NULL;
             }
             return fingerprintOf(status, result);
         }},
        {"loft (skin)", [] {
             const double sections[3][3] = {{30, 12, 0}, {20, 20, 10}, {10, 6, 20}};  // length, width, height
             SmTArray<SmCurve*> curves;
             SmTArray<const SmCurve*> profiles;
             SmTArray<ULONG> counts;
             for (const auto& section : sections)
             {
                 SmTArray<SmCurve*> profile = rectangle(0, 0, section[0], section[1], section[2]);
                 counts.Add(profile.GetSize());
                 for (ULONG index = 0; index < profile.GetSize(); ++index)
                 {
                     curves.Add(profile[index]);
                     profiles.Add(profile[index]);
                 }
             }
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateSkinPrimitive(counts, profiles, 2, TRUE, result, NULL, NULL, NULL);
             deleteCurves(curves);
             return fingerprintOf(status, result);
         }},
        {"sweep along a planar path", [] {
             SmBSplineCurve* circle = NULL;
             SmApiCreateCircle(SmPoint3d(0, 0, 0), 1.5, circle);
             SmApiRotate(circle, SmVector3d(0, 0, 0), SmVector3d(0, 1, 0), 90.0);
             SmLine* line = NULL;
             SmApiCreateLineSegment(SmPoint3d(0, 0, 0), SmPoint3d(20, 0, 0), line);
             SmBSplineCurve* arc = NULL;
             SmApiCreateArc(SmPoint3d(20, 10, 0), 10.0, -90.0, 0.0, arc);
             SmTArray<SmCurve*> profile, path;
             profile.Add(circle);
             path.Add(line);
             path.Add(arc);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateSweepAlongPlanarPath(profile, path, TRUE, 3, result);
             deleteCurves(profile);
             deleteCurves(path);
             return fingerprintOf(status, result);
         }},
        {"pipe along interpolated points", [] {
             SmTArray<SmPoint3d> points;
             points.Add(SmPoint3d(0, 0, 0));
             points.Add(SmPoint3d(6, 4, 1));
             points.Add(SmPoint3d(12, 0, 3));
             points.Add(SmPoint3d(18, 5, 2));
             points.Add(SmPoint3d(24, 2, 0));
             SmCurve* path = NULL;
             SmApiStatus status = SmApiCreateCurveInterpPoints(points, path, SM_CP_CHORDLENGTH);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
             {
                 SmBSplineCurve* spline = dynamic_cast<SmBSplineCurve*>(path);
                 status = spline ? SmApiCreatePipeSweep(1.0, spline, TRUE, result) : SM_ERR;
             }
             delete path;
             return fingerprintOf(status, result);
         }},
        {"offset profile", [] {
             SmTArray<SmCurve*> outline = rectangle(0, 0, 30, 12);
             SmBrep* result = NULL;
             const SmApiStatus status = SmApiCreateOffsetProfile(outline, 2.0, 3, TRUE, FALSE, result);  // 3: both sides
             deleteCurves(outline);
             return fingerprintOf(status, result);
         }},
        {"turn to NURBS", [] {
             SmBrep* body = cylinder(0, 0, 0, 10, 20);
             return fingerprintOf(SmApiTurnToNurbs(body), body);
         }},
        {"scale about a point", [] {
             SmBrep* body = roundedBoxWithHole();
             const SmApiStatus status = body ? SmApiScaleByPt(body, SmVector3d(5, 5, 0), SmVector3d(1.5, 0.8, 2.0)) : SM_ERR;
             return fingerprintOf(status, body);
         }},
        {"section by a plane", [] {
             SmBrep* body = roundedBoxWithHole();
             SmTArray<SmCurve*> curves;
             const SmApiStatus status = body ? SmApiIntersectBrepWithPlane(body, SmVector3d(0, 0, 7), SmVector3d(0, 0.3, 1), curves) : SM_ERR;
             delete body;
             return curvesPrint(status, curves);
         }},
        {"line through a body", [] {
             SmBrep* body = roundedBoxWithHole();
             SmLine* line = NULL;
             SmApiCreateLineSegment(SmPoint3d(-5, 15, 10), SmPoint3d(45, 15.5, 10), line);
             SmTArray<SmCurve*> curves;
             SmTArray<SmPoint3d> points;
             const SmApiStatus status = body ? SmApiIntersectCurveBrep(line, body, curves, points) : SM_ERR;
             deleteCurves(curves);
             delete line;
             delete body;
             return pointsPrint(status, points);
         }},
        {"tight box and surface closest points", [] {
             SmBrep* body = roundedBoxWithHole();
             SmPoint3d low, high;
             SmApiStatus status = body ? SmApiBrepBoundingBox(body, TRUE, low, high) : SM_ERR;
             SmTArray<SmPoint3d> points;
             points.Add(low);
             points.Add(high);
             if (status == SM_SUCCESS)
             {
                 SmTArray<SmFace*> faces;
                 body->GetFaces(faces);
                 for (ULONG index = 0; index < faces.GetSize() && status == SM_SUCCESS; ++index)
                 {
                     SmPoint3d closest;
                     SmTArray<SmPoint2d> parameters;
                     double distance = 0.0;
                     status = SmApiSurfaceClosestPoint(faces[index]->GetSurface(), SmPoint3d(21, 14, 30), closest, parameters, distance);
                     points.Add(closest);
                 }
             }
             delete body;
             return pointsPrint(status, points);
         }},
        {"edge outlines (tessellate boundaries)", [] {
             SmBrep* body = roundedBoxWithHole();
             SmTArray<SmPoint3d> points;
             SmTArray<ULONG> counts;
             SmTArray<SmEdge*> edges;
             const SmApiStatus status = body ? SmApiTessellateBoundaries(body, 4.0, points, counts, edges) : SM_ERR;
             Fingerprint print = pointsPrint(status, points);
             print.edges = edges.GetSize();
             delete body;
             return print;
         }},
        {"tessellate coarse (draft)", [] {
             SmTessellationParams draft;
             draft.dChordHeightTol = 0.5;
             draft.dCurveAngleTolDeg = draft.dSurfaceAngleTolDeg = 20.0;
             SmBrep* body = roundedBoxWithHole();
             return fingerprintOf(body ? SM_SUCCESS : SM_ERR, body, draft);
         }},
        {"tessellate fine, edge length limit", [] {
             SmTessellationParams fine;
             fine.dChordHeightTol = 0.01;
             fine.dCurveAngleTolDeg = fine.dSurfaceAngleTolDeg = 8.0;
             fine.dMaxEdgeLength = 2.0;
             SmBrep* body = roundedBoxWithHole();
             return fingerprintOf(body ? SM_SUCCESS : SM_ERR, body, fine);
         }},
        {"squeeze a sliver face", [] {
             // four rounds a micrometre short of meeting leave a face 2 um across
             SmBrep* body = box(0, 0, 0, 40, 40, 40);
             SmTArray<SmEdge*> edges, top;
             body->GetEdges(edges);
             for (ULONG index = 0; index < edges.GetSize(); ++index)
             {
                 SmPoint3d low, high;
                 SmApiEdgeBoundingBox(edges[index], TRUE, low, high);
                 if (std::fabs(low.z - 40.0) < 1.0e-6 && std::fabs(high.z - 40.0) < 1.0e-6)
                     top.Add(edges[index]);
             }
             SmApiStatus status = SmApiFilletEdges(body, top, 19.999, 1, 1, 1.0);
             SmFace* sliver = NULL;
             if (status == SM_SUCCESS)
             {
                 SmTArray<SmFace*> faces;
                 body->GetFaces(faces);
                 for (ULONG index = 0; index < faces.GetSize(); ++index)
                 {
                     SmPoint3d low, high;
                     SmApiFaceBoundingBox(faces[index], TRUE, low, high);
                     if (std::hypot(high.x - low.x, high.y - low.y, high.z - low.z) < 3.0e-3)
                         sliver = faces[index];
                 }
                 status = sliver ? SmApiSqueezeFace(body, sliver, 4.0e-3) : SM_ERR;
             }
             return fingerprintOf(status, body);
         }},
        {"Bezier arch (canonical curve), extruded", [] {
             // a sketch's Bezier segment as cadler builds it, closed by a line and extruded
             SmTArray<SmPoint3d> points;
             points.Add(SmPoint3d(0, 0, 0));
             points.Add(SmPoint3d(5, 15, 0));
             points.Add(SmPoint3d(15, 15, 0));
             points.Add(SmPoint3d(20, 0, 0));
             SmTArray<double> knots;
             knots.Add(0.0);
             knots.Add(1.0);
             SmTArray<ULONG> multiplicities;
             multiplicities.Add(4);
             multiplicities.Add(4);
             SmBSplineCurve* arch = NULL;
             SmApiStatus status = SmApiCreateCanonicalCurve(points, knots, multiplicities, NULL, 3, SM_CF_UNSPECIFIED, arch);
             SmLine* base = NULL;
             SmApiCreateLineSegment(SmPoint3d(20, 0, 0), SmPoint3d(0, 0, 0), base);
             SmTArray<SmCurve*> profile;
             profile.Add(arch);
             profile.Add(base);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
                 status = SmApiCreateLinearSweep(profile, SmVector3d(0, 0, 1), 5.0, TRUE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"rational arc (weighted canonical curve), extruded", [] {
             // a quarter circle as cadler's loft alignment builds arcs: degree 2 with weights
             const double weight = std::cos(M_PI / 4.0);
             SmTArray<SmPoint3d> points;
             points.Add(SmPoint3d(10, 0, 0));
             points.Add(SmPoint3d(10, 10, 0));
             points.Add(SmPoint3d(0, 10, 0));
             SmTArray<double> knots, weights;
             knots.Add(0.0);
             knots.Add(1.0);
             weights.Add(1.0);
             weights.Add(weight);
             weights.Add(1.0);
             SmTArray<ULONG> multiplicities;
             multiplicities.Add(3);
             multiplicities.Add(3);
             SmBSplineCurve* arc = NULL;
             SmApiStatus status = SmApiCreateCanonicalCurve(points, knots, multiplicities, &weights, 2, SM_CF_UNSPECIFIED, arc);
             SmLine* up = NULL;
             SmLine* across = NULL;
             SmApiCreateLineSegment(SmPoint3d(0, 10, 0), SmPoint3d(0, 0, 0), up);
             SmApiCreateLineSegment(SmPoint3d(0, 0, 0), SmPoint3d(10, 0, 0), across);
             SmTArray<SmCurve*> profile;
             profile.Add(arc);
             profile.Add(up);
             profile.Add(across);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
                 status = SmApiCreateLinearSweep(profile, SmVector3d(0, 0, 1), 5.0, TRUE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"ellipse placed by a transform, extruded", [] {
             const double angle = 30.0 * M_PI / 180.0;
             SmEllipse* ellipse = NULL;
             SmApiStatus status = SmApiCreateEllipse(SmPoint3d(0, 0, 0), 8.0, 4.0, ellipse);
             if (status == SM_SUCCESS)
                 status = SmApiTransform(ellipse, SmAxis2Placement(SmPoint3d(5, 3, 0), SmVector3d(std::cos(angle), std::sin(angle), 0), SmVector3d(-std::sin(angle), std::cos(angle), 0)));
             SmTArray<SmCurve*> profile;
             profile.Add(ellipse);
             SmBrep* result = NULL;
             if (status == SM_SUCCESS)
                 status = SmApiCreateLinearSweep(profile, SmVector3d(0, 0, 1), 6.0, TRUE, result);
             deleteCurves(profile);
             return fingerprintOf(status, result);
         }},
        {"body placed by a transform", [] {
             // a tilted frame, as cadler places copies and tilted cuts
             SmBrep* body = roundedBoxWithHole();
             const SmVector3d across(0.8, 0.6, 0.0);
             const SmVector3d up(-0.36, 0.48, 0.8);
             const SmApiStatus status = body ? SmApiTransform(body, SmAxis2Placement(SmPoint3d(10, -4, 7), across, up)) : SM_ERR;
             return fingerprintOf(status, body);
         }},
        {"evaluate a curve", [] {
             SmTArray<SmPoint3d> points;
             points.Add(SmPoint3d(0, 0, 0));
             points.Add(SmPoint3d(6, 4, 1));
             points.Add(SmPoint3d(12, 0, 3));
             points.Add(SmPoint3d(18, 5, 2));
             SmCurve* curve = NULL;
             SmApiStatus status = SmApiCreateCurveInterpPoints(points, curve, SM_CP_CHORDLENGTH);
             SmTArray<SmPoint3d> values;
             if (status == SM_SUCCESS)
             {
                 const SmExtent1d range = curve->GetNaturalInterval();
                 for (int step = 0; step <= 8 && status == SM_SUCCESS; ++step)
                 {
                     SmVector3d point, tangent, curvature;
                     SmVector3d* pointOut = &point;
                     SmVector3d* tangentOut = &tangent;
                     SmVector3d* curvatureOut = &curvature;
                     status = SmApiEvaluateCurve(curve, range.GetMin() + (range.GetMax() - range.GetMin()) * step / 8.0, pointOut, tangentOut, curvatureOut);
                     values.Add(SmPoint3d(point.x, point.y, point.z));
                     values.Add(SmPoint3d(tangent.x, tangent.y, tangent.z));
                     values.Add(SmPoint3d(curvature.x, curvature.y, curvature.z));
                 }
             }
             delete curve;
             return pointsPrint(status, values);
         }},
        {"material census", [] {
             // a box with a ball-shaped void inside: one solid, one void
             SmBrep* ball = NULL;
             SmApiCreateSphere(SmVector3d(20, 15, 10), 5.0, ball);
             SmBrep* body = NULL;
             SmApiStatus status = combine(SmApiBooleanDifference, box(0, 0, 0, 40, 30, 20), ball, body);
             Fingerprint print;
             if (status == SM_SUCCESS)
             {
                 long solids = 0, voids = 0;
                 status = SmApiBrepMaterialCensus(body, solids, voids);
                 print.faces = static_cast<unsigned long>(solids);
                 print.edges = static_cast<unsigned long>(voids);
             }
             delete body;
             print.status = static_cast<int>(status);
             return print;
         }},
        {"import: read, heal, stitch", [] {
             SmBrep* body = NULL;
             SmApiStatus status = SmApiReadBrepFromFile(importFile.c_str(), TRUE, TRUE, body);
             if (status == SM_SUCCESS)
                 status = SmApiHealBrep(body);
             if (status == SM_SUCCESS)
             {
                 SmBoolean solid = FALSE;
                 ULONG stitched = 0;
                 double vertexGap = 0.0, edgeGap = 0.0;
                 status = SmApiStitchIntoSolid(body, solid, stitched, vertexGap, edgeGap);
             }
             return fingerprintOf(status, body);
         }},
    };
}

// Every operation in order, `passes` times, in the calling thread; the results in that order.
std::vector<Fingerprint> runSequence(const std::vector<Operation>& sequence, int passes)
{
    std::vector<Fingerprint> results;
    for (int pass = 0; pass < passes; ++pass)
        for (const Operation& operation : sequence)
        {
            if (std::getenv("HARNESS_TRACE"))
                std::fprintf(stderr, "-> %s\n", operation.name);
            results.push_back(operation.run());
        }
    SmApiDeleteContext();
    return results;
}

std::vector<int> parseCounts(const char* text)
{
    std::vector<int> counts;
    for (const char* cursor = text; *cursor;)
    {
        counts.push_back(std::atoi(cursor));
        while (*cursor && *cursor != ',')
            ++cursor;
        if (*cursor == ',')
            ++cursor;
    }
    return counts;
}

double secondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// A slot cut across a body; the body is used up.
Fingerprint cutSlot(SmBrep* body)
{
    SmBrep* result = NULL;
    const SmApiStatus status = combine(SmApiBooleanDifference, body, box(-5, 12, 8, 50, 6, 4), result);
    return fingerprintOf(status, result);
}

// What `use` gives on a body `build` made, both in one fresh thread: what a handed body must give.
Fingerprint alone(const std::function<SmBrep*()>& build, const std::function<Fingerprint(SmBrep*)>& use)
{
    Fingerprint result;
    std::thread([&] {
        result = use(build());
        SmApiDeleteContext();
    }).join();
    return result;
}

SmBrep* copyOf(SmBrep* body)
{
    SmBrep* copy = NULL;
    SmApiBrepCopy(body, copy);
    delete body;
    return copy;
}

// Bodies crossing threads, by the rules of .agents/operations/concurrency.md. A thread whose body
// lives on in another keeps its context (deleting it would leave the body pointing at freed
// memory). Returns how many results differ.
int handOffs(const std::vector<Operation>& sequence, const std::vector<Fingerprint>& reference, int passes)
{
    int wrong = 0;
    auto check = [&](const char* what, const Fingerprint& result, const Fingerprint& expected) {
        if (!(result == expected))
        {
            std::printf("  %s: the result differs\n", what);
            ++wrong;
        }
    };

    // a body built in one thread, used and deleted in another once its builder rests
    SmBrep* handed = NULL;
    std::thread([&] { handed = roundedBoxWithHole(); }).join();
    Fingerprint result;
    std::thread([&] {
        result = cutSlot(handed);
        SmApiDeleteContext();
    }).join();
    check("a body handed to another thread", result, alone(roundedBoxWithHole, cutSlot));

    // a body from another thread as a boolean's tool (used up by it), its builder resting
    SmBrep* tool = NULL;
    std::thread([&] { tool = cylinder(20, 15, -10, 8, 40); }).join();
    auto drill = [](SmBrep* hole) {
        SmBrep* body = box(0, 0, 0, 40, 30, 20);
        filletAll(body, 3.0);
        SmBrep* drilled = NULL;
        const SmApiStatus status = combine(SmApiBooleanDifference, body, hole, drilled);
        return fingerprintOf(status, drilled);
    };
    std::thread([&] {
        result = drill(tool);
        SmApiDeleteContext();
    }).join();
    check("a tool from another thread", result, alone([] { return cylinder(20, 15, -10, 8, 40); }, drill));

    // copies of one body, taken under its lock by several threads while its builder keeps working
    // in its own context: how a cache shared by threads hands out bodies
    const int copiers = 4;
    const int rounds = 4 * passes;
    const Fingerprint expected = alone([] { return copyOf(roundedBoxWithHole()); }, cutSlot);
    std::mutex sharedLock;
    SmBrep* shared = NULL;
    std::atomic<bool> ready(false), copied(false);
    int builderWrong = 0;
    std::thread builder([&] {
        shared = roundedBoxWithHole();
        ready = true;
        for (std::size_t index = 0; !copied || index == 0; ++index)
            if (!(sequence[index % sequence.size()].run() == reference[index % reference.size()]))
                ++builderWrong;
        delete shared;
        SmApiDeleteContext();
    });
    while (!ready)
        std::this_thread::yield();
    std::vector<int> copierWrong(copiers, 0);
    std::vector<std::thread> workers;
    for (int copier = 0; copier < copiers; ++copier)
        workers.emplace_back([&, copier] {
            for (int round = 0; round < rounds; ++round)
            {
                SmBrep* copy = NULL;
                {
                    std::lock_guard<std::mutex> guard(sharedLock);
                    SmApiBrepCopy(shared, copy);
                }
                if (!(cutSlot(copy) == expected))
                    ++copierWrong[copier];
            }
            SmApiDeleteContext();
        });
    for (std::thread& worker : workers)
        worker.join();
    copied = true;
    builder.join();
    for (int copier = 0; copier < copiers; ++copier)
        if (copierWrong[copier])
        {
            std::printf("  copies under a lock, copier %d: %d of %d results differ\n", copier, copierWrong[copier], rounds);
            wrong += copierWrong[copier];
        }
    if (builderWrong)
    {
        std::printf("  copies under a lock: %d results of the builder differ\n", builderWrong);
        wrong += builderWrong;
    }
    std::printf("hand-offs: a body, a tool, %d copies under a lock while the builder works, %d differ\n", copiers * rounds, wrong);
    return wrong;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<int> threadCounts = {1, 2, 4, 8};
    int passes = 3;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        if (std::strcmp(argv[index], "--threads") == 0)
            threadCounts = parseCounts(argv[index + 1]);
        else if (std::strcmp(argv[index], "--passes") == 0)
            passes = std::atoi(argv[index + 1]);
    }
    importFile = writeImportFile();
    const std::vector<Operation> sequence = operations();
    const std::size_t count = sequence.size();

    // the reference: the sequence alone in a fresh thread
    std::vector<Fingerprint> reference;
    const auto serialStart = std::chrono::steady_clock::now();
    std::thread([&] { reference = runSequence(sequence, passes); }).join();
    const double serialSeconds = secondsSince(serialStart);
    for (std::size_t index = 0; index < count; ++index)
        if (reference[index].status != SM_SUCCESS)
            std::printf("note: %s fails alone too (status %d)\n", sequence[index].name, reference[index].status);

    int mismatches = 0;
    for (int threads : threadCounts)
    {
        std::vector<std::vector<Fingerprint>> results(threads);
        std::vector<std::thread> workers;
        const auto start = std::chrono::steady_clock::now();
        for (int thread = 0; thread < threads; ++thread)
            workers.emplace_back([&, thread] { results[thread] = runSequence(sequence, passes); });
        for (std::thread& worker : workers)
            worker.join();
        const double seconds = secondsSince(start);
        int wrong = 0;
        for (int thread = 0; thread < threads; ++thread)
            for (std::size_t index = 0; index < reference.size(); ++index)
                if (!(results[thread][index] == reference[index]))
                {
                    if (wrong < 10)
                        std::printf("  thread %d, pass %zu: %s differs\n", thread, index / count + 1, sequence[index % count].name);
                    ++wrong;
                }
        mismatches += wrong;
        std::printf("%2d threads: %zu operations in %.2f s, %.1fx the throughput of one, %d differ\n",
                    threads, threads * reference.size(), seconds, threads * serialSeconds / seconds, wrong);
    }

    mismatches += handOffs(sequence, reference, passes);
    std::filesystem::remove(importFile);

    std::printf(mismatches ? "FAILED: %d results differ from the serial run\n" : "ok: every result matches the serial run\n", mismatches);
    return mismatches ? 1 : 0;
}
