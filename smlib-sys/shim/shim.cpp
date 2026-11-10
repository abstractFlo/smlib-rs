// SPDX-License-Identifier: Apache-2.0
// The C interface of shim.h over SM_API and SMLib. Thin by design: what usd-brep's own binding
// (source/SmPyLib) does around a call is done here the same way, so SMLib gives the results it
// gives there, and everything above it lives in Rust.

#include "shim.h"

#include <SmSmlibAll.h>
#include <SmPoly.h>
#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiFillets.h>
#include <SmApiGeneral.h>
#include <SmApiHeal.h>
#include <SmApiImportExport.h>
#include <SmApiIntersectors.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmApiSurfaces.h>
#include <SmApiTrimmedSurfaces.h>
#include <SmAxis2Placement.h>
#include <SmThreadLocalStorage.h>
#include <nurbs.h>

#if defined(_UNICODE) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <cmath>
#include <cstring>
#include <exception>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct StatusName
{
    SmStatus code;
    const char* name;
};

const StatusName kStatusNames[] = {
    {SM_SUCCESS, "SM_SUCCESS"},
    {SM_ERR, "SM_ERR"},
    {SM_ERR_OUT_OF_MEMORY, "SM_ERR_OUT_OF_MEMORY"},
    {SM_ERR_UNKNOWN, "SM_ERR_UNKNOWN"},
    {SM_ERR_FATAL, "SM_ERR_FATAL"},
    {SM_ERR_ASSERT_FAILURE, "SM_ERR_ASSERT_FAILURE"},
    {SM_ERR_NULL_POINTER, "SM_ERR_NULL_POINTER"},
    {SM_ERR_INVALID_INPUT, "SM_ERR_INVALID_INPUT"},
    {SM_ERR_NON_NULL_OUTPUT_POINTER, "SM_ERR_NON_NULL_OUTPUT_POINTER"},
    {SM_ERR_ASSERTVALID_FAILURE, "SM_ERR_ASSERTVALID_FAILURE"},
    {SM_ERR_OUTSIDE_OF_DOMAIN, "SM_ERR_OUTSIDE_OF_DOMAIN"},
    {SM_ERR_NOT_WITHIN_TOLERANCE, "SM_ERR_NOT_WITHIN_TOLERANCE"},
    {SM_ERR_NOT_CONVERGING, "SM_ERR_NOT_CONVERGING"},
    {SM_ERR_WARNING, "SM_ERR_WARNING"},
    {SM_ERR_MESSAGE, "SM_ERR_MESSAGE"},
    {SM_ERR_AXIS_INSIDE_FACE, "SM_ERR_AXIS_INSIDE_FACE"},
    {SM_ERR_LICENSE_EXPIRED, "SM_ERR_LICENSE_EXPIRED"},
    {SM_ERR_BAD_INTERSECTIONS, "SM_ERR_BAD_INTERSECTIONS"},
    {SM_ERR_BAD_COINCIDENT_VERTICES, "SM_ERR_BAD_COINCIDENT_VERTICES"},
    {SM_ERR_BAD_SURFACE_POINT, "SM_ERR_BAD_SURFACE_POINT"},
    {SM_ERR_BAD_TANGENT_DROP, "SM_ERR_BAD_TANGENT_DROP"},
    {SM_ERR_DEGENERATE_SURFACE, "SM_ERR_DEGENERATE_SURFACE"},
    {SM_ERR_BAD_FIND_DEGEN_PARAM, "SM_ERR_BAD_FIND_DEGEN_PARAM"},
    {SM_ERR_METHOD_FAILURE_QUITING, "SM_ERR_METHOD_FAILURE_QUITING"},
    {SM_ERR_NOTYET_HEAL_FACE, "SM_ERR_NOTYET_HEAL_FACE"},
    {SM_ERR_TESS_FAIL_FACE, "SM_ERR_TESS_FAIL_FACE"},
};

const char* statusName(SmStatus status)
{
    for (const StatusName& entry : kStatusNames)
        if (entry.code == status)
            return entry.name;
    return "SM_ERR_???";
}

std::string baseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// what SMLib reported during the current call, and the message of the last failure
thread_local std::string tTrail;
thread_local bool tTrailFull = false;
thread_local std::string tError;

void clearTrail()
{
    tTrail.clear();
    tTrailFull = false;
}

void errorCallback(SmStatus error, const TCHAR* fileName, ULONG lineNumber, const TCHAR* message)
{
    if (error == SM_SUCCESS || error == SM_ERR_WARNING || error == SM_ERR_MESSAGE || tTrailFull)
        return;
    std::string entry = statusName(error);
    const std::string file = smos_FromTChar(fileName);
    if (!file.empty())
        entry += " at " + baseName(file) + ":" + std::to_string(lineNumber);
    const std::string text = smos_FromTChar(message);
    if (!text.empty())
        entry += " (" + text + ")";
    if (!tTrail.empty())
        tTrail += "; ";
    tTrail += entry;
    if (tTrail.size() >= 4096)
    {
        tTrail += " ...[truncated]";
        tTrailFull = true;
    }
}

// The message usd-brep's binding gives (SmPyRaise): a failure worded as SMLib's users know it.
int fail(const char* entry, SmStatus status)
{
    tError = std::string(entry) + " failed: " + statusName(status) + " (" + std::to_string(static_cast<long>(status)) + ")";
    if (!tTrail.empty())
        tError += ". Kernel trace: " + tTrail;
    clearTrail();
    return status == SM_SUCCESS ? -1 : static_cast<int>(status);
}

int failWith(const char* entry, const char* what)
{
    tError = std::string(entry) + ": " + what;
    clearTrail();
    return -1;
}

#ifdef _UNICODE
using TPath = std::wstring;
#else
using TPath = std::string;
#endif

// a UTF-8 path as SMLib's TCHAR text
TPath toTChar(const char* path)
{
#if defined(_UNICODE) && defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wide(length > 0 ? static_cast<size_t>(length) : 0, L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, path, -1, &wide[0], length);
    if (!wide.empty() && wide.back() == L'\0')
        wide.pop_back();
    return wide;
#else
    return TPath(path);
#endif
}

SmBrep* asBrep(CsBrep* value) { return reinterpret_cast<SmBrep*>(value); }
const SmBrep* asBrep(const CsBrep* value) { return reinterpret_cast<const SmBrep*>(value); }
SmBrep* asMutableBrep(const CsBrep* value) { return const_cast<SmBrep*>(reinterpret_cast<const SmBrep*>(value)); }
SmFace* asFace(const CsFace* value) { return const_cast<SmFace*>(reinterpret_cast<const SmFace*>(value)); }
SmEdge* asEdge(const CsEdge* value) { return const_cast<SmEdge*>(reinterpret_cast<const SmEdge*>(value)); }
SmVertex* asVertex(const CsVertex* value) { return const_cast<SmVertex*>(reinterpret_cast<const SmVertex*>(value)); }
SmCurve* asCurve(const CsCurve* value) { return const_cast<SmCurve*>(reinterpret_cast<const SmCurve*>(value)); }
SmSurface* asSurface(const CsSurface* value) { return const_cast<SmSurface*>(reinterpret_cast<const SmSurface*>(value)); }

SmVector3d asVec(const double* value) { return SmVector3d(value[0], value[1], value[2]); }
SmPoint3d asPoint(const double* value) { return SmPoint3d(value[0], value[1], value[2]); }

void put(const SmVector3d& value, double* out)
{
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

template <typename Item, typename Out>
size_t copyList(const SmTArray<Item*>& items, Out** out, size_t capacity)
{
    const size_t count = items.GetSize();
    for (size_t index = 0; index < count && index < capacity; ++index)
        out[index] = reinterpret_cast<Out*>(items[static_cast<ULONG>(index)]);
    return count;
}

SmObject* asObject(void* value, int kind)
{
    if (kind == 0)
        return static_cast<SmObject*>(reinterpret_cast<SmBrep*>(value));
    return static_cast<SmObject*>(reinterpret_cast<SmCurve*>(value));
}

void putBox(const SmPoint3d& low, const SmPoint3d& high, double* box)
{
    box[0] = low.x;
    box[1] = low.y;
    box[2] = low.z;
    box[3] = high.x;
    box[4] = high.y;
    box[5] = high.z;
}

} // namespace

#define CS_TRY try {
#define CS_CATCH(entry)                                                     \
    }                                                                       \
    catch (const std::exception& exception) { return failWith(entry, exception.what()); } \
    catch (...) { return failWith(entry, "unknown C++ exception"); }
#define CS_CHECK(entry, expression)                                         \
    do {                                                                    \
        clearTrail();                                                       \
        const SmStatus status_ = (expression);                              \
        if (status_ != SM_SUCCESS)                                          \
            return fail(entry, status_);                                    \
    } while (0)

struct CsMesh
{
    std::vector<float> points;
    std::vector<float> normals;
    std::vector<int32_t> stream;
    std::vector<int32_t> faceIds;
};

struct CsLines
{
    std::vector<double> points;
    std::vector<uint32_t> counts;
    std::vector<CsEdge*> edges;
};

extern "C" {

// ---- threads and errors ----

void cs_thread_start(void)
{
    SmApiGetOrCreateContext();
    if (SmThreadLocalStorage::GetErrCallbackFunction() != errorCallback)
        smos_SetErrorCallback(errorCallback);
}

void cs_thread_end(void) { SmApiDeleteContext(); }

const char* cs_error(void) { return tError.c_str(); }

// ---- breps ----

void cs_brep_free(CsBrep* value)
{
    try { delete asBrep(value); } catch (...) {}
}

int cs_brep_copy(const CsBrep* value, CsBrep** result)
{
    CS_TRY
    SmBrep* copy = nullptr;
    CS_CHECK("SmApiBrepCopy", SmApiBrepCopy(asBrep(value), copy));
    *result = reinterpret_cast<CsBrep*>(copy);
    return 0;
    CS_CATCH("SmApiBrepCopy")
}

size_t cs_brep_faces(const CsBrep* value, CsFace** faces, size_t capacity)
{
    SmTArray<SmFace*> items;
    asMutableBrep(value)->GetFaces(items);
    return copyList(items, faces, capacity);
}

size_t cs_brep_edges(const CsBrep* value, CsEdge** edges, size_t capacity)
{
    SmTArray<SmEdge*> items;
    asMutableBrep(value)->GetEdges(items);
    return copyList(items, edges, capacity);
}

size_t cs_brep_vertices(const CsBrep* value, CsVertex** vertices, size_t capacity)
{
    SmTArray<SmVertex*> items;
    asMutableBrep(value)->GetVertices(items);
    return copyList(items, vertices, capacity);
}

int cs_brep_bounding_box(const CsBrep* value, int tight, double box[6])
{
    CS_TRY
    SmPoint3d low, high;
    CS_CHECK("SmApiBrepBoundingBox", SmApiBrepBoundingBox(asBrep(value), tight ? TRUE : FALSE, low, high));
    putBox(low, high, box);
    return 0;
    CS_CATCH("SmApiBrepBoundingBox")
}

int cs_brep_volume(const CsBrep* value, double accuracy, double* volume)
{
    CS_TRY
    double result = 0.0;
    CS_CHECK("SmApiBrepComputeVolume", SmApiBrepComputeVolume(asBrep(value), accuracy, result));
    *volume = result;
    return 0;
    CS_CATCH("SmApiBrepComputeVolume")
}

int cs_brep_area(const CsBrep* value, double accuracy, double* area)
{
    CS_TRY
    double result = 0.0;
    CS_CHECK("SmApiBrepComputeArea", SmApiBrepComputeArea(asMutableBrep(value), accuracy, result));
    *area = result;
    return 0;
    CS_CATCH("SmApiBrepComputeArea")
}

int cs_brep_is_manifold_solid(const CsBrep* value, int* solid)
{
    CS_TRY
    SmBoolean result = FALSE;
    CS_CHECK("SmApiBrepIsManifoldSolid", SmApiBrepIsManifoldSolid(asBrep(value), result));
    *solid = result ? 1 : 0;
    return 0;
    CS_CATCH("SmApiBrepIsManifoldSolid")
}

uint64_t cs_brep_memory_used(const CsBrep* value)
{
    try
    {
        ULONG allocated = 0;
        asMutableBrep(value)->GetMemoryUsed(allocated);
        return static_cast<uint64_t>(allocated);
    }
    catch (...) { return 0; }
}

// as the binding's mass_properties: about the centroid when no origin is given
int cs_brep_mass_properties(const CsBrep* value, double accuracy, double density, const double* origin, double result[12])
{
    CS_TRY
    double area = 0.0, volume = 0.0, mass = 0.0;
    SmPoint3d centroid;
    SmVector3d moments, products;
    if (!origin)
    {
        SmPoint3d low, high;
        CS_CHECK("SmApiBrepBoundingBox", SmApiBrepBoundingBox(asBrep(value), FALSE, low, high));
        const SmPoint3d middle(0.5 * (low.x + high.x), 0.5 * (low.y + high.y), 0.5 * (low.z + high.z));
        double firstArea = 0.0, firstVolume = 0.0, firstMass = 0.0;
        SmVector3d firstMoments, firstProducts;
        CS_CHECK("SmApiBrepComputeMassProperties", SmApiBrepComputeMassProperties(asBrep(value), accuracy, density, middle, firstArea, firstVolume, firstMass, centroid, firstMoments, firstProducts));
        SmPoint3d again;
        CS_CHECK("SmApiBrepComputeMassProperties", SmApiBrepComputeMassProperties(asBrep(value), accuracy, density, centroid, area, volume, mass, again, moments, products));
    }
    else
    {
        CS_CHECK("SmApiBrepComputeMassProperties", SmApiBrepComputeMassProperties(asBrep(value), accuracy, density, asPoint(origin), area, volume, mass, centroid, moments, products));
    }
    result[0] = area;
    result[1] = volume;
    result[2] = mass;
    put(centroid, result + 3);
    put(moments, result + 6);
    put(products, result + 9);
    return 0;
    CS_CATCH("SmApiBrepComputeMassProperties")
}

int cs_brep_material_census(const CsBrep* value, int64_t* solids, int64_t* voids)
{
    CS_TRY
    long solidCount = 0, voidCount = 0;
    CS_CHECK("SmApiBrepMaterialCensus", SmApiBrepMaterialCensus(asBrep(value), solidCount, voidCount));
    *solids = solidCount;
    *voids = voidCount;
    return 0;
    CS_CATCH("SmApiBrepMaterialCensus")
}

int cs_brep_write(const CsBrep* value, const char* path, int ascii)
{
    CS_TRY
    CS_CHECK("SmApiWriteBrepToFile", SmApiWriteBrepToFile(asBrep(value), toTChar(path).c_str(), ascii ? TRUE : FALSE));
    return 0;
    CS_CATCH("SmApiWriteBrepToFile")
}

int cs_brep_read(const char* path, int ascii, CsBrep** result)
{
    CS_TRY
    SmBrep* read = nullptr;
    CS_CHECK("SmApiReadBrepFromFile", SmApiReadBrepFromFile(toTChar(path).c_str(), ascii ? TRUE : FALSE, FALSE, read));
    *result = reinterpret_cast<CsBrep*>(read);
    return 0;
    CS_CATCH("SmApiReadBrepFromFile")
}

// ---- faces ----

int cs_face_bounding_box(const CsFace* value, int tight, double box[6])
{
    CS_TRY
    SmPoint3d low, high;
    CS_CHECK("SmApiFaceBoundingBox", SmApiFaceBoundingBox(asFace(value), tight ? TRUE : FALSE, low, high));
    putBox(low, high, box);
    return 0;
    CS_CATCH("SmApiFaceBoundingBox")
}

int cs_face_area(const CsFace* value, double accuracy, double* area)
{
    CS_TRY
    double result = 0.0;
    CS_CHECK("SmApiFaceComputeArea", SmApiFaceComputeArea(asFace(value), accuracy, result));
    *area = result;
    return 0;
    CS_CATCH("SmApiFaceComputeArea")
}

int cs_face_centroid(const CsFace* value, double accuracy, double centroid[3])
{
    CS_TRY
    double area = 0.0;
    SmPoint3d result;
    CS_CHECK("SmApiFaceComputeCentroid", SmApiFaceComputeCentroid(asFace(value), accuracy, area, result));
    put(result, centroid);
    return 0;
    CS_CATCH("SmApiFaceComputeCentroid")
}

const CsSurface* cs_face_surface(const CsFace* value)
{
    return reinterpret_cast<const CsSurface*>(asFace(value)->GetSurface());
}

void cs_face_uv_domain(const CsFace* value, double domain[4])
{
    const SmExtent2d extent = asFace(value)->GetUVDomain();
    const SmPoint2d low = extent.GetMin(), high = extent.GetMax();
    domain[0] = low.x;
    domain[1] = low.y;
    domain[2] = high.x;
    domain[3] = high.y;
}

int cs_face_point_at_uv(const CsFace* value, double u, double v, double out[3])
{
    CS_TRY
    SmVector3d result;
    CS_CHECK("SmApiEvaluateSurfacePoint", SmApiEvaluateSurfacePoint(asFace(value)->GetSurface(), SmVector2d(u, v), result));
    put(result, out);
    return 0;
    CS_CATCH("SmApiEvaluateSurfacePoint")
}

int cs_face_normal_at_uv(const CsFace* value, double u, double v, int outward, double out[3])
{
    CS_TRY
    SmFace* target = asFace(value);
    SmVector3d normal;
    CS_CHECK("SmApiEvaluateSurfaceNormal", SmApiEvaluateSurfaceNormal(target->GetSurface(), SmVector2d(u, v), normal));
    if (outward)
    {
        SmBrep* owner = target->GetBrep();
        SmFaceuse* upward = target->GetUpwardFaceuse();
        if (owner && upward && upward->GetShell() && owner->GetInfiniteRegion() != upward->GetShell()->GetRegion())
            normal *= -1.0;
    }
    put(normal, out);
    return 0;
    CS_CATCH("SmApiEvaluateSurfaceNormal")
}

int cs_face_interior_point(const CsFace* value, double out[3])
{
    CS_TRY
    SmPoint3d result;
    CS_CHECK("SmApiFaceInternalPoint", SmApiFaceInternalPoint(asFace(value), result));
    put(result, out);
    return 0;
    CS_CATCH("SmApiFaceInternalPoint")
}

int cs_face_classify_uv(const CsFace* value, double u, double v, int* where)
{
    CS_TRY
    int classification = 0;
    CS_CHECK("SmApiFaceClassifyUV", SmApiFaceClassifyUV(asFace(value), SmPoint2d(u, v), classification));
    switch (classification)
    {
        case SM_PC_FACE: *where = 0; break;
        case SM_PC_UNKNOWN: *where = 1; break;
        case SM_PC_EDGE: *where = 2; break;
        case SM_PC_VERTEX: *where = 3; break;
        default: return failWith("SmApiFaceClassifyUV", "unexpected point classification");
    }
    return 0;
    CS_CATCH("SmApiFaceClassifyUV")
}

int cs_face_closest_point(const CsFace* value, const double target[3], double closest[3], double* distance)
{
    CS_TRY
    SmPoint3d result;
    double gap = 0.0;
    CS_CHECK("SmApiFaceClosestPoint", SmApiFaceClosestPoint(asFace(value), asPoint(target), result, gap));
    put(result, closest);
    *distance = gap;
    return 0;
    CS_CATCH("SmApiFaceClosestPoint")
}

size_t cs_face_edges(const CsFace* value, CsEdge** edges, size_t capacity)
{
    SmTArray<SmEdge*> items;
    asFace(value)->GetEdges(items);
    return copyList(items, edges, capacity);
}

size_t cs_face_vertices(const CsFace* value, CsVertex** vertices, size_t capacity)
{
    SmTArray<SmVertex*> items;
    asFace(value)->GetVertices(items);
    return copyList(items, vertices, capacity);
}

size_t cs_face_outer_loop_edges(const CsFace* value, CsEdge** edges, size_t capacity)
{
    SmTArray<SmEdge*> items;
    asFace(value)->GetOuterLoopEdges(items);
    return copyList(items, edges, capacity);
}

int cs_face_loops(const CsFace* value, uint32_t* loopSizes, size_t loopCapacity, size_t* loopCount,
                  CsEdge** edges, uint8_t* forward, size_t edgeCapacity, size_t* edgeCount)
{
    CS_TRY
    SmTArray<ULONG> sizes;
    SmTArray<SmEdge*> loopEdges;
    SmTArray<SmOrientType> orientations;
    CS_CHECK("SmApiFaceGetBoundaryLoops", SmApiFaceGetBoundaryLoops(asFace(value), sizes, loopEdges, orientations));
    if (orientations.GetSize() != loopEdges.GetSize())
        return failWith("SmApiFaceGetBoundaryLoops", "edges and orientations differ in number");
    *loopCount = sizes.GetSize();
    *edgeCount = loopEdges.GetSize();
    for (size_t index = 0; index < sizes.GetSize() && index < loopCapacity; ++index)
        loopSizes[index] = static_cast<uint32_t>(sizes[static_cast<ULONG>(index)]);
    for (size_t index = 0; index < loopEdges.GetSize() && index < edgeCapacity; ++index)
    {
        edges[index] = reinterpret_cast<CsEdge*>(loopEdges[static_cast<ULONG>(index)]);
        const SmOrientType orientation = orientations[static_cast<ULONG>(index)];
        if (orientation != SM_OT_SAME && orientation != SM_OT_OPPOSITE)
            return failWith("SmApiFaceGetBoundaryLoops", "an edge without a direction in its loop");
        forward[index] = orientation == SM_OT_SAME ? 1 : 0;
    }
    return 0;
    CS_CATCH("SmApiFaceGetBoundaryLoops")
}

void cs_face_user_ids(const CsFace* value, int64_t ids[2])
{
    ids[0] = static_cast<int64_t>(asFace(value)->GetUserIndex1());
    ids[1] = static_cast<int64_t>(asFace(value)->GetUserIndex2());
}

// above SMLib's own attribute ids (below 10000), as SmAttribute.h asks of applications
static const ULONG ORIGIN_TAG_ATTRIBUTE = 10101;

int cs_face_tag(const CsFace* value, int64_t* tag)
{
    const SmAttribute* attribute = asFace(value)->FindAttribute(ORIGIN_TAG_ATTRIBUTE);
    const SmLongAttribute* origin = attribute ? dynamic_cast<const SmLongAttribute*>(attribute) : nullptr;
    if (!origin) return 0;
    *tag = static_cast<int64_t>(origin->GetValue());
    return 1;
}

int cs_face_set_tag(const CsFace* value, int64_t tag)
{
    CS_TRY
    SmFace* face = asFace(value);
    if (SmAttribute* old = face->FindAttribute(ORIGIN_TAG_ATTRIBUTE)) face->RemoveAttribute(old);
    face->AddAttribute(new (*face->GetContext()) SmLongAttribute(ORIGIN_TAG_ATTRIBUTE, static_cast<long>(tag), SM_AB_COPY));
    return 0;
    CS_CATCH("SmAObject::AddAttribute")
}

const CsBrep* cs_face_brep(const CsFace* value) { return reinterpret_cast<const CsBrep*>(asFace(value)->GetBrep()); }

// ---- surfaces ----

int cs_surface_is_planar(const CsSurface* value, double tolerance)
{
    try { return asSurface(value)->IsPlanar(tolerance > 0 ? tolerance : SM_EFF_ZERO) ? 1 : 0; }
    catch (...) { return 0; }
}

void cs_surface_uv_domain(const CsSurface* value, double domain[4])
{
    const SmExtent2d extent = asSurface(value)->GetNaturalUVDomain();
    domain[0] = extent.GetMin().x;
    domain[1] = extent.GetMin().y;
    domain[2] = extent.GetMax().x;
    domain[3] = extent.GetMax().y;
}

int cs_surface_point(const CsSurface* value, double u, double v, double out[3])
{
    CS_TRY
    SmVector3d result;
    CS_CHECK("SmApiEvaluateSurfacePoint", SmApiEvaluateSurfacePoint(asSurface(value), SmVector2d(u, v), result));
    put(result, out);
    return 0;
    CS_CATCH("SmApiEvaluateSurfacePoint")
}

int cs_surface_normal(const CsSurface* value, double u, double v, double out[3])
{
    CS_TRY
    SmVector3d result;
    CS_CHECK("SmApiEvaluateSurfaceNormal", SmApiEvaluateSurfaceNormal(asSurface(value), SmVector2d(u, v), result));
    put(result, out);
    return 0;
    CS_CATCH("SmApiEvaluateSurfaceNormal")
}

int cs_surface_derivatives(const CsSurface* value, double u, double v, double result[9])
{
    CS_TRY
    SmVector3d position, alongU, alongV;
    CS_CHECK("SmApiEvaluateSurfaceDerivatives", SmApiEvaluateSurfaceDerivatives(asSurface(value), SmVector2d(u, v), position, alongU, alongV));
    put(position, result);
    put(alongU, result + 3);
    put(alongV, result + 6);
    return 0;
    CS_CATCH("SmApiEvaluateSurfaceDerivatives")
}

int cs_surface_closest_point(const CsSurface* value, const double target[3], double closest[3], double uv[2], int* hasUv, double* distance)
{
    CS_TRY
    SmPoint3d result;
    SmTArray<SmPoint2d> uvs;
    double gap = 0.0;
    CS_CHECK("SmApiSurfaceClosestPoint", SmApiSurfaceClosestPoint(asSurface(value), asPoint(target), result, uvs, gap));
    put(result, closest);
    *distance = gap;
    *hasUv = uvs.GetSize() > 0 ? 1 : 0;
    if (uvs.GetSize() > 0)
    {
        uv[0] = uvs[0].x;
        uv[1] = uvs[0].y;
    }
    return 0;
    CS_CATCH("SmApiSurfaceClosestPoint")
}

void cs_surface_periodic(const CsSurface* value, int* inU, int* inV)
{
    try
    {
        SmSurface* target = asSurface(value);
        *inU = target->IsPeriodic(target->GetNaturalUVDomain(), SM_SP_U) ? 1 : 0;
        *inV = target->IsPeriodic(target->GetNaturalUVDomain(), SM_SP_V) ? 1 : 0;
    }
    catch (...)
    {
        *inU = 0;
        *inV = 0;
    }
}

int cs_surface_kind(const CsSurface* value)
{
    SmSurface* target = asSurface(value);
    if (dynamic_cast<SmPlane*>(target))
        return 1;
    if (dynamic_cast<SmSurfOfRevolution*>(target))
        return 9;
    if (dynamic_cast<SmSurfOfExtrusion*>(target))
        return 8;
    if (dynamic_cast<SmOffsetSurface*>(target))
        return 7;
    if (dynamic_cast<SmBSplineSurface*>(target))
        return 6;
    return 0;
}

// ---- edges and vertices ----

int cs_edge_bounding_box(const CsEdge* value, int tight, double box[6])
{
    CS_TRY
    SmPoint3d low, high;
    CS_CHECK("SmApiEdgeBoundingBox", SmApiEdgeBoundingBox(asEdge(value), tight ? TRUE : FALSE, low, high));
    putBox(low, high, box);
    return 0;
    CS_CATCH("SmApiEdgeBoundingBox")
}

int cs_edge_length(const CsEdge* value, double accuracy, double* length)
{
    CS_TRY
    double result = 0.0;
    CS_CHECK("SmApiEdgeComputeLength", SmApiEdgeComputeLength(asEdge(value), accuracy, result));
    *length = result;
    return 0;
    CS_CATCH("SmApiEdgeComputeLength")
}

int cs_edge_parameter_range(const CsEdge* value, double range[2])
{
    CS_TRY
    double low = 0.0, high = 0.0;
    CS_CHECK("SmApiEdgeParameterRange", SmApiEdgeParameterRange(asEdge(value), low, high));
    range[0] = low;
    range[1] = high;
    return 0;
    CS_CATCH("SmApiEdgeParameterRange")
}

int cs_edge_point_at(const CsEdge* value, double t, double out[3])
{
    CS_TRY
    SmVector3d storage;
    SmVector3d* position = &storage;
    SmVector3d* first = nullptr;
    SmVector3d* second = nullptr;
    CS_CHECK("SmApiEvaluateCurve", SmApiEvaluateCurve(asEdge(value)->GetCurve(), t, position, first, second));
    put(storage, out);
    return 0;
    CS_CATCH("SmApiEvaluateCurve")
}

int cs_edge_tangent_at(const CsEdge* value, double t, double out[3])
{
    CS_TRY
    SmVector3d tangent;
    CS_CHECK("SmApiEdgeTangent", SmApiEdgeTangent(asEdge(value), t, tangent));
    put(tangent, out);
    return 0;
    CS_CATCH("SmApiEdgeTangent")
}

int cs_edge_closest_point(const CsEdge* value, const double target[3], double closest[3], double* t, double* distance)
{
    CS_TRY
    SmPoint3d result;
    double parameter = 0.0, gap = 0.0;
    CS_CHECK("SmApiEdgeClosestPoint", SmApiEdgeClosestPoint(asEdge(value), asPoint(target), result, parameter, gap));
    put(result, closest);
    *t = parameter;
    *distance = gap;
    return 0;
    CS_CATCH("SmApiEdgeClosestPoint")
}

int cs_edge_classify_parameter(const CsEdge* value, double t, int* where)
{
    CS_TRY
    int classification = 0;
    CS_CHECK("SmApiEdgeClassifyParameter", SmApiEdgeClassifyParameter(asEdge(value), t, classification));
    switch (classification)
    {
        case SM_PC_EDGE: *where = 0; break;
        case SM_PC_VERTEX: *where = 1; break;
        case SM_PC_UNKNOWN: *where = 2; break;
        default: return failWith("SmApiEdgeClassifyParameter", "unexpected parameter classification");
    }
    return 0;
    CS_CATCH("SmApiEdgeClassifyParameter")
}

size_t cs_edge_faces(const CsEdge* value, CsFace** faces, size_t capacity)
{
    SmTArray<SmFace*> items;
    asEdge(value)->GetFaces(items);
    return copyList(items, faces, capacity);
}

void cs_edge_vertices(const CsEdge* value, CsVertex** start, CsVertex** end)
{
    SmVertex* first = nullptr;
    SmVertex* last = nullptr;
    asEdge(value)->GetVertices(first, last);
    *start = reinterpret_cast<CsVertex*>(first);
    *end = reinterpret_cast<CsVertex*>(last);
}

const CsCurve* cs_edge_curve(const CsEdge* value) { return reinterpret_cast<const CsCurve*>(asEdge(value)->GetCurve()); }

int cs_vertex_point(const CsVertex* value, double out[3])
{
    CS_TRY
    SmPoint3d result;
    CS_CHECK("SmApiVertexGetPoint", SmApiVertexGetPoint(asVertex(value), result));
    put(result, out);
    return 0;
    CS_CATCH("SmApiVertexGetPoint")
}

size_t cs_vertex_edges(const CsVertex* value, CsEdge** edges, size_t capacity)
{
    SmTArray<SmEdge*> items;
    asVertex(value)->GetEdges(items);
    return copyList(items, edges, capacity);
}

size_t cs_vertex_faces(const CsVertex* value, CsFace** faces, size_t capacity)
{
    SmTArray<SmFace*> items;
    asVertex(value)->GetFaces(items);
    return copyList(items, faces, capacity);
}

// ---- curves ----

void cs_curve_free(CsCurve* value)
{
    try { delete asCurve(value); } catch (...) {}
}

int cs_curve_copy(const CsCurve* value, CsCurve** result)
{
    CS_TRY
    SmCurve* copy = nullptr;
    CS_CHECK("SmCurve::Copy", asCurve(value)->Copy(*SmApiGetOrCreateContext(), copy));
    *result = reinterpret_cast<CsCurve*>(copy);
    return 0;
    CS_CATCH("SmCurve::Copy")
}

int cs_curve_kind(const CsCurve* value)
{
    SmCurve* target = asCurve(value);
    if (dynamic_cast<SmLine*>(target))
        return 2;
    if (dynamic_cast<SmCircle*>(target))
        return 3;
    if (dynamic_cast<SmEllipse*>(target))
        return 4;
    if (dynamic_cast<SmBSplineCurve*>(target))
        return 1;
    return 0;
}

int cs_circle_frame(const CsCurve* value, double frame[10])
{
    CS_TRY
    SmCircle* circle = dynamic_cast<SmCircle*>(asCurve(value));
    if (!circle)
        return failWith("SmCircle::GetCanonical", "not a circle");
    SmAxis2Placement placement;
    double radius = 0.0;
    CS_CHECK("SmCircle::GetCanonical", circle->GetCanonical(placement, radius));
    put(placement.GetOrigin(), frame);
    put(placement.GetZAxis(), frame + 3);
    put(placement.GetXAxis(), frame + 6);
    frame[9] = circle->GetRadius();
    return 0;
    CS_CATCH("SmCircle::GetCanonical")
}

void cs_curve_parameter_range(const CsCurve* value, double range[2])
{
    const SmExtent1d interval = asCurve(value)->GetNaturalInterval();
    range[0] = interval.GetMin();
    range[1] = interval.GetMax();
}

int cs_curve_point(const CsCurve* value, double t, double out[3])
{
    CS_TRY
    SmPoint3d result;
    CS_CHECK("SmCurve::EvaluatePoint", asCurve(value)->EvaluatePoint(t, result));
    put(result, out);
    return 0;
    CS_CATCH("SmCurve::EvaluatePoint")
}

int cs_curve_derivatives(const CsCurve* value, double t, double position[3], double first[3], double second[3])
{
    CS_TRY
    SmVector3d storedPoint, storedFirst, storedSecond;
    SmVector3d* pointer = &storedPoint;
    SmVector3d* firstPointer = &storedFirst;
    SmVector3d* secondPointer = &storedSecond;
    CS_CHECK("SmApiEvaluateCurve", SmApiEvaluateCurve(asCurve(value), t, pointer, firstPointer, secondPointer));
    put(storedPoint, position);
    put(storedFirst, first);
    put(storedSecond, second);
    return 0;
    CS_CATCH("SmApiEvaluateCurve")
}

int cs_curve_length(const CsCurve* value, double accuracy, double* length)
{
    CS_TRY
    double result = 0.0;
    CS_CHECK("SmCurve::Length", asCurve(value)->Length(asCurve(value)->GetNaturalInterval(), accuracy, result));
    *length = result;
    return 0;
    CS_CATCH("SmCurve::Length")
}

int cs_curve_is_closed(const CsCurve* value, double tolerance)
{
    try { return asCurve(value)->IsClosed(asCurve(value)->GetNaturalInterval(), tolerance) ? 1 : 0; }
    catch (...) { return 0; }
}

int cs_curve_is_planar(const CsCurve* value, double tolerance)
{
    try { return asCurve(value)->IsPlanar(tolerance > 0 ? tolerance : SM_EFF_ZERO) ? 1 : 0; }
    catch (...) { return 0; }
}

int cs_curve_nurbs(const CsCurve* value, double* points, size_t pointCapacity, size_t* pointCount,
                   double* knots, size_t knotCapacity, size_t* knotCount, uint32_t* degree)
{
    CS_TRY
    SmBSplineCurve* spline = dynamic_cast<SmBSplineCurve*>(asCurve(value));
    if (!spline)
        return failWith("SmBSplineCurve", "not a b-spline");
    ULONG controlCount = 0;
    double* raw = nullptr;
    CS_CHECK("SmBSplineCurve::GetControlPointsPointer", spline->GetControlPointsPointer(controlCount, raw));
    *pointCount = controlCount;
    for (ULONG index = 0; index < controlCount && index < pointCapacity; ++index)
    {
        SmPoint3d control;
        double weight = 1.0;
        CS_CHECK("SmBSplineCurve::GetControlPoint", spline->GetControlPoint(SM_CP_EUCLIDIAN_RATIONAL, index, control, weight));
        points[4 * index] = control.x;
        points[4 * index + 1] = control.y;
        points[4 * index + 2] = control.z;
        points[4 * index + 3] = weight;
    }
    SmTArray<double> allKnots;
    CS_CHECK("SmBSplineCurve::GetKnotsAll", spline->GetKnotsAll(allKnots));
    *knotCount = allKnots.GetSize();
    for (ULONG index = 0; index < allKnots.GetSize() && index < knotCapacity; ++index)
        knots[index] = allKnots[index];
    *degree = static_cast<uint32_t>(spline->GetDegree());
    return 0;
    CS_CATCH("SmBSplineCurve")
}

int cs_create_line_segment(const double start[3], const double end[3], CsCurve** result)
{
    CS_TRY
    SmLine* line = nullptr;
    CS_CHECK("SmApiCreateLineSegment", SmApiCreateLineSegment(asPoint(start), asPoint(end), line));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(line));
    return 0;
    CS_CATCH("SmApiCreateLineSegment")
}

int cs_create_arc(const double center[3], double radius, double startDeg, double endDeg, CsCurve** result)
{
    CS_TRY
    SmBSplineCurve* arc = nullptr;
    CS_CHECK("SmApiCreateArc", SmApiCreateArc(asPoint(center), radius, startDeg, endDeg, arc));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(arc));
    return 0;
    CS_CATCH("SmApiCreateArc")
}

int cs_create_circle(const double center[3], double radius, CsCurve** result)
{
    CS_TRY
    SmCircle* circle = nullptr;
    CS_CHECK("SmApiCreateCircle", SmApiCreateCircle(asPoint(center), radius, circle));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(circle));
    return 0;
    CS_CATCH("SmApiCreateCircle")
}

int cs_create_ellipse(const double center[3], double radius1, double radius2, CsCurve** result)
{
    CS_TRY
    SmEllipse* ellipse = nullptr;
    CS_CHECK("SmApiCreateEllipse", SmApiCreateEllipse(asPoint(center), radius1, radius2, ellipse));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(ellipse));
    return 0;
    CS_CATCH("SmApiCreateEllipse")
}

int cs_create_canonical_curve(const double* points, size_t pointCount, const double* knots, const uint32_t* multiplicities,
                              size_t knotCount, const double* weights, uint32_t degree, CsCurve** result)
{
    CS_TRY
    SmTArray<SmPoint3d> controls;
    for (size_t index = 0; index < pointCount; ++index)
        controls.Add(asPoint(points + 3 * index));
    SmTArray<double> knotValues;
    SmTArray<ULONG> knotMultiplicities;
    for (size_t index = 0; index < knotCount; ++index)
    {
        knotValues.Add(knots[index]);
        knotMultiplicities.Add(static_cast<ULONG>(multiplicities[index]));
    }
    SmTArray<double> weightValues;
    if (weights)
        for (size_t index = 0; index < pointCount; ++index)
            weightValues.Add(weights[index]);
    SmBSplineCurve* spline = nullptr;
    CS_CHECK("SmApiCreateCanonicalCurve", SmApiCreateCanonicalCurve(controls, knotValues, knotMultiplicities, weights ? &weightValues : nullptr, degree, SM_CF_UNSPECIFIED, spline));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(spline));
    return 0;
    CS_CATCH("SmApiCreateCanonicalCurve")
}

int cs_create_curve_interp(const double* points, size_t pointCount, int parameterization, CsCurve** result)
{
    CS_TRY
    SmTArray<SmPoint3d> through;
    for (size_t index = 0; index < pointCount; ++index)
        through.Add(asPoint(points + 3 * index));
    SmCurve* fitted = nullptr;
    CS_CHECK("SmApiCreateCurveInterpPoints", SmApiCreateCurveInterpPoints(through, fitted, static_cast<SmCurveParameterizationType>(parameterization)));
    *result = reinterpret_cast<CsCurve*>(fitted);
    return 0;
    CS_CATCH("SmApiCreateCurveInterpPoints")
}

int cs_create_helix(const double origin[3], double height, double radiusStart, double radiusEnd, double turns,
                    int rightHanded, double tolerance, CsCurve** result)
{
    CS_TRY
    SmBSplineCurve* helix = nullptr;
    CS_CHECK("SmApiCreateHelix", SmApiCreateHelix(asPoint(origin), height, radiusStart, radiusEnd, turns, rightHanded ? TRUE : FALSE, tolerance, helix));
    *result = reinterpret_cast<CsCurve*>(static_cast<SmCurve*>(helix));
    return 0;
    CS_CATCH("SmApiCreateHelix")
}

// ---- solids ----

#define CS_SOLID(entry, call)                                               \
    CS_TRY                                                                  \
    SmBrep* made = nullptr;                                                 \
    CS_CHECK(entry, call);                                                  \
    *result = reinterpret_cast<CsBrep*>(made);                              \
    return 0;                                                               \
    CS_CATCH(entry)

int cs_create_box(const double origin[3], double length, double width, double height, CsBrep** result)
{
    CS_SOLID("SmApiCreateBox", SmApiCreateBox(asVec(origin), length, width, height, made))
}

int cs_create_cylinder(const double origin[3], double radius, double height, CsBrep** result)
{
    CS_SOLID("SmApiCreateCylinder", SmApiCreateCylinder(asVec(origin), radius, height, made))
}

int cs_create_cone(const double origin[3], double radiusBase, double radiusTop, double height, CsBrep** result)
{
    CS_SOLID("SmApiCreateCone", SmApiCreateCone(asVec(origin), radiusBase, radiusTop, height, made))
}

int cs_create_sphere(const double origin[3], double radius, CsBrep** result)
{
    CS_SOLID("SmApiCreateSphere", SmApiCreateSphere(asVec(origin), radius, made))
}

int cs_create_torus(const double origin[3], double radiusMajor, double radiusMinor, CsBrep** result)
{
    CS_SOLID("SmApiCreateTorus", SmApiCreateTorus(asVec(origin), radiusMajor, radiusMinor, made))
}

int cs_create_partial_cylinder(const double origin[3], double radius, double height, double startDeg, double endDeg, CsBrep** result)
{
    CS_SOLID("SmApiCreatePartialCylinder", SmApiCreatePartialCylinder(asVec(origin), radius, height, startDeg, endDeg, made))
}

int cs_create_partial_cone(const double origin[3], double radiusBase, double radiusTop, double height, double startDeg, double endDeg, CsBrep** result)
{
    CS_SOLID("SmApiCreatePartialCone", SmApiCreatePartialCone(asVec(origin), radiusBase, radiusTop, height, startDeg, endDeg, made))
}

int cs_create_partial_sphere(const double origin[3], double radius, double startDeg, double endDeg, CsBrep** result)
{
    CS_SOLID("SmApiCreatePartialSphere", SmApiCreatePartialSphere(asVec(origin), radius, startDeg, endDeg, made))
}

int cs_create_partial_torus(const double origin[3], double radiusMajor, double radiusMinor, double startDeg, double endDeg, CsBrep** result)
{
    CS_SOLID("SmApiCreatePartialTorus", SmApiCreatePartialTorus(asVec(origin), radiusMajor, radiusMinor, startDeg, endDeg, made))
}

static SmTArray<SmCurve*> curveList(CsCurve* const* curves, size_t count)
{
    SmTArray<SmCurve*> list;
    for (size_t index = 0; index < count; ++index)
        list.Add(asCurve(curves[index]));
    return list;
}

int cs_linear_sweep(CsCurve* const* curves, size_t count, const double direction[3], double distance, int cap, CsBrep** result)
{
    const SmTArray<SmCurve*> list = curveList(curves, count);
    CS_SOLID("SmApiCreateLinearSweep", SmApiCreateLinearSweep(list, asVec(direction), distance, cap ? TRUE : FALSE, made))
}

int cs_rotational_sweep(CsCurve* const* curves, size_t count, const double base[3], const double axis[3], double angleDeg, int cap, CsBrep** result)
{
    const SmTArray<SmCurve*> list = curveList(curves, count);
    CS_SOLID("SmApiCreateRotationalSweep", SmApiCreateRotationalSweep(list, asPoint(base), asVec(axis), angleDeg, cap ? TRUE : FALSE, made))
}

int cs_pipe_sweep(const CsCurve* path, double radius, int cap, CsBrep** result)
{
    SmBSplineCurve* spline = dynamic_cast<SmBSplineCurve*>(asCurve(path));
    if (!spline)
        return failWith("SmApiCreatePipeSweep", "path must be a B-spline curve");
    CS_SOLID("SmApiCreatePipeSweep", SmApiCreatePipeSweep(radius, spline, cap ? TRUE : FALSE, made))
}

int cs_create_planar_faces(CsCurve* const* curves, size_t count, CsBrep** result)
{
    const SmTArray<SmCurve*> list = curveList(curves, count);
    CS_SOLID("SmApiCreatePlanarFaces", SmApiCreatePlanarFaces(list, made))
}

int cs_taper_extrude(CsCurve* const* curves, size_t count, double height, double draftDeg, int capEnds, int filletCorner, CsBrep** result)
{
    const SmTArray<SmCurve*> list = curveList(curves, count);
    CS_SOLID("SmApiCreateTaperExtrude", SmApiCreateTaperExtrude(list, height, draftDeg, capEnds, filletCorner ? TRUE : FALSE, made))
}

int cs_skin(const uint32_t* curvesPerProfile, size_t profileCount, CsCurve* const* curves, size_t curveCount, uint32_t degree, int cap, CsBrep** result)
{
    SmTArray<ULONG> perProfile;
    for (size_t index = 0; index < profileCount; ++index)
        perProfile.Add(static_cast<ULONG>(curvesPerProfile[index]));
    SmTArray<const SmCurve*> list;
    for (size_t index = 0; index < curveCount; ++index)
        list.Add(asCurve(curves[index]));
    CS_SOLID("SmApiCreateSkinPrimitive", SmApiCreateSkinPrimitive(perProfile, list, degree, cap ? TRUE : FALSE, made, nullptr, nullptr, nullptr))
}

// ---- booleans ----

int cs_boolean(CsBrep* first, CsBrep* second, int operation, CsBrep** result)
{
    *result = nullptr;
    try
    {
        SmBrep* made = nullptr;
        clearTrail();
        SmStatus status = SM_ERR;
        switch (operation)
        {
            case 1: status = SmApiBooleanUnion(asBrep(first), asBrep(second), made); break;
            case 2: status = SmApiBooleanIntersection(asBrep(first), asBrep(second), made); break;
            case 3: status = SmApiBooleanDifference(asBrep(first), asBrep(second), made); break;
            default: status = SmApiBoolean(asBrep(first), asBrep(second), static_cast<SmBooleanOperationType>(operation), made); break;
        }
        const char* entry = operation == 1 ? "SmApiBooleanUnion" : operation == 2 ? "SmApiBooleanIntersection" : operation == 3 ? "SmApiBooleanDifference" : "SmApiBoolean";
        if (status != SM_SUCCESS)
        {
            const int code = fail(entry, status);
            delete asBrep(first);
            delete asBrep(second);
            return code;
        }
        *result = reinterpret_cast<CsBrep*>(made);
        return 0;
    }
    catch (const std::exception& exception) { return failWith("SmApiBoolean", exception.what()); }
    catch (...) { return failWith("SmApiBoolean", "unknown C++ exception"); }
}

int cs_create_offset_profile(CsCurve* const* curves, size_t count, double distance, int offsetType, int roundCorners, CsBrep** result)
{
    *result = nullptr;
    CS_TRY
    SmTArray<SmCurve*> list;
    for (size_t index = 0; index < count; ++index)
        list.Add(asCurve(curves[index]));
    SmBrep* made = nullptr;
    clearTrail();
    // 1 left, 2 right, 3 a band on both sides of the curves
    CS_CHECK("SmApiCreateOffsetProfile", SmApiCreateOffsetProfile(list, distance, static_cast<unsigned long>(offsetType), roundCorners ? TRUE : FALSE, FALSE, made));
    *result = reinterpret_cast<CsBrep*>(made);
    return 0;
    CS_CATCH("SmApiCreateOffsetProfile")
}

int cs_boolean_imprint(CsBrep* first, CsBrep* second, CsBrep** result)
{
    *result = nullptr;
    try
    {
        SmBrep* made = nullptr;
        clearTrail();
        // a union that splits the faces where the tool touches them (imprinting), the tool used up
        SmStatus status = SmApiBooleanWithOptions(asBrep(first), asBrep(second), static_cast<SmBooleanOperationType>(1), FALSE, TRUE, TRUE, FALSE, FALSE, made);
        if (status != SM_SUCCESS)
        {
            const int code = fail("SmApiBooleanWithOptions", status);
            delete asBrep(first);
            delete asBrep(second);
            return code;
        }
        *result = reinterpret_cast<CsBrep*>(made);
        return 0;
    }
    catch (const std::exception& exception) { return failWith("SmApiBooleanWithOptions", exception.what()); }
    catch (...) { return failWith("SmApiBooleanWithOptions", "unknown C++ exception"); }
}

int cs_create_cylindrical_box(const double origin[3], double length, double insideRadius, double outsideRadius, double startDeg, double endDeg, CsBrep** result)
{
    CS_SOLID("SmApiCreateCylindricalBox", SmApiCreateCylindricalBox(asVec(origin), length, insideRadius, outsideRadius, startDeg, endDeg, made))
}

int cs_merge_breps(CsBrep* const* breps, size_t count, int operation, CsBrep** result, int* consumed)
{
    *result = nullptr;
    *consumed = 0;
    CS_TRY
    SmTArray<SmBrep*> list;
    for (size_t index = 0; index < count; ++index)
        list.Add(asBrep(breps[index]));
    SmBrep* made = nullptr;
    clearTrail();
    const SmStatus status = SmApiMergeBreps(list, static_cast<SmBooleanOperationType>(operation), made);
    // validated input is consumed, also when the merge then fails
    *consumed = status == SM_ERR_INVALID_INPUT ? 0 : 1;
    if (status != SM_SUCCESS)
        return fail("SmApiMergeBreps", status);
    *result = reinterpret_cast<CsBrep*>(made);
    return 0;
    CS_CATCH("SmApiMergeBreps")
}

// ---- rounds and shells ----

static SmTArray<SmEdge*> edgeList(CsEdge* const* edges, size_t count)
{
    SmTArray<SmEdge*> list;
    for (size_t index = 0; index < count; ++index)
        list.Add(asEdge(edges[index]));
    return list;
}

int cs_fillet_edges(CsBrep* value, CsEdge* const* edges, size_t count, double radius, uint32_t crossSection, uint32_t continuity, double thumbweight)
{
    CS_TRY
    SmTArray<SmEdge*> list = edgeList(edges, count);
    CS_CHECK("SmApiFilletEdges", SmApiFilletEdges(asBrep(value), list, radius, crossSection, continuity, thumbweight));
    return 0;
    CS_CATCH("SmApiFilletEdges")
}

int cs_fillet_edges_per_edge(CsBrep* value, CsEdge* const* edges, size_t count, const double* radii, const int32_t* crossSections, uint32_t continuity, double thumbweight)
{
    CS_TRY
    SmTArray<SmEdge*> list = edgeList(edges, count);
    SmTArray<double> radiusList;
    SmTArray<SmFilletSurfaceGeneratorType> sectionList;
    for (size_t index = 0; index < count; ++index)
    {
        radiusList.Add(radii[index]);
        sectionList.Add(static_cast<SmFilletSurfaceGeneratorType>(crossSections[index]));
    }
    CS_CHECK("SmApiFilletEdgesPerEdge", SmApiFilletEdgesPerEdge(asBrep(value), list, radiusList, sectionList, continuity, thumbweight));
    return 0;
    CS_CATCH("SmApiFilletEdgesPerEdge")
}

int cs_variable_radius_fillet(CsBrep* value, CsEdge* const* edges, size_t count, double startRadius, double endRadius, uint32_t crossSection, uint32_t continuity)
{
    CS_TRY
    SmTArray<SmEdge*> list = edgeList(edges, count);
    CS_CHECK("SmApiVariableRadiusFillet", SmApiVariableRadiusFillet(asBrep(value), list, startRadius, endRadius, crossSection, continuity));
    return 0;
    CS_CATCH("SmApiVariableRadiusFillet")
}

int cs_chamfer_two_distances(CsBrep* value, CsEdge* const* edges, CsFace* const* referenceFaces, size_t count, const double* first, const double* second)
{
    CS_TRY
    SmTArray<SmEdge*> list = edgeList(edges, count);
    SmTArray<SmFace*> faces;
    SmTArray<double> firstDistances, secondDistances;
    for (size_t index = 0; index < count; ++index)
    {
        faces.Add(asFace(referenceFaces[index]));
        firstDistances.Add(first[index]);
        secondDistances.Add(second[index]);
    }
    CS_CHECK("SmApiChamferEdgesTwoDistances", SmApiChamferEdgesTwoDistances(asBrep(value), list, faces, firstDistances, secondDistances));
    return 0;
    CS_CATCH("SmApiChamferEdgesTwoDistances")
}

int cs_shell(const CsBrep* value, double distance, int extended, int selfIntersection, int createSolid, CsFace* const* faces, size_t faceCount, CsBrep** result)
{
    SmTArray<const SmFace*> open;
    for (size_t index = 0; index < faceCount; ++index)
        open.Add(asFace(faces[index]));
    CS_SOLID("SmApiShellBrepFull", SmApiShellBrepFull(asBrep(value), distance, extended ? TRUE : FALSE, selfIntersection ? TRUE : FALSE, createSolid ? TRUE : FALSE, open, made))
}

int cs_offset(const CsBrep* value, double distance, int extended, int selfIntersection, CsBrep** result)
{
    CS_SOLID("SmApiOffsetBrepFull", SmApiOffsetBrepFull(asMutableBrep(value), distance, extended ? TRUE : FALSE, selfIntersection ? TRUE : FALSE, made))
}

int cs_cut(CsBrep* value, const double planePoint[3], const double planeNormal[3])
{
    CS_TRY
    CS_CHECK("SmApiCut", SmApiCut(asBrep(value), asVec(planePoint), asVec(planeNormal)));
    return 0;
    CS_CATCH("SmApiCut")
}

int cs_turn_to_nurbs(CsBrep* value)
{
    CS_TRY
    CS_CHECK("SmApiTurnToNurbs", SmApiTurnToNurbs(asBrep(value)));
    return 0;
    CS_CATCH("SmApiTurnToNurbs")
}

int cs_squeeze_face(CsBrep* value, CsFace* target, double tolerance)
{
    CS_TRY
    CS_CHECK("SmApiSqueezeFace", SmApiSqueezeFace(asBrep(value), asFace(target), tolerance));
    return 0;
    CS_CATCH("SmApiSqueezeFace")
}

int cs_sweep_along_planar_path(CsCurve* const* profile, size_t profileCount, CsCurve* const* path, size_t pathCount, int moveProfile, int capEnds, CsBrep** result)
{
    SmTArray<SmCurve*> profileList = curveList(profile, profileCount);
    SmTArray<SmCurve*> pathList = curveList(path, pathCount);
    CS_SOLID("SmApiCreateSweepAlongPlanarPath", SmApiCreateSweepAlongPlanarPath(profileList, pathList, moveProfile ? TRUE : FALSE, capEnds, made))
}

// ---- moves ----

int cs_translate(void* value, int kind, const double offset[3])
{
    CS_TRY
    CS_CHECK("SmApiTranslate", SmApiTranslate(asObject(value, kind), asVec(offset)));
    return 0;
    CS_CATCH("SmApiTranslate")
}

int cs_rotate(void* value, int kind, const double pivot[3], const double axis[3], double angleDeg)
{
    CS_TRY
    CS_CHECK("SmApiRotate", SmApiRotate(asObject(value, kind), asVec(pivot), asVec(axis), angleDeg));
    return 0;
    CS_CATCH("SmApiRotate")
}

int cs_scale(void* value, int kind, const double factors[3])
{
    CS_TRY
    CS_CHECK("SmApiScale", SmApiScale(asObject(value, kind), asVec(factors)));
    return 0;
    CS_CATCH("SmApiScale")
}

int cs_scale_about(void* value, int kind, const double pivot[3], const double factors[3])
{
    CS_TRY
    CS_CHECK("SmApiScaleByPt", SmApiScaleByPt(asObject(value, kind), asVec(pivot), asVec(factors)));
    return 0;
    CS_CATCH("SmApiScaleByPt")
}

int cs_transform(void* value, int kind, const double origin[3], const double xAxis[3], const double yAxis[3])
{
    CS_TRY
    const SmAxis2Placement placement(asPoint(origin), asVec(xAxis), asVec(yAxis));
    CS_CHECK("SmApiTransform", SmApiTransform(asObject(value, kind), placement));
    return 0;
    CS_CATCH("SmApiTransform")
}

// ---- tessellation ----

namespace
{

SmVector3d unitNormal(const SmVector3d& normal, const SmVector3d& fallback)
{
    const double length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length <= 1e-30)
        return fallback;
    return normal / length;
}

// the binding's vertex_normal: the normal the point has on the polygon's own face
SmVector3d vertexNormal(SmPolyVertex* corner, SmFace* original)
{
    const auto& normals = corner->GetNormalsRef();
    const auto& faces = corner->GetFacesRef();
    SmVector3d average(0, 0, 0);
    for (ULONG index = 0; index < normals.GetSize(); ++index)
        average += normals[index];
    average = unitNormal(average, SmVector3d(0, 0, 1));
    if (original)
        for (ULONG index = 0; index < faces.GetSize() && index < normals.GetSize(); ++index)
            if (faces[index] == static_cast<SmTopology*>(original))
                return unitNormal(normals[index], average);
    return average;
}

// the binding's extract_mesh_arrays with face_varying_normals and source
void meshArrays(SmPolyBrep& poly, SmBrep* source, CsMesh& mesh)
{
    std::unordered_map<const SmFace*, int32_t> faceIndex;
    SmTArray<SmFace*> sourceFaces;
    source->GetFaces(sourceFaces);
    faceIndex.reserve(sourceFaces.GetSize());
    for (ULONG index = 0; index < sourceFaces.GetSize(); ++index)
        faceIndex[sourceFaces[index]] = static_cast<int32_t>(index);

    SmTArray<SmPolyVertex*> polyVertices;
    SmTArray<SmPolyFace*> polyFaces;
    poly.GetPolyVertices(polyVertices);
    poly.GetPolyFaces(polyFaces);
    const ULONG vertexCount = polyVertices.GetSize();
    const ULONG faceCount = polyFaces.GetSize();
    mesh.points.reserve(static_cast<size_t>(vertexCount) * 3);
    mesh.normals.reserve(static_cast<size_t>(faceCount) * 9);
    mesh.stream.reserve(static_cast<size_t>(faceCount) * 4);
    mesh.faceIds.reserve(faceCount);

    std::unordered_map<SmPolyVertex*, ULONG> vertexIndex;
    vertexIndex.reserve(vertexCount);
    for (ULONG index = 0; index < vertexCount; ++index)
    {
        SmPolyVertex* corner = polyVertices[index];
        vertexIndex[corner] = index;
        const SmPoint3d& position = corner->GetPoint();
        mesh.points.push_back(static_cast<float>(position.x));
        mesh.points.push_back(static_cast<float>(position.y));
        mesh.points.push_back(static_cast<float>(position.z));
    }
    std::vector<int32_t> corners;
    for (ULONG index = 0; index < faceCount; ++index)
    {
        SmPolyFace* polygon = polyFaces[index];
        SmPolyLoop* loop = polygon->GetOuterPolyLoop();
        if (!loop)
            continue;
        SmTArray<SmPolyEdge*> loopEdges;
        loop->GetPolyEdges(loopEdges);
        const ULONG edgeCount = loopEdges.GetSize();
        if (edgeCount == 0)
            continue;
        corners.clear();
        bool missing = false;
        for (ULONG corner = 0; corner < edgeCount; ++corner)
        {
            const auto found = vertexIndex.find(loopEdges[corner]->GetStartPolyVertex());
            if (found == vertexIndex.end())
            {
                missing = true;
                break;
            }
            corners.push_back(static_cast<int32_t>(found->second));
        }
        if (missing)
            continue;
        mesh.stream.push_back(static_cast<int32_t>(edgeCount));
        mesh.stream.insert(mesh.stream.end(), corners.begin(), corners.end());
        const auto owner = faceIndex.find(polygon->GetOriginalFace());
        mesh.faceIds.push_back(owner == faceIndex.end() ? -1 : owner->second);
        for (ULONG corner = 0; corner < edgeCount; ++corner)
        {
            const SmVector3d normal = vertexNormal(loopEdges[corner]->GetStartPolyVertex(), polygon->GetOriginalFace());
            mesh.normals.push_back(static_cast<float>(normal.x));
            mesh.normals.push_back(static_cast<float>(normal.y));
            mesh.normals.push_back(static_cast<float>(normal.z));
        }
    }
}

} // namespace

int cs_tessellate(const CsBrep* value, double chord, double curveAngleDeg, double surfaceAngleDeg, double maxEdge,
                  double maxAspect, int allowPartial, CsMesh** result, size_t* failedFaces)
{
    *result = nullptr;
    *failedFaces = 0;
    CS_TRY
    SmTessellationParams params;
    params.dChordHeightTol = chord;
    params.dCurveAngleTolDeg = curveAngleDeg;
    params.dSurfaceAngleTolDeg = surfaceAngleDeg;
    params.dMaxEdgeLength = maxEdge;
    params.dMaxAspectRatio = maxAspect;
    SmTessellationReport report;
    SmPolyBrep* poly = nullptr;
    CS_CHECK("SmApiTessellate", SmApiTessellate(asMutableBrep(value), poly, params, allowPartial ? TRUE : FALSE, nullptr, &report));
    *failedFaces = report.failures.GetSize();
    CsMesh* mesh = new CsMesh();
    try
    {
        meshArrays(*poly, asMutableBrep(value), *mesh);
    }
    catch (...)
    {
        delete mesh;
        delete poly;
        throw;
    }
    delete poly;
    *result = mesh;
    return 0;
    CS_CATCH("SmApiTessellate")
}

void cs_mesh_free(CsMesh* mesh) { delete mesh; }

size_t cs_mesh_points(const CsMesh* mesh, const float** points)
{
    *points = mesh->points.data();
    return mesh->points.size();
}

size_t cs_mesh_normals(const CsMesh* mesh, const float** normals)
{
    *normals = mesh->normals.data();
    return mesh->normals.size();
}

size_t cs_mesh_stream(const CsMesh* mesh, const int32_t** stream)
{
    *stream = mesh->stream.data();
    return mesh->stream.size();
}

size_t cs_mesh_face_ids(const CsMesh* mesh, const int32_t** faceIds)
{
    *faceIds = mesh->faceIds.data();
    return mesh->faceIds.size();
}

int cs_tessellate_boundaries(const CsBrep* value, double angleDeg, int allowPartial, CsLines** result)
{
    *result = nullptr;
    CS_TRY
    SmTArray<SmPoint3d> points;
    SmTArray<ULONG> counts;
    SmTArray<SmEdge*> edges;
    clearTrail();
    const SmStatus status = SmApiTessellateBoundaries(asMutableBrep(value), angleDeg, points, counts, edges);
    const SmStatus effective = allowPartial && status == SM_ERR && points.GetSize() > 0 ? SM_SUCCESS : status;
    if (effective != SM_SUCCESS)
        return fail("SmApiTessellateBoundaries", effective);
    CsLines* lines = new CsLines();
    lines->points.reserve(static_cast<size_t>(points.GetSize()) * 3);
    for (ULONG index = 0; index < points.GetSize(); ++index)
    {
        lines->points.push_back(points[index].x);
        lines->points.push_back(points[index].y);
        lines->points.push_back(points[index].z);
    }
    for (ULONG index = 0; index < counts.GetSize(); ++index)
        lines->counts.push_back(static_cast<uint32_t>(counts[index]));
    for (ULONG index = 0; index < edges.GetSize(); ++index)
        lines->edges.push_back(reinterpret_cast<CsEdge*>(edges[index]));
    *result = lines;
    return 0;
    CS_CATCH("SmApiTessellateBoundaries")
}

void cs_lines_free(CsLines* lines) { delete lines; }

size_t cs_lines_points(const CsLines* lines, const double** points)
{
    *points = lines->points.data();
    return lines->points.size();
}

size_t cs_lines_counts(const CsLines* lines, const uint32_t** counts)
{
    *counts = lines->counts.data();
    return lines->counts.size();
}

size_t cs_lines_edges(const CsLines* lines, CsEdge* const** edges)
{
    *edges = lines->edges.data();
    return lines->edges.size();
}

// ---- queries between bodies ----

int cs_brep_closest_point(const CsBrep* value, const double target[3], double closest[3], double* distance)
{
    CS_TRY
    SmPoint3d result;
    double gap = 0.0;
    CS_CHECK("SmApiBrepClosestPoint", SmApiBrepClosestPoint(asBrep(value), asPoint(target), result, gap));
    put(result, closest);
    *distance = gap;
    return 0;
    CS_CATCH("SmApiBrepClosestPoint")
}

int cs_brep_distance(const CsBrep* first, const CsBrep* second, double* distance)
{
    CS_TRY
    double gap = 0.0;
    CS_CHECK("SmApiBrepDistance", SmApiBrepDistance(asBrep(first), asBrep(second), gap));
    *distance = gap;
    return 0;
    CS_CATCH("SmApiBrepDistance")
}

int cs_brep_relationship(const CsBrep* first, const CsBrep* second, int* relationship, double* distance)
{
    CS_TRY
    int found = 0;
    double gap = 0.0;
    CS_CHECK("SmApiBrepRelationship", SmApiBrepRelationship(asBrep(first), asBrep(second), found, gap));
    *relationship = found;
    *distance = gap;
    return 0;
    CS_CATCH("SmApiBrepRelationship")
}

int cs_intersect_curve_brep(const CsCurve* value, const CsBrep* body, double* points, size_t capacity, size_t* count)
{
    CS_TRY
    SmTArray<SmCurve*> curves;
    SmTArray<SmPoint3d> hits;
    CS_CHECK("SmApiIntersectCurveBrep", SmApiIntersectCurveBrep(asCurve(value), asMutableBrep(body), curves, hits));
    for (ULONG index = 0; index < curves.GetSize(); ++index)
        delete curves[index];
    *count = hits.GetSize();
    for (ULONG index = 0; index < hits.GetSize() && index < capacity; ++index)
    {
        points[3 * index] = hits[index].x;
        points[3 * index + 1] = hits[index].y;
        points[3 * index + 2] = hits[index].z;
    }
    return 0;
    CS_CATCH("SmApiIntersectCurveBrep")
}

int cs_intersect_brep_with_plane(const CsBrep* value, const double planePoint[3], const double planeNormal[3], CsCurve** curves, size_t capacity, size_t* count)
{
    CS_TRY
    SmTArray<SmCurve*> section;
    CS_CHECK("SmApiIntersectBrepWithPlane", SmApiIntersectBrepWithPlane(asMutableBrep(value), asVec(planePoint), asVec(planeNormal), section));
    *count = section.GetSize();
    for (ULONG index = 0; index < section.GetSize(); ++index)
    {
        if (index < capacity)
            curves[index] = reinterpret_cast<CsCurve*>(section[index]);
        else
            delete section[index];
    }
    return 0;
    CS_CATCH("SmApiIntersectBrepWithPlane")
}

} // extern "C"

// ---- the exact data of a body as JSON (what usd-brep's USD export packs, SmuConvert.cpp) ----

namespace
{

void jsonNumber(std::string& out, double value)
{
    if (!std::isfinite(value))
    {
        out += "null";
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    out += buffer;
}

void jsonPoint(std::string& out, const SmPoint3d& point)
{
    out += '[';
    jsonNumber(out, point.x);
    out += ',';
    jsonNumber(out, point.y);
    out += ',';
    jsonNumber(out, point.z);
    out += ']';
}

void jsonVector(std::string& out, const SmVector3d& vector)
{
    jsonPoint(out, SmPoint3d(vector.x, vector.y, vector.z));
}

// [[x, y, z, w], ...], knots and degree of an NLIB curve
void jsonCurve(std::string& out, SmBSplineCurve& spline)
{
    gw_CURVE* nurb = spline.GetOrCreateGwNurbPointer();
    out += "{\"degree\":" + std::to_string(nurb->p) + ",\"points\":[";
    for (long index = 0; index <= nurb->pol->n; ++index)
    {
        const NL_CPOINT& control = nurb->pol->Pw[index];
        double weight = control.w != NL_NOW ? control.w : 1.0;
        if (index)
            out += ',';
        jsonPoint(out, SmPoint3d(control.x / weight, control.y / weight, control.z / weight));
        out.back() = ',';
        jsonNumber(out, weight);
        out += ']';
    }
    out += "],\"knots\":[";
    for (long index = 0; index <= nurb->knt->m; ++index)
    {
        if (index)
            out += ',';
        jsonNumber(out, nurb->knt->U[index]);
    }
    out += "]}";
}

void jsonSurface(std::string& out, SmBSplineSurface& spline)
{
    NL_SURFACE* surface = spline.GetOrCreateGwNurbPointer();
    NL_CNET* net = surface->net;
    out += "{\"kind\":\"nurbs\",\"degreeU\":" + std::to_string(surface->p) + ",\"degreeV\":" + std::to_string(surface->q);
    out += ",\"countU\":" + std::to_string(net->n + 1) + ",\"countV\":" + std::to_string(net->m + 1) + ",\"points\":[";
    for (long u = 0; u <= net->n; ++u)
        for (long v = 0; v <= net->m; ++v)
        {
            const NL_CPOINT& control = net->Pw[u][v];
            double weight = control.w != NL_NOW ? control.w : 1.0;
            if (u || v)
                out += ',';
            jsonPoint(out, SmPoint3d(control.x / weight, control.y / weight, control.z / weight));
            out.back() = ',';
            jsonNumber(out, weight);
            out += ']';
        }
    out += "],\"knotsU\":[";
    for (long index = 0; index <= surface->knu->m; ++index)
    {
        if (index)
            out += ',';
        jsonNumber(out, surface->knu->U[index]);
    }
    out += "],\"knotsV\":[";
    for (long index = 0; index <= surface->knv->m; ++index)
    {
        if (index)
            out += ',';
        jsonNumber(out, surface->knv->U[index]);
    }
    out += "]}";
}

void jsonPlacement(std::string& out, const char* originName, const SmAxis2Placement& placement)
{
    out += std::string("\"") + originName + "\":";
    jsonPoint(out, placement.GetOrigin());
    out += ",\"axis\":";
    jsonVector(out, placement.GetZAxis());
    out += ",\"refDirection\":";
    jsonVector(out, placement.GetXAxis());
}

double maximumZoneTolerance(const SmBrep* brep)
{
    double most = SmTol::GetZoneTol3d(brep);
    auto accumulate = [&most](auto& items) {
        for (ULONG index = 0; index < items.GetSize(); ++index)
        {
            const double tolerance = static_cast<double>(SmTol::GetZoneTol3d(items[index]));
            if (std::isfinite(tolerance) && tolerance > most)
                most = tolerance;
        }
    };
    SmTArray<SmFace*> faces;
    brep->GetFaces(faces);
    accumulate(faces);
    SmTArray<SmEdge*> edges;
    brep->GetEdges(edges);
    accumulate(edges);
    SmTArray<SmVertex*> vertices;
    brep->GetVertices(vertices);
    accumulate(vertices);
    return most;
}

// a sphere's, cylinder's, cone's or torus's face whose canonical (STEP) normal opposes the face's
// (SmuConvert's AnalyticUsdNormalOpposesFaceNormal)
bool analyticNormalOpposes(SmSurface* surface)
{
    if (surface == nullptr || !(surface->IsKindOf(SmSphere_TYPE) || surface->IsKindOf(SmCylinder_TYPE) || surface->IsKindOf(SmCone_TYPE) || surface->IsKindOf(SmTorus_TYPE)))
        return false;
    SmSurfOfRevolution* revolution = static_cast<SmSurfOfRevolution*>(surface);
    return (revolution->GetSwapUVBit() != FALSE) != (revolution->GetInsideOut() != FALSE);
}

} // namespace

extern "C" {

int cs_brep_exact(const CsBrep* value, char** text, size_t* length)
{
    CS_TRY
    // on a copy: the analytic faces whose canonical normal opposes theirs are swapped, as the
    // USD export does, so every face-use goes by its surface's own normal
    SmBrep* brep = nullptr;
    CS_CHECK("SmApiBrepCopy", SmApiBrepCopy(asBrep(value), brep));
    SmObjDelete cleanBrep(brep);
    {
        SmTArray<SmFace*> faces;
        brep->GetFaces(faces);
        std::unordered_map<const SmSurface*, ULONG> uses;
        for (ULONG index = 0; index < faces.GetSize(); ++index)
            ++uses[faces[index]->GetSurface()];
        for (ULONG index = 0; index < faces.GetSize(); ++index)
            if (analyticNormalOpposes(faces[index]->GetSurface()) && uses[faces[index]->GetSurface()] == 1)
                CS_CHECK("SmFace::SwapUV", faces[index]->SwapUV());
    }
    SmXSectTol3d crossing = SM_ZONE_TO_XSECTTOL3D(maximumZoneTolerance(brep));
    const SmApproxTol3d approximation = SmTol::GetApproxTol3d(crossing);
    SmBrepData* data = new (*brep->GetContext()) SmBrepData(FALSE, SM_DS_SMLIB);
    SmObjDelete cleanData(data);
    SmTArray<SmAttribute*> attributes;
    CS_CHECK("SmBrepData::FromBrep", data->FromBrep(*brep, attributes));
    std::string out = "{\"vertices\":[";
    for (ULONG index = 0; index < data->m_vVertices.GetSize(); ++index)
    {
        if (index)
            out += ',';
        jsonPoint(out, data->m_vVertices[index].m_vPoint);
    }
    out += "],\"edges\":[";
    for (ULONG index = 0; index < data->m_vEdges.GetSize(); ++index)
    {
        SmEdgeData& edge = data->m_vEdges[index];
        if (index)
            out += ',';
        out += "{\"start\":" + std::to_string(edge.m_lStartVertex) + ",\"end\":" + std::to_string(edge.m_lEndVertex) + ",\"interval\":[";
        jsonNumber(out, edge.m_vInterval.GetMin());
        out += ',';
        jsonNumber(out, edge.m_vInterval.GetMax());
        out += "],\"curve\":";
        SmCurve* curve = edge.m_pEdge != nullptr ? edge.m_pEdge->GetCurve() : nullptr;
        if (curve == nullptr)
        {
            out += "null}";
            continue;
        }
        SmBSplineCurve* spline = nullptr;
        SmObjDelete cleanSpline;
        if (curve->IsKindOf(SmBSplineCurve_TYPE))
            spline = SM_REINTERPRET_CAST(SmBSplineCurve*, curve);
        else
        {
            SmTArray<double> breaks;
            breaks.Add(edge.m_vInterval.GetMin());
            breaks.Add(edge.m_vInterval.GetMax());
            double achieved = 0.0;
            curve->ApproximateCurve(*curve->GetContext(), breaks, approximation, achieved, spline);
            cleanSpline.SetObj(spline);
        }
        if (spline == nullptr)
            out += "null";
        else
            jsonCurve(out, *spline);
        out += '}';
    }
    out += "],\"faces\":[";
    for (ULONG index = 0; index < data->m_vFaces.GetSize(); ++index)
    {
        SmFaceData& face = data->m_vFaces[index];
        if (index)
            out += ',';
        out += "{\"loops\":[";
        for (ULONG loop = 0; loop < face.m_lNumLoops; ++loop)
        {
            SmLoopData& loopData = data->m_vLoops[face.m_lStartLoop + loop];
            if (loop)
                out += ',';
            if (loopData.m_lLoopType == 1)
            {
                out += "{\"vertex\":" + std::to_string(loopData.m_lVertex) + "}";
                continue;
            }
            out += '[';
            bool first = true;
            for (ULONG use = 0; use < loopData.m_lNumEU; ++use)
            {
                SmEUData& edgeuse = data->m_vEdgeuses[loopData.m_lStartEU + use];
                if (edgeuse.m_lEUType != 0)
                    continue;
                if (!first)
                    out += ',';
                first = false;
                out += "[" + std::to_string(edgeuse.m_lEdge) + "," + (edgeuse.m_bOrientation == TRUE ? "true" : "false") + "]";
            }
            out += ']';
        }
        out += "],\"surface\":";
        SmSurface* surface = data->m_vSurfaces[face.m_lSurface];
        if (surface->IsKindOf(SmSphere_TYPE))
        {
            SmAxis2Placement placement;
            double radius = 0.0;
            static_cast<SmSphere*>(surface)->GetCanonical(placement, radius);
            out += "{\"kind\":\"sphere\",";
            jsonPlacement(out, "center", placement);
            out += ",\"radius\":";
            jsonNumber(out, radius);
            out += '}';
        }
        else if (surface->IsKindOf(SmPlane_TYPE))
        {
            SmAxis2Placement placement;
            static_cast<SmPlane*>(surface)->GetCanonical(placement);
            out += "{\"kind\":\"plane\",";
            jsonPlacement(out, "origin", placement);
            out += '}';
        }
        else if (surface->IsKindOf(SmCylinder_TYPE))
        {
            SmAxis2Placement placement;
            double radius = 0.0;
            static_cast<SmCylinder*>(surface)->GetCanonical(placement, radius);
            out += "{\"kind\":\"cylinder\",";
            jsonPlacement(out, "origin", placement);
            out += ",\"radius\":";
            jsonNumber(out, radius);
            out += '}';
        }
        else if (surface->IsKindOf(SmCone_TYPE))
        {
            SmAxis2Placement placement;
            double radius = 0.0, semiAngle = 0.0;
            static_cast<SmCone*>(surface)->GetCanonical(placement, radius, semiAngle);
            out += "{\"kind\":\"cone\",";
            jsonPlacement(out, "origin", placement);
            out += ",\"radius\":";
            jsonNumber(out, radius);
            out += ",\"semiAngle\":";
            jsonNumber(out, SM_DEG2RAD(semiAngle));
            out += '}';
        }
        else if (surface->IsKindOf(SmTorus_TYPE))
        {
            SmAxis2Placement placement;
            double major = 0.0, minor = 0.0;
            static_cast<SmTorus*>(surface)->GetCanonical(placement, major, minor);
            out += "{\"kind\":\"torus\",";
            jsonPlacement(out, "origin", placement);
            out += ",\"majorRadius\":";
            jsonNumber(out, major);
            out += ",\"minorRadius\":";
            jsonNumber(out, minor);
            out += '}';
        }
        else
        {
            SmBSplineSurface* spline = nullptr;
            SmObjDelete cleanSpline;
            if (surface->IsKindOf(SmBSplineSurface_TYPE))
                spline = SM_REINTERPRET_CAST(SmBSplineSurface*, surface);
            else
            {
                double achieved = 0.0;
                surface->ApproximateSurface(*surface->GetContext(), approximation, achieved, 5, spline, TRUE);
                cleanSpline.SetObj(spline);
            }
            if (spline == nullptr)
                out += "null";
            else
                jsonSurface(out, *spline);
        }
        out += '}';
    }
    out += "],\"shells\":[";
    bool firstShell = true;
    for (ULONG index = 0; index < data->m_vShells.GetSize(); ++index)
    {
        SmShellData& shell = data->m_vShells[index];
        if (!firstShell)
            out += ',';
        firstShell = false;
        out += '[';
        if (shell.m_lShellType == 0)
            for (ULONG use = 0; use < shell.m_lNumFaceuses; ++use)
            {
                SmFaceuseData& faceuse = data->m_vFaceuses[shell.m_lFaceuseStart + use];
                if (use)
                    out += ',';
                out += "[" + std::to_string(faceuse.m_lFace) + "," + (faceuse.m_bOrientation == TRUE ? "true" : "false") + "]";
            }
        out += ']';
    }
    out += "],\"regions\":[";
    for (ULONG index = 0; index < data->m_lNumRegions; ++index)
    {
        SmRegionData& region = data->m_vRegions[data->m_lStartRegion + index];
        if (index)
            out += ',';
        out += "[" + std::to_string(region.m_lNumShells) + "," + (region.m_bIsVoidFlag == TRUE ? "true" : "false") + "]";
    }
    out += "]}";
    *length = out.size();
    *text = static_cast<char*>(std::malloc(out.size() + 1));
    if (*text == nullptr)
        return failWith("cs_brep_exact", "out of memory");
    std::memcpy(*text, out.c_str(), out.size() + 1);
    return 0;
    CS_CATCH("cs_brep_exact")
}

void cs_text_free(char* text) { std::free(text); }

int cs_brep_read_file(const char* path, int ascii, int rebuildUvTrims, CsBrep** result)
{
    CS_TRY
    SmBrep* brep = nullptr;
    const TPath name = toTChar(path);
    CS_CHECK("SmApiReadBrepFromFile", SmApiReadBrepFromFile(name.c_str(), ascii ? TRUE : FALSE, rebuildUvTrims ? TRUE : FALSE, brep));
    *result = reinterpret_cast<CsBrep*>(brep);
    return 0;
    CS_CATCH("SmApiReadBrepFromFile")
}

int cs_stitch_into_solid(CsBrep* value, int* solid)
{
    CS_TRY
    SmBoolean producesSolid = FALSE;
    ULONG stitched = 0;
    double vertexGap = 0.0, edgeGap = 0.0;
    CS_CHECK("SmApiStitchIntoSolid", SmApiStitchIntoSolid(asBrep(value), producesSolid, stitched, vertexGap, edgeGap));
    *solid = producesSolid ? 1 : 0;
    return 0;
    CS_CATCH("SmApiStitchIntoSolid")
}

int cs_heal_brep(CsBrep* value)
{
    CS_TRY
    CS_CHECK("SmApiHealBrep", SmApiHealBrep(asBrep(value)));
    return 0;
    CS_CATCH("SmApiHealBrep")
}

} // extern "C"
