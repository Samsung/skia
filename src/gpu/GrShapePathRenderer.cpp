/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrShapePathRenderer.h"

#include "GrContext.h"
#include "GrPathUtils.h"
#include "SkString.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "SkTraceEvent.h"
#include "GrDefaultGeoProcFactory.h"


GrShapePathRenderer::GrShapePathRenderer() {}

////////////////////////////////////////////////////////////////////////////////
// Stencil rules for paths
GR_STATIC_CONST_SAME_STENCIL(gShapeStencilOverWrite,
                             kZero_StencilOp,
                             kZero_StencilOp,
                             kEqual_StencilFunc,
                             0xffff,
                             0xffff,
                             0xffff);

GR_STATIC_CONST_SAME_STENCIL(gShapeStencilKeep,
                             kKeep_StencilOp,
                             kKeep_StencilOp,
                             kEqual_StencilFunc,
                             0xffff,
                             0xffff,
                             0xffff);

////////////////////////////////////////////////////////////////////////////////
// Helpers for drawPath

#define STENCIL_OFF     0   // Always disable stencil (even when needed)

GrPathRenderer::StencilSupport GrShapePathRenderer::onGetStencilSupport(const GrDrawTarget*,
                                                   const GrPipelineBuilder*,
                                                   const SkPath&,
                                                   const SkStrokeRec&) const {
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
        // GrPrintf("Path not rendered, too many verts (%d)\n", maxPts);
        return false;
    }

    *primType = kTriangles_GrPrimitiveType;

    if (!arg->set(target, maxPts, GrDefaultGeoProcFactory::DefaultVertexStride(), 0)) {
        return false;
    }
    SkASSERT(GrDefaultGeoProcFactory::DefaultVertexStride() == sizeof(SkPoint));

    SkPoint* base = reinterpret_cast<SkPoint*>(arg->vertices());
    SkASSERT(NULL != base);
    SkPoint* vert = base;

    SkPoint outerPts[4];
    SkPoint innerPts[4];
    SkPoint pts[4];

    // Tessellate outer and inner path,
    // inner and outer path should have same verbs
    SkPath::Iter outerIter(outer, false);
    SkPath::Iter innerIter(inner, false);

    for (;;) {
        SkPath::Verb outerVerb = outerIter.next(outerPts);
        innerIter.next(innerPts);
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
            case SkPath::kQuad_Verb:
                // first pt of quad is the pt we ended on in previous step
                GrPathUtils::generateShapedQuadraticPoints(outerPts, innerPts,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(outerPts, srcSpaceTol),
                            GrPathUtils::quadraticPointCount(innerPts, srcSpaceTol));
                break;
            case SkPath::kCubic_Verb:
                // First pt of cubic is the pt we ended on in previous step
                GrPathUtils::generateShapedCubicPoints(outerPts, innerPts,
                                srcSpaceTolSqd, &vert,
                                GrPathUtils::cubicPointCount(outerPts, srcSpaceTol),
                                GrPathUtils::cubicPointCount(innerPts, srcSpaceTol));
                break;
            case SkPath::kClose_Verb:
                break;
            case SkPath::kDone_Verb:
                goto CAPS_AND_JOINS;
        }
    }

CAPS_AND_JOINS:
    // Tessellate joins and caps
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
            case SkPath::kQuad_Verb:
                // First pt of quad is the pt we ended on in previous step
                GrPathUtils::generateFanQuadraticPoints(
                            pts[0], pts[1], pts[2], lastPt,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(pts, srcSpaceTol));
                break;
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
                                           GrPipelineBuilder* pipelineBuilder,
                                           GrColor color,
                                           const SkMatrix& viewMatrix,
                                           bool isOpaque) {
    SkMatrix viewM = viewMatrix;
    uint8_t newCoverage = 0xff;

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
    // Face culling doesn't make sense here
    SkASSERT(GrPipelineBuilder::kBoth_DrawFace == pipelineBuilder->getDrawFace());


    GrStencilSettings stencilSetting;
    GrPipelineBuilder::DrawFace drawFace =  GrPipelineBuilder::kBoth_DrawFace;


    SkRect devBounds;
    GetPathDevBounds(outer, pipelineBuilder->getRenderTarget(), viewM, &devBounds);
    SkRect capsAndJoinsBounds, innerBounds;
    GetPathDevBounds(join, pipelineBuilder->getRenderTarget(), viewM, &capsAndJoinsBounds);
    GetPathDevBounds(inner, pipelineBuilder->getRenderTarget(), viewM, &innerBounds);
    devBounds.join(capsAndJoinsBounds.left(), capsAndJoinsBounds.top(),
                   capsAndJoinsBounds.right(), capsAndJoinsBounds.bottom());
    devBounds.join(innerBounds.left(), innerBounds.top(),
                   innerBounds.right(), innerBounds.bottom());

    pipelineBuilder->setDrawFace(drawFace);

    if (!pipelineBuilder->isOpaque()) {
        stencilSetting = (GrStencilSettings) gShapeStencilOverWrite;
    } else {
        stencilSetting = (GrStencilSettings) gShapeStencilKeep;
    }

    stencilSetting.setOverWrite();
    *pipelineBuilder->stencil() = stencilSetting;
    GrPipelineBuilder::AutoRestoreEffects are(pipelineBuilder);
        SkAutoTUnref<const GrGeometryProcessor> gp(
                    GrDefaultGeoProcFactory::Create(GrDefaultGeoProcFactory::kPosition_GPType,
                                                    color,
                                                    viewMatrix,
                                                    SkMatrix::I(),
                                                    false,
                                                    newCoverage));


    if (!pipelineBuilder->isOpaque()) {
        target->drawNonIndexed(pipelineBuilder, gp, primType, 0, vertexCnt, &devBounds,false, true);
    } else {
        target->drawNonIndexed(pipelineBuilder, gp, primType, 0, vertexCnt, &devBounds,false, false);
    }
    return true;
}

bool GrShapePathRenderer::canDrawPath(const SkPath& pathA,
                                      const SkPath& pathB,
                                      const SkPath& pathC,
                                      const SkStrokeRec& stroke,
                                      const GrDrawTarget* target,
                                      GrPipelineBuilder* pipelineBuilder,
                                      GrColor color,
                                      const SkMatrix& viewMatrix,
                                      bool antiAlias) const {
    // This class can draw any path with any fill but doesn't do any anti-aliasing.

    return !antiAlias &&
         SkStrokeRec::kStroke_Style == stroke.getStyle() &&
         !IsStrokeHairlineOrEquivalent(stroke, viewMatrix, NULL);
}

bool onDrawPath(GrDrawTarget*,
                            GrPipelineBuilder*,
                            GrColor,
                            const SkMatrix& viewMatrix,
                            const SkPath&,
                            const SkStrokeRec&,
                            bool antiAlias) {
    return false;
}

bool GrShapePathRenderer::onDrawPath(const SkPath& outer,
                                     const SkPath& inner,
                                     const SkPath& join,
                                     const SkStrokeRec& stroke,
                                     GrDrawTarget* target,
                                     GrPipelineBuilder* pipelineBuilder,
                                     GrColor color,
                                     const SkMatrix& viewMatrix,
                                     bool isOpaque) {
    return this->internalDrawPath(outer,
                                  inner,
                                  join,
                                  stroke,
                                  target,
                                  pipelineBuilder,
                                  color,
                                  viewMatrix,
                                  isOpaque);
}

void GrShapePathRenderer::onStencilPath(const SkPath& outer,
                                        const SkPath& inner,
                                        const SkPath& join,
                                        const SkStrokeRec& stroke,
                                        GrDrawTarget* target,
                                        GrPipelineBuilder* pipelineBuilder,
                                        GrColor color,
                                        const SkMatrix& viewMatrix) {
    this->internalDrawPath(outer, inner, join, stroke, target,   pipelineBuilder,    color,    viewMatrix,true);
    return;
}

