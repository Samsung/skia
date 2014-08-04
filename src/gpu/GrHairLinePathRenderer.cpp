/*
 * Copyright 2014 Samsung Research America, Inc. - Silicon Valley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrHairLinePathRenderer.h"

#include "GrContext.h"
#include "GrDrawState.h"
#include "SkString.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "SkTraceEvent.h"
#include "GrPathUtils.h"
#include "GrGpu.h"

////////////////////////////////////////////////////////////////////////////////
static const int MAX_LINE_POINTS_PER_CURVE = 2048;  // 16 x 2048 = 32K
static const int MAX_POINTS = 1 << 11; // 2048
static const SkScalar gMinCurveTol = 0.0001f;

struct HairLineVertex {
    SkPoint fPos;
    GrColor fColor;
    GrColor fCoverage;
};

static uint32_t quadraticPointCount(const SkPoint points[], SkScalar tol)
{
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }
    SkASSERT(tol > 0);

    SkScalar d = points[1].distanceToLineSegmentBetween(points[0], points[2]);

    if (d <= tol)
        return 2;
    else {
        int temp = SkScalarCeilToInt(SkScalarSqrt(SkScalarDiv(d, tol)));
        int pow2 = GrNextPow2(temp) * 2;

        return SkTMin(pow2, MAX_LINE_POINTS_PER_CURVE);
    }
}

static uint32_t generateQuadraticPoints(const SkPoint& p0,
                                        const SkPoint& p1,
                                        const SkPoint& p2,
                                        SkScalar tolSqd,
                                        HairLineVertex** points,
                                        uint32_t pointsLeft,
                                        GrColor color, GrColor coverage)
{
    if (pointsLeft <= 2 ||
        (p1.distanceToLineSegmentBetweenSqd(p0, p2)) < tolSqd) {
        (*points)[0].fPos = p0;
        (*points)[0].fColor = color;
        (*points)[0].fCoverage = coverage;
        (*points)[1].fPos = p2;
        (*points)[1].fColor = color;
        (*points)[1].fCoverage = coverage;
        *points += 2;
        return 2;
    }

    SkPoint q[] = {
        { SkScalarAve(p0.fX, p1.fX), SkScalarAve(p0.fY, p1.fY) },
        { SkScalarAve(p1.fX, p2.fX), SkScalarAve(p1.fY, p2.fY) },
    };
    SkPoint r = { SkScalarAve(q[0].fX, q[1].fX), SkScalarAve(q[0].fY, q[1].fY) };
    pointsLeft >>= 1;
    uint32_t a = generateQuadraticPoints(p0, q[0], r, tolSqd, points, pointsLeft, color, coverage);
    uint32_t b = generateQuadraticPoints(r, q[1], p2, tolSqd, points, pointsLeft, color, coverage);
    return a + b;
}

static uint32_t cubicPointCount(const SkPoint points[], SkScalar tol)
{
    if (tol < gMinCurveTol)
        tol = gMinCurveTol;

    SkASSERT(tol > 0);

    SkScalar d = SkTMax(
        points[1].distanceToLineSegmentBetweenSqd(points[0], points[3]),
        points[2].distanceToLineSegmentBetweenSqd(points[0], points[3]));
    d = SkScalarSqrt(d);
    if (d <= tol) {
        return 2;
    } else {
        int temp = SkScalarCeilToInt(SkScalarSqrt(SkScalarDiv(d, tol)));
        int pow2 = GrNextPow2(temp) * 2;
        if (pow2 < 2)
            pow2 = 2;
        return SkTMin(pow2, MAX_LINE_POINTS_PER_CURVE);
    }
}

static uint32_t generateCubicPoints(const SkPoint& p0,
                                    const SkPoint& p1,
                                    const SkPoint& p2,
                                    const SkPoint& p3,
                                    SkScalar tolSqd,
                                    HairLineVertex** points,
                                    uint32_t pointsLeft,
                                    GrColor color, GrColor coverage)
{
    if (pointsLeft <= 2 ||
        (p1.distanceToLineSegmentBetweenSqd(p0, p3) < tolSqd &&
         p2.distanceToLineSegmentBetweenSqd(p0, p3) < tolSqd)) {
            (*points)[0].fPos = p0;
            (*points)[0].fColor = color;
            (*points)[0].fCoverage = coverage;
            (*points)[1].fPos = p3;
            (*points)[1].fColor = color;
            (*points)[1].fCoverage = coverage;
            *points += 2;
            return 2;
    }

    SkPoint q[] = {
        { SkScalarAve(p0.fX, p1.fX), SkScalarAve(p0.fY, p1.fY) },
        { SkScalarAve(p1.fX, p2.fX), SkScalarAve(p1.fY, p2.fY) },
        { SkScalarAve(p2.fX, p3.fX), SkScalarAve(p2.fY, p3.fY) }
    };
    SkPoint r[] = {
        { SkScalarAve(q[0].fX, q[1].fX), SkScalarAve(q[0].fY, q[1].fY) },
        { SkScalarAve(q[1].fX, q[2].fX), SkScalarAve(q[1].fY, q[2].fY) }
    };
    SkPoint s = { SkScalarAve(r[0].fX, r[1].fX), SkScalarAve(r[0].fY, r[1].fY) };

    pointsLeft >>= 1;
    uint32_t a = generateCubicPoints(p0, q[0], r[0], s, tolSqd, points, pointsLeft, color, coverage);
    uint32_t b = generateCubicPoints(s, r[1], q[2], p3, tolSqd, points, pointsLeft, color, coverage);
    return a + b;
}

static int worstCasePointCount(const SkPath& path, SkScalar tol)
{
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }

    SkASSERT(tol > 0);

    int pointCount = 0;

    SkPath::Iter iter(path, false);
    SkPath::Verb verb;

    SkPoint pts[4];
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kLine_Verb:
                pointCount += 2;
                break;
            case SkPath::kQuad_Verb:
                pointCount += quadraticPointCount(pts, tol);
                break;
            case SkPath::kCubic_Verb:
                pointCount += cubicPointCount(pts, tol);
                break;
            case SkPath::kMove_Verb:
            default:
                break;
        }
    }

    return pointCount;
}

static const uint16_t gLineIndices[] = { 0, 1 };

static inline void fill_indices(uint16_t* indices, const int count)
{
    for (int i = 0; i < count; i++)
        indices[i] = i;
}

extern const GrVertexAttrib gHairLineVertexAttribs[] = {
    {kVec2f_GrVertexAttribType, 0,                kPosition_GrVertexAttribBinding},
    {kVec4ub_GrVertexAttribType, sizeof(SkPoint), kColor_GrVertexAttribBinding},
    {kVec4ub_GrVertexAttribType, sizeof(SkPoint) + sizeof(uint8_t) * 4, kCoverage_GrVertexAttribBinding}
};
    
////////////////////////////////////////////////////////////////////////////////
GrHairLinePathRenderer::~GrHairLinePathRenderer()
{
    SkSafeSetNull(fIndexBuffer);
}

GrIndexBuffer* GrHairLinePathRenderer::indexBuffer(GrGpu* gpu)
{
    if (fIndexBuffer == NULL) {
        fIndexBuffer = gpu->createIndexBuffer(MAX_POINTS * sizeof(uint16_t), false);
        if (NULL != fIndexBuffer) {
            // FIXME: use lock/ublock when port to later version
            uint16_t* indices = (uint16_t*) fIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, MAX_POINTS);
                fIndexBuffer->unmap();
            } else {
                indices = (uint16_t*) sk_malloc_throw(sizeof(uint16_t) * MAX_POINTS);
                fill_indices(indices, MAX_POINTS);
                if (!fIndexBuffer->updateData(indices, MAX_POINTS * sizeof(uint16_t))) {
                    fIndexBuffer->unref();
                    fIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fIndexBuffer;
}


bool GrHairLinePathRenderer::internalDrawPath(const SkPath& path,
                                              const SkStrokeRec& origStroke,
                                              GrDrawTarget* target,
                                              bool stencilOnly) {
    SkScalar hairlineCoverage;
    SkTCopyOnFirstWrite<SkStrokeRec> stroke(origStroke);

    if (IsStrokeHairlineOrEquivalent(*stroke, target->getDrawState().getViewMatrix(),
                                     &hairlineCoverage)) {
        uint8_t newCoverage = SkScalarRoundToInt(hairlineCoverage *
                                                 target->getDrawState().getCoverage());
        target->drawState()->setCoverage(newCoverage);

        if (!stroke->isHairlineStyle()) {
            stroke.writable()->setHairlineStyle();
        }
    }

    SkScalar tol = SK_Scalar1;
    tol = GrPathUtils::scaleToleranceToSrc(tol, target->getDrawState().getViewMatrix(), path.getBounds());
    SkScalar srcSpaceTolSqd = SkScalarMul(tol, tol);

    GrDrawState* drawState = target->drawState();
    SkMatrix viewM = drawState->getViewMatrix();
    GrColor coverage = drawState->getCoverageColor();
    GrColor color = drawState->getColor();

    int vertexCnt;
    int indexCnt;
    int maxIds;
    int maxPts;

    int maxIdxs = maxPts = fNumPts;

    GrContext* context = drawState->getRenderTarget()->getContext();

    GrDrawState::AutoViewMatrixRestore avmr;
    if (!avmr.setIdentity(drawState)) {
        fNumPts = 0;
        return false;
    }

    GrIndexBuffer* indexBuffer = this->indexBuffer(context->getGpu());
    if (NULL == indexBuffer) {
        fNumPts = 0;
        return false;
    }

    GrDrawState::AutoColorRestore acr;
    acr.set(drawState, 0xFFFFFFFF);

    GrDrawState::AutoCoverageRestore acvr;
    acvr.set(drawState, 0xFF);

    drawState->setVertexAttribs<gHairLineVertexAttribs>(SK_ARRAY_COUNT(gHairLineVertexAttribs));
    SkASSERT(sizeof(HairLineVertex) == drawState->getVertexSize());

    GrDrawTarget::AutoReleaseGeometry geo(target, maxPts, 0);
    if (!geo.succeeded()) {
        fNumPts = 0;
        GrPrintf("Failed to get space for vertices!\n");
        return false;
    }

    HairLineVertex* verts = reinterpret_cast<HairLineVertex*>(geo.vertices());
    HairLineVertex* base = verts;

    SkPoint pts[4];
    SkPoint unmapped[4];
    SkPath::Iter iter(path, false);
    for (;;) {
        SkPath::Verb verb = iter.next(unmapped);
        switch (verb) {
            case SkPath::kConic_Verb:
                SkASSERT(0);
                break;
            case SkPath::kLine_Verb:
                viewM.mapPoints(pts, unmapped, 2);
                (*verts).fPos = pts[0];
                (*verts).fColor = color;
                (*verts).fCoverage = coverage;
                verts++;
                (*verts).fPos = pts[1];
                (*verts).fColor = color;
                (*verts).fCoverage = coverage;
                verts++;
                break;
            case SkPath::kQuad_Verb:
                viewM.mapPoints(pts, unmapped, 3);
                generateQuadraticPoints(pts[0], pts[1], pts[2],
                                        srcSpaceTolSqd, &verts,
                                        quadraticPointCount(pts, tol),
                                        color, coverage);
                break;
            case SkPath::kCubic_Verb:
                viewM.mapPoints(pts, unmapped, 4);
                generateCubicPoints(pts[0], pts[1], pts[2], pts[3],
                                    srcSpaceTolSqd, &verts,
                                    cubicPointCount(pts, tol),
                                    color, coverage);
                break;
            case SkPath::kClose_Verb:
            case SkPath::kMove_Verb:
                break;
            case SkPath::kDone_Verb:
                goto FINISHED;
        }
    }
                    
FINISHED:
    int vertex = static_cast<int>(verts - base);
    SkRect devBounds;
    GetPathDevBounds(path, drawState->getRenderTarget(), viewM, &devBounds);

    target->setIndexSourceToBuffer(indexBuffer);
    target->drawIndexedInstances(kLines_GrPrimitiveType, 1, vertex, vertex, &devBounds);
    fNumPts = 0;
    return true;
}

bool GrHairLinePathRenderer::canDrawPath(const SkPath& path,
                                         const SkStrokeRec& stroke,
                                         const GrDrawTarget* target,
                                         bool antiAlias) const {
    // this class can draw any path with any fill but doesn't do any anti-aliasing.

    bool canDraw = !antiAlias &&
        (stroke.isHairlineStyle() ||
         IsStrokeHairlineOrEquivalent(stroke, target->getDrawState().getViewMatrix(), NULL));

    if (canDraw) {
        if (fNumPts == 0) {
            SkScalar tol = SK_Scalar1;
            tol = GrPathUtils::scaleToleranceToSrc(tol,
                        target->getDrawState().getViewMatrix(),
                        path.getBounds());
            int maxPts = worstCasePointCount(path, tol);
            ((GrHairLinePathRenderer*)this)->setNumberOfPts(maxPts);
            return maxPts > 0 && maxPts <= MAX_POINTS;
        }
        else
            return fNumPts > 0 && fNumPts <= MAX_POINTS;
    }
    return false;
}

bool GrHairLinePathRenderer::onDrawPath(const SkPath& path,
                                       const SkStrokeRec& stroke,
                                       GrDrawTarget* target,
                                       bool antiAlias) {
    return this->internalDrawPath(path,
                                  stroke,
                                  target,
                                  false);
}

GrPathRenderer::StencilSupport GrHairLinePathRenderer::onGetStencilSupport(
                                                          const SkPath& path,
                                                          const SkStrokeRec& stroke,
                                                          const GrDrawTarget*) const {
    return GrPathRenderer::kNoRestriction_StencilSupport;
}
