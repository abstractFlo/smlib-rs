// SM_API in several threads of one process: every operation cadler uses runs in 1 to N threads at
// once, each thread building its own objects, and each result is compared bit for bit with the
// same sequence run alone in a fresh thread (volume, area, topology, tessellation). Also a body
// handed from one thread to another once its builder rests. Built twice by run.sh: against the
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
#include <SmApiPolygons.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmBSplineCurve.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmPoly.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
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

// Volume, area, topology and the tessellation of a body; the body is deleted.
Fingerprint fingerprintOf(SmApiStatus status, SmBrep* body)
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
    SmPolyBrep* mesh = NULL;
    if (SmApiTessellate(body, mesh) == SM_SUCCESS && mesh != NULL)
    {
        SmTArray<SmPolyVertex*> vertices;
        mesh->GetPolyVertices(vertices);
        result.meshVertices = vertices.GetSize();
        std::uint64_t hash = 1469598103934665603ull;
        for (ULONG index = 0; index < vertices.GetSize(); ++index)
        {
            const SmPoint3d point = vertices[index]->GetPoint();
            hash = hashBytes(hash, &point.x, sizeof point.x);
            hash = hashBytes(hash, &point.y, sizeof point.y);
            hash = hashBytes(hash, &point.z, sizeof point.z);
        }
        result.meshHash = hash;
        delete mesh;
    }
    delete body;
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

SmTArray<SmCurve*> rectangle(double x, double y, double length, double width)
{
    SmTArray<SmBSplineCurve*> sides;
    SmApiCreateRectangle(SmPoint3d(x, y, 0.0), length, width, sides);
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

    // a body built in one thread, used and deleted in another once its builder rests
    SmBrep* handed = NULL;
    std::thread([&] {
        handed = box(0, 0, 0, 40, 30, 20);
        filletAll(handed, 3.0);
    }).join();
    Fingerprint handedPrint;
    std::thread([&] {
        SmBrep* result = NULL;
        const SmApiStatus status = combine(SmApiBooleanDifference, handed, cylinder(20, 15, -10, 8, 40), result);
        handedPrint = fingerprintOf(status, result);
        SmApiDeleteContext();
    }).join();
    Fingerprint expected;
    std::thread([&] {
        SmBrep* own = box(0, 0, 0, 40, 30, 20);
        filletAll(own, 3.0);
        SmBrep* result = NULL;
        const SmApiStatus status = combine(SmApiBooleanDifference, own, cylinder(20, 15, -10, 8, 40), result);
        expected = fingerprintOf(status, result);
        SmApiDeleteContext();
    }).join();
    if (!(handedPrint == expected))
    {
        std::printf("a body handed to another thread gives a different result\n");
        ++mismatches;
    }

    std::printf(mismatches ? "FAILED: %d results differ from the serial run\n" : "ok: every result matches the serial run\n", mismatches);
    return mismatches ? 1 : 0;
}
