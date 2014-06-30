/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrShapePathRenderer.h"

#include "GrContext.h"
#include "GrDrawState.h"
#include "GrPathUtils.h"
#include "SkString.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "SkTraceEvent.h"


GrShapePathRenderer::GrShapePathRenderer() {}

////////////////////////////////////////////////////////////////////////////////
// Stencil rules for paths
GR_STATIC_CONST_SAME_STENCIL(gShapeStencilPass,
    kZero_StencilOp,
    kZero_StencilOp,
    kEqual_StencilFunc,
    0xffff,
    0xffff,
    0xffff);

GR_STATIC_CONST_STENCIL(gShapeStencilSeparate,
    kZero_StencilOp,               kZero_StencilOp,
    kZero_StencilOp,               kZero_StencilOp,
    kEqual_StencilFunc,            kEqual_StencilFunc,
    0xffff,                        0xffff,
    0xffff,                        0xffff,
    0xffff,                        0xffff);

////// Normal render to stencil

// Sometimes the default path renderer can draw a path directly to the stencil
// buffer without having to first resolve the interior / exterior.
GR_STATIC_CONST_SAME_STENCIL(gDirectToStencil,
    kZero_StencilOp,
    kZero_StencilOp,
    kEqual_StencilFunc,
    0xffff,
    0xffff,
    0xffff);

////////////////////////////////////////////////////////////////////////////////
// Helpers for drawPath

#define STENCIL_OFF     0   // Always disable stencil (even when needed)

GrShapePathRenderer::StencilSupport GrShapePathRenderer::onGetStencilSupport(
                                                            const SkPath& path,
                                                            const SkStrokeRec& stroke,
                                                            const GrDrawTarget*) const {
    return GrPathRenderer::kNoRestriction_StencilSupport;
}

bool GrShapePathRenderer::createGeom(const SkPath& outer,
                                     const SkPath& inner,
                                     const SkPath& joinsAndCaps,
                                     SkScalar srcSpaceTol,
                                     GrDrawTarget* target,
                                     GrPrimitiveType* primType,
                                     int* vertexCnt,
                                     GrDrawTarget::AutoReleaseGeometry* arg) {
    SkScalar srcSpaceTolSqd = SkScalarMul(srcSpaceTol, srcSpaceTol);
    int outerContourCnt;
    int innerContourCnt;
    int joinsContourCnt;
    int contourCnt;
    int maxPts = GrPathUtils::worstCasePointCount(outer, &outerContourCnt,
                                                  srcSpaceTol) * 6;
    contourCnt = outerContourCnt;
    maxPts += GrPathUtils::worstCasePointCount(inner, &innerContourCnt,
                                               srcSpaceTol) * 6;
    contourCnt += innerContourCnt;
    maxPts += GrPathUtils::worstCasePointCount(joinsAndCaps, &joinsContourCnt,
                                               srcSpaceTol) * 6;
    contourCnt += joinsContourCnt;

    if (maxPts <= 0) {
        return false;
    }
    if (maxPts > ((int)SK_MaxU16 + 1)) {
        GrPrintf("Path not rendered, too many verts (%d)\n", maxPts);
        return false;
    }

    *primType = kTriangles_GrPrimitiveType;

    target->drawState()->setDefaultVertexAttribs();
    if (!arg->set(target, maxPts, 0)) {
        return false;
    }

    SkPoint* base = reinterpret_cast<SkPoint*>(arg->vertices());
    SkASSERT(NULL != base);
    SkPoint* vert = base;

    SkPoint outerPts[4];
    SkPoint innerPts[4];
    SkPoint pts[4];

    // tessellate outer and inner path,
    // inner and outer path should have same verbs
    SkPath::Iter outerIter(outer, false);
    SkPath::Iter innerIter(inner, false);

    for (;;) {
        SkPath::Verb outerVerb = outerIter.next(outerPts);
        SkPath::Verb innerVerb = innerIter.next(innerPts);
        switch (outerVerb) {
            case SkPath::kConic_Verb:
                SkASSERT(0);
                break;
            case SkPath::kMove_Verb:
                break;
            case SkPath::kLine_Verb:
                *(vert++) = outerPts[0];
                *(vert++) = outerPts[1];
                *(vert++) = innerPts[1];
                *(vert++) = outerPts[0];
                *(vert++) = innerPts[0];
                *(vert++) = innerPts[1];
                break;
            case SkPath::kQuad_Verb: {
                // first pt of quad is the pt we ended on in previous step
                GrPathUtils::generateShapedQuadraticPoints(outerPts, innerPts,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(outerPts, srcSpaceTol),
                            GrPathUtils::quadraticPointCount(innerPts, srcSpaceTol));
                break;
            }
            case SkPath::kCubic_Verb: {
                // first pt of cubic is the pt we ended on in previous step
                GrPathUtils::generateShapedCubicPoints(outerPts, innerPts,
                                srcSpaceTolSqd, &vert,
                                GrPathUtils::cubicPointCount(outerPts, srcSpaceTol),
                                GrPathUtils::cubicPointCount(innerPts, srcSpaceTol));
                break;
            }
            case SkPath::kClose_Verb:
                break;
            case SkPath::kDone_Verb:
             // uint16_t currIdx = (uint16_t) (vert - base);
                goto CAPS_AND_JOINS;
        }
    }

CAPS_AND_JOINS:
    // tessellate joins and caps
    SkPath::Iter iter(joinsAndCaps, false);
    SkPoint lastPt;

    for (;;) {
        SkPath::Verb verb = iter.next(pts);
        switch (verb) {
            case SkPath::kConic_Verb:
                SkASSERT(0);
                break;
            case SkPath::kMove_Verb:
                lastPt = pts[0];
                break;
            case SkPath::kLine_Verb:
                if (lastPt != pts[0 || lastPt!= pts[1]]) {
                    *(vert++) = lastPt;
                    *(vert++) = pts[0];
                    *(vert++) = pts[1];
                }
                break;
            case SkPath::kQuad_Verb: {
                // first pt of quad is the pt we ended on in previous step
                GrPathUtils::generateFanQuadraticPoints(
                            pts[0], pts[1], pts[2], lastPt,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(pts, srcSpaceTol));
                break;
            }
            case SkPath::kCubic_Verb:
                break;
            case SkPath::kClose_Verb:
                break;
            case SkPath::kDone_Verb:
             // uint16_t currIdx = (uint16_t) (vert - base);
                goto FINISHED;
        }
    }
FINISHED:
    SkASSERT((vert - base) <= maxPts);

    *vertexCnt = static_cast<int>(vert - base);

    return true;
}

bool GrShapePathRenderer::internalDrawPath(const SkPath& outer,
                                           const SkPath& inner,
                                           const SkPath& join,
                                           const SkStrokeRec& origStroke,
                                           GrDrawTarget* target,
                                           bool isOpaque) {
    SkMatrix viewM = target->getDrawState().getViewMatrix();
    SkTCopyOnFirstWrite<SkStrokeRec> stroke(origStroke);

    SkScalar tol = SK_Scalar1;
    tol = GrPathUtils::scaleToleranceToSrc(tol, viewM, outer.getBounds());

    int vertexCnt;
    GrPrimitiveType primType;
    GrDrawTarget::AutoReleaseGeometry arg;
    if (!this->createGeom(outer,
                          inner,
                          join,
                          tol,
                          target,
                          &primType,
                          &vertexCnt,
                          &arg)) {
        return false;
    }

    SkASSERT(NULL != target);
    GrDrawTarget::AutoStateRestore asr(target, GrDrawTarget::kPreserve_ASRInit);
    GrDrawState* drawState = target->drawState();
    bool colorWritesWereDisabled = drawState->isColorWriteDisabled();
    // face culling doesn't make sense here
    SkASSERT(GrDrawState::kBoth_DrawFace == drawState->getDrawFace());

    GrStencilSettings     stencilSetting;
    GrDrawState::DrawFace       drawFace = GrDrawState::kBoth_DrawFace;

    SkRect devBounds;
    GetPathDevBounds(outer, drawState->getRenderTarget(), viewM, &devBounds);
    SkRect capsAndJoinsBounds, innerBounds;
    GetPathDevBounds(join, drawState->getRenderTarget(), viewM, &capsAndJoinsBounds);
    GetPathDevBounds(inner, drawState->getRenderTarget(), viewM, &innerBounds);
    devBounds.join(capsAndJoinsBounds.left(), capsAndJoinsBounds.top(),
                   capsAndJoinsBounds.right(), capsAndJoinsBounds.bottom());
    devBounds.join(innerBounds.left(), innerBounds.top(),
                   innerBounds.right(), innerBounds.bottom());

    drawState->setDrawFace(drawFace);

    stencilSetting = (GrStencilSettings) gShapeStencilPass;
    if (!colorWritesWereDisabled) {
        drawState->disableState(GrDrawState::kNoColorWrites_StateBit);
    }

    stencilSetting.setOverWrite();
    *drawState->stencil() = stencilSetting;
    target->drawNonIndexed(primType, 0, vertexCnt, &devBounds, false);
    return true;
}

bool GrShapePathRenderer::canDrawPath(const SkPath& pathA,
                                      const SkPath& pathB,
                                      const SkPath& pathC,
                                      const SkStrokeRec& stroke,
                                      const GrDrawTarget* target,
                                      bool antiAlias) const {
    // this class can draw any path with any fill but doesn't do any anti-aliasing.

    return !antiAlias &&
         SkStrokeRec::kStroke_Style == stroke.getStyle() && 
         !IsStrokeHairlineOrEquivalent(stroke, target->getDrawState().getViewMatrix(), NULL);
}

bool GrShapePathRenderer::onDrawPath(const SkPath& path,
                                     const SkStrokeRec& stroke,
                                     GrDrawTarget* target,
                                     bool antiAlias) {
    return false;
}

bool GrShapePathRenderer::onDrawPath(const SkPath& outer,
                                     const SkPath& inner,
                                     const SkPath& join,
                                     const SkStrokeRec& stroke,
                                     GrDrawTarget* target,
                                     bool isOpaque) {
    return this->internalDrawPath(outer,
                                  inner,
                                  join,
                                  stroke,
                                  target,
                                  isOpaque);
}

void GrShapePathRenderer::onStencilPath(const SkPath& outer,
                                        const SkPath& inner,
                                        const SkPath& join,
                                        const SkStrokeRec& stroke,
                                        GrDrawTarget* target) {
    this->internalDrawPath(outer, inner, join, stroke, target, true);
}

void GrShapePathRenderer::onStencilPath(const SkPath& path,
                                        const SkStrokeRec& stroke,
                                        GrDrawTarget* target) {
    return;
}
