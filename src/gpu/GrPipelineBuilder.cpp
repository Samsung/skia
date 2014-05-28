/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrPipelineBuilder.h"

#include "GrBlend.h"
#include "GrPaint.h"
#include "GrPipeline.h"
#include "GrProcOptInfo.h"
#include "GrXferProcessor.h"
#include "batches/GrBatch.h"
#include "effects/GrPorterDuffXferProcessor.h"

GrPipelineBuilder::GrPipelineBuilder()
    : fFlags(0x0)
    , fDrawFace(kBoth_DrawFace)
    , fCanOptimizeForBitmapShader(false)
    , fIsOpaque (false)
    , fUseStencilBufferForWindingRules(true)
    , fClipBitsOverWrite(false) {

    SkDEBUGCODE(fBlockEffectRemovalCnt = 0;)
    fLocalMatrix.setIdentity();
}

GrPipelineBuilder::GrPipelineBuilder(const GrPaint& paint, GrRenderTarget* rt, const GrClip& clip) {
    SkDEBUGCODE(fBlockEffectRemovalCnt = 0;)

    for (int i = 0; i < paint.numColorFragmentProcessors(); ++i) {
        fColorFragmentProcessors.push_back(SkRef(paint.getColorFragmentProcessor(i)));
    }

    for (int i = 0; i < paint.numCoverageFragmentProcessors(); ++i) {
        fCoverageFragmentProcessors.push_back(SkRef(paint.getCoverageFragmentProcessor(i)));
    }

    fXPFactory.reset(SkRef(paint.getXPFactory()));

    this->setRenderTarget(rt);

    // These have no equivalent in GrPaint, set them to defaults
    fDrawFace = kBoth_DrawFace;
    fStencilSettings.setDisabled();
    fFlags = 0;

    fClip = clip;
    this->fLocalMatrix = paint.getLocalMatrix();
    this->fCanOptimizeForBitmapShader = paint.canOptimizeForBitmapShader();

    GrColor color = paint.getColor();
    this->fIsOpaque = paint.isConstantBlendedColor(&color);
    this->setState(GrPipelineBuilder::kHWAntialias_Flag,
                   rt->isUnifiedMultisampled() && paint.isAntiAlias());
}

//////////////////////////////////////////////////////////////////////////////s

bool GrPipelineBuilder::willXPNeedDstTexture(const GrCaps& caps,
                                             const GrProcOptInfo& colorPOI,
                                             const GrProcOptInfo& coveragePOI) const {
    return this->getXPFactory()->willNeedDstTexture(caps, colorPOI, coveragePOI,
                                                    this->hasMixedSamples());
}

void GrPipelineBuilder::AutoLocalMatrixChange::restore() {
    if (NULL != fDrawState) {
        SkDEBUGCODE(--fDrawState->fBlockEffectRemovalCnt;)
        fDrawState = NULL;
    }
}

void GrPipelineBuilder::AutoLocalMatrixChange::set(GrPipelineBuilder* drawState) {
    this->restore();

    if (drawState == NULL)
        return;

    if (drawState->canOptimizeForBitmapShader()) {
        SkASSERT(drawState->numColorStages() >= 1);
        const GrFragmentProcessor *fp = drawState->getColorFragmentProcessor(0);
        GrCoordTransform& transform = (GrCoordTransform&) fp->coordTransform(0);
        SkMatrix& m = (SkMatrix&) transform.getMatrix();
        const SkMatrix &localMatrix = drawState->getLocalMatrix();
        SkMatrix inv;
        if (localMatrix.invert(&inv))
            m.preConcat(inv);
    }
    fDrawState = drawState;

    SkDEBUGCODE(++fDrawState->fBlockEffectRemovalCnt;)
}

////////////////////////////////////////////////////////////////////////////////

void GrPipelineBuilder::AutoLocalMatrixRestore::restore() {
    if (NULL != fDrawState) {
        SkDEBUGCODE(--fDrawState->fBlockEffectRemovalCnt;)
        fDrawState = NULL;
    }
}


void GrPipelineBuilder::AutoLocalMatrixRestore::set(GrPipelineBuilder* drawState, SkMatrix& matrix) {
    this->restore();

    SkASSERT(NULL == fDrawState);
    if (NULL == drawState)
        return;

    if (drawState->canOptimizeForBitmapShader()) {
        SkASSERT(drawState->numColorStages() >= 1);
        const GrFragmentProcessor *fp = drawState->getColorFragmentProcessor(0);
        GrCoordTransform& transform = (GrCoordTransform&) fp->coordTransform(0);
        SkMatrix& m = (SkMatrix&) transform.getMatrix();
        m.preConcat(matrix);
    }
    fDrawState = drawState;
    SkDEBUGCODE(++fDrawState->fBlockEffectRemovalCnt;)
}

void GrPipelineBuilder::AutoRestoreFragmentProcessorState::set(
                                                         const GrPipelineBuilder* pipelineBuilder) {
    if (fPipelineBuilder) {
        int m = fPipelineBuilder->numColorFragmentProcessors() - fColorEffectCnt;
        SkASSERT(m >= 0);
        for (int i = 0; i < m; ++i) {
            fPipelineBuilder->fColorFragmentProcessors.fromBack(i)->unref();
        }
        fPipelineBuilder->fColorFragmentProcessors.pop_back_n(m);

        int n = fPipelineBuilder->numCoverageFragmentProcessors() - fCoverageEffectCnt;
        SkASSERT(n >= 0);
        for (int i = 0; i < n; ++i) {
            fPipelineBuilder->fCoverageFragmentProcessors.fromBack(i)->unref();
        }
        fPipelineBuilder->fCoverageFragmentProcessors.pop_back_n(n);
        SkDEBUGCODE(--fPipelineBuilder->fBlockEffectRemovalCnt;)
    }
    fPipelineBuilder = const_cast<GrPipelineBuilder*>(pipelineBuilder);
    if (nullptr != pipelineBuilder) {
        fColorEffectCnt = pipelineBuilder->numColorFragmentProcessors();
        fCoverageEffectCnt = pipelineBuilder->numCoverageFragmentProcessors();
        SkDEBUGCODE(++pipelineBuilder->fBlockEffectRemovalCnt;)
    }
}

////////////////////////////////////////////////////////////////////////////////

GrPipelineBuilder::~GrPipelineBuilder() {
    SkASSERT(0 == fBlockEffectRemovalCnt);
    for (int i = 0; i < fColorFragmentProcessors.count(); ++i) {
        fColorFragmentProcessors[i]->unref();
    }
    for (int i = 0; i < fCoverageFragmentProcessors.count(); ++i) {
        fCoverageFragmentProcessors[i]->unref();
    }
}

////////////////////////////////////////////////////////////////////////////////

void GrPipelineBuilder::calcColorInvariantOutput(const GrDrawBatch* batch) const {
    fColorProcInfo.calcColorWithBatch(batch, fColorFragmentProcessors.begin(),
                                      this->numColorFragmentProcessors());
}

void GrPipelineBuilder::calcCoverageInvariantOutput(const GrDrawBatch* batch) const {
    fCoverageProcInfo.calcCoverageWithBatch(batch, fCoverageFragmentProcessors.begin(),
                                            this->numCoverageFragmentProcessors());
}

