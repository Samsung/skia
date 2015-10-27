/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrOvalRenderer.h"

#include "GrBatch.h"
#include "GrBatchTarget.h"
#include "GrBufferAllocPool.h"
#include "GrDrawTarget.h"
#include "GrGeometryProcessor.h"
#include "GrGpu.h"
#include "GrInvariantOutput.h"
#include "GrPipelineBuilder.h"
#include "GrProcessor.h"
#include "SkRRect.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "effects/GrRRectEffect.h"
#include "gl/GrGLProcessor.h"
#include "gl/GrGLSL.h"
#include "gl/GrGLGeometryProcessor.h"
#include "gl/builders/GrGLProgramBuilder.h"
#include "GrDefaultGeoProcFactory.h"

// TODO(joshualitt) - Break this file up during GrBatch post implementation cleanup

namespace {
// TODO(joshualitt) add per vertex colors
struct CircleVertex {
    SkPoint  fPos;
    SkPoint  fOffset;
    SkScalar fOuterRadius;
    SkScalar fInnerRadius;
    GrColor  fColor;
};

struct CircleUVVertex {
    SkPoint  fPos;
    SkPoint  fOffset;
    SkScalar fOuterRadius;
    SkScalar fInnerRadius;
    GrColor  fColor;
    SkPoint  fLocalPos;
};

struct EllipseVertex {
    SkPoint  fPos;
    SkPoint  fOffset;
    SkPoint  fOuterRadii;
    SkPoint  fInnerRadii;
    GrColor  fColor;
};

struct EllipseUVVertex {
    SkPoint  fPos;
    SkPoint  fOffset;
    SkPoint  fOuterRadii;
    SkPoint  fInnerRadii;
    GrColor  fColor;
    SkPoint  fLocalPos;
};

struct DIEllipseVertex {
    SkPoint  fPos;
    SkPoint  fOuterOffset;
    SkPoint  fInnerOffset;
    GrColor  fColor;
};

struct DIEllipseUVVertex {
    SkPoint  fPos;
    SkPoint  fOuterOffset;
    SkPoint  fInnerOffset;
    GrColor  fColor;
    SkPoint  fLocalPos;
};

inline bool circle_stays_circle(const SkMatrix& m) {
    return m.isSimilarity();
}

}

///////////////////////////////////////////////////////////////////////////////

/**
 * The output of this effect is a modulation of the input color and coverage for a circle. It
 * operates in a space normalized by the circle radius (outer radius in the case of a stroke)
 * with origin at the circle center. Two   vertex attributes are used:
 *    vec2f : position in device space of the bounding geometry vertices
 *    vec4f : (p.xy, outerRad, innerRad)
 *             p is the position in the normalized space.
 *             outerRad is the outerRadius in device space.
 *             innerRad is the innerRadius in normalized space (ignored if not stroking).
 */

class CircleEdgeEffect : public GrGeometryProcessor {
public:
    static GrGeometryProcessor* Create(GrColor color, bool stroke, const SkMatrix& localMatrix, bool useLocalCoords) {
        return SkNEW_ARGS(CircleEdgeEffect, (color, stroke, localMatrix, useLocalCoords));
    }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inCircleEdge() const { return fInCircleEdge; }
    const Attribute* inCircleColor() const { return fInCircleColor; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }
    virtual ~CircleEdgeEffect() {}

    const char* name() const SK_OVERRIDE { return "CircleEdge"; }

    inline bool isStroked() const { return fStroke; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor(const GrGeometryProcessor&,
                    const GrBatchTracker&)
            : fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) SK_OVERRIDE{
            const CircleEdgeEffect& ce = args.fGP.cast<CircleEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
            const BatchTracker& local = args.fBT.cast<BatchTracker>();
            GrGLVertexBuilder* vsBuilder = args.fPB->getVertexShaderBuilder();

            // emit attributes
            vsBuilder->emitAttributes(ce);

            GrGLVertToFrag v(kVec4f_GrSLType);
            args.fPB->addVarying("CircleEdge", &v);
            vsBuilder->codeAppendf("%s = %s;", v.vsOut(), ce.inCircleEdge()->fName);


            // Setup pass through color
            this->setupColorPassThrough(pb, local.fInputColorType,  args.fOutputColor , ce.inCircleColor(),
                                        &fColorUniform);

            // Setup position
            this->setupPosition(pb, gpArgs, ce.inPosition()->fName, ce.viewMatrix());
            if (ce.inLocalCoords()) {
                // emit transforms with explicit local coords
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ce.inLocalCoords()->fName,
                                     ce.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            } else {
                // emit transforms with position
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ce.inPosition()->fName,
                                     ce.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            }

            GrGLGPFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
            fsBuilder->codeAppendf("float d = length(%s.xy);", v.fsIn());
            fsBuilder->codeAppendf("float edgeAlpha = clamp(%s.z * (1.0 - d), 0.0, 1.0);", v.fsIn());
            if (ce.isStroked()) {
                fsBuilder->codeAppendf("float innerAlpha = clamp(%s.z * (d - %s.w), 0.0, 1.0);",
                                       v.fsIn(), v.fsIn());
                fsBuilder->codeAppend("edgeAlpha *= innerAlpha;");
            }

            fsBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.fOutputCoverage);
        }

        static void GenKey(const GrGeometryProcessor& gp,
                           const GrBatchTracker& bt,
                           const GrGLCaps&,
                           GrProcessorKeyBuilder* b) {
            const BatchTracker& local = bt.cast<BatchTracker>();
            const CircleEdgeEffect& circleEffect = gp.cast<CircleEdgeEffect>();
            uint16_t key = circleEffect.isStroked() ? 0x1 : 0x0;
            key |= local.fUsesLocalCoords && gp.localMatrix().hasPerspective() ? 0x2 : 0x0;
            key |= ComputePosKey(gp.viewMatrix()) << 2;
            b->add32(key << 16 | local.fInputColorType);
        }

        virtual void setData(const GrGLProgramDataManager& pdman,
                             const GrPrimitiveProcessor& gp,
                             const GrBatchTracker& bt) SK_OVERRIDE {
            this->setUniformViewMatrix(pdman, gp.viewMatrix());

            const BatchTracker& local = bt.cast<BatchTracker>();
            if (kUniform_GrGPInput == local.fInputColorType && local.fColor != fColor) {
                GrGLfloat c[4];
                GrColorToRGBAFloat(local.fColor, c);
                pdman.set4fv(fColorUniform, 1, c);
                fColor = local.fColor;
            }
        }

    private:
        GrColor fColor;
        UniformHandle fColorUniform;
        typedef GrGLGeometryProcessor INHERITED;
    };

    virtual void getGLProcessorKey(const GrBatchTracker& bt,
                                   const GrGLCaps& caps,
                                   GrProcessorKeyBuilder* b) const SK_OVERRIDE {
        GLProcessor::GenKey(*this, bt, caps, b);
    }

    virtual GrGLPrimitiveProcessor* createGLInstance(const GrBatchTracker& bt,
                                                     const GrGLCaps&) const SK_OVERRIDE {
        return SkNEW_ARGS(GLProcessor, (*this, bt));
    }

    void initBatchTracker(GrBatchTracker* bt, const GrPipelineInfo& init) const SK_OVERRIDE {
        BatchTracker* local = bt->cast<BatchTracker>();
        // Pass true as we have color attribute part of the vertex.
        local->fInputColorType = GetColorInputType(&local->fColor, this->color(), init, true);
        local->fUsesLocalCoords = init.fUsesLocalCoords;
    }

    bool onCanMakeEqual(const GrBatchTracker& m,
                        const GrGeometryProcessor& that,
                        const GrBatchTracker& t) const SK_OVERRIDE {
        const BatchTracker& mine = m.cast<BatchTracker>();
        const BatchTracker& theirs = t.cast<BatchTracker>();
        return CanCombineLocalMatrices(*this, mine.fUsesLocalCoords,
                                       that, theirs.fUsesLocalCoords) &&
               CanCombineOutput(mine.fInputColorType, mine.fColor,
                                theirs.fInputColorType, theirs.fColor);
    }

private:
    CircleEdgeEffect(GrColor color, bool stroke, const SkMatrix& localMatrix, bool useLocalCoords)
        : INHERITED(color, SkMatrix::I(), localMatrix) {
        this->initClassID<CircleEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType));
        fInCircleEdge = &this->addVertexAttrib(Attribute("inCircleEdge",
                                                           kVec4f_GrVertexAttribType));
        fInCircleColor = &this->addVertexAttrib(Attribute("inCircleColor",
                                                           kVec4ub_GrVertexAttribType));
        this->setHasVertexColor();
        fInLocalCoords = NULL;
        if (useLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasLocalCoords();
        }

        fStroke = stroke;
    }

    bool onIsEqual(const GrGeometryProcessor& other) const SK_OVERRIDE {
        const CircleEdgeEffect& cee = other.cast<CircleEdgeEffect>();
        return cee.fStroke == fStroke;
    }

    void onGetInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    struct BatchTracker {
        GrGPInput fInputColorType;
        GrColor fColor;
        bool fUsesLocalCoords;
    };

    const Attribute* fInPosition;
    const Attribute* fInCircleEdge;
    const Attribute* fInCircleColor;
    const Attribute* fInLocalCoords;
    bool fStroke;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(CircleEdgeEffect);

GrGeometryProcessor* CircleEdgeEffect::TestCreate(SkRandom* random,
                                                  GrContext* context,
                                                  const GrDrawTargetCaps&,
                                                  GrTexture* textures[]) {
    return CircleEdgeEffect::Create(GrRandomColor(random),
                                    random->nextBool(),
                                    GrProcessorUnitTest::TestMatrix(random),
                                    false);
}

///////////////////////////////////////////////////////////////////////////////

/**
 * The output of this effect is a modulation of the input color and coverage for an axis-aligned
 * ellipse, specified as a 2D offset from center, and the reciprocals of the outer and inner radii,
 * in both x and y directions.
 *
 * We are using an implicit function of x^2/a^2 + y^2/b^2 - 1 = 0.
 */

class EllipseEdgeEffect : public GrGeometryProcessor {
public:
    static GrGeometryProcessor* Create(GrColor color, bool stroke, const SkMatrix& localMatrix, bool useLocalCoords) {
        return SkNEW_ARGS(EllipseEdgeEffect, (color, stroke, localMatrix, useLocalCoords));
    }

    virtual ~EllipseEdgeEffect() {}

    const char* name() const SK_OVERRIDE { return "EllipseEdge"; }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inEllipseOffset() const { return fInEllipseOffset; }
    const Attribute* inEllipseRadii() const { return fInEllipseRadii; }
    const Attribute* inEllipseColor() const { return fInEllipseColor; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }

    inline bool isStroked() const { return fStroke; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor(const GrGeometryProcessor&,
                    const GrBatchTracker&)
            : fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) SK_OVERRIDE{
            const EllipseEdgeEffect& ee = args.fGP.cast<EllipseEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
            const BatchTracker& local = args.fBT.cast<BatchTracker>();
            GrGLVertexBuilder* vsBuilder = args.fPB->getVertexShaderBuilder();

            // emit attributes
            vsBuilder->emitAttributes(ee);

            GrGLVertToFrag ellipseOffsets(kVec2f_GrSLType);
            args.fPB->addVarying("EllipseOffsets", &ellipseOffsets);
            vsBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut(),
                                   ee.inEllipseOffset()->fName);

            GrGLVertToFrag ellipseRadii(kVec4f_GrSLType);
            args.fPB->addVarying("EllipseRadii", &ellipseRadii);
            vsBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut(),
                                   ee.inEllipseRadii()->fName);

            // Setup pass through color
            this->setupColorPassThrough(pb, local.fInputColorType, args.fOutputColor, ee.inEllipseColor(),
                                        &fColorUniform);

            // Setup position
            this->setupPosition(pb, gpArgs, ee.inPosition()->fName, ee.viewMatrix());

            if (ee.inLocalCoords()) {
                // emit transforms with explicit local coords
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inLocalCoords()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            } else {
                // emit transforms with position
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inPosition()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            }

            // for outer curve
            GrGLGPFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
            fsBuilder->codeAppendf("vec2 scaledOffset = %s*%s.xy;", ellipseOffsets.fsIn(),
                                   ellipseRadii.fsIn());
            fsBuilder->codeAppend("float test = dot(scaledOffset, scaledOffset) - 1.0;");
            fsBuilder->codeAppendf("vec2 grad = 2.0*scaledOffset*%s.xy;", ellipseRadii.fsIn());
            fsBuilder->codeAppend("float grad_dot = dot(grad, grad);");

            // avoid calling inversesqrt on zero.
            fsBuilder->codeAppend("grad_dot = max(grad_dot, 1.0e-4);");
            fsBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
            fsBuilder->codeAppend("float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");

            // for inner curve
            if (ee.isStroked()) {
                fsBuilder->codeAppendf("scaledOffset = %s*%s.zw;",
                                       ellipseOffsets.fsIn(), ellipseRadii.fsIn());
                fsBuilder->codeAppend("test = dot(scaledOffset, scaledOffset) - 1.0;");
                fsBuilder->codeAppendf("grad = 2.0*scaledOffset*%s.zw;",
                                       ellipseRadii.fsIn());
                fsBuilder->codeAppend("invlen = inversesqrt(dot(grad, grad));");
                fsBuilder->codeAppend("edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);");
            }

            fsBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.fOutputCoverage);
        }

        static void GenKey(const GrGeometryProcessor& gp,
                           const GrBatchTracker& bt,
                           const GrGLCaps&,
                           GrProcessorKeyBuilder* b) {
            const BatchTracker& local = bt.cast<BatchTracker>();
            const EllipseEdgeEffect& ellipseEffect = gp.cast<EllipseEdgeEffect>();
            uint16_t key = ellipseEffect.isStroked() ? 0x1 : 0x0;
            key |= local.fUsesLocalCoords && gp.localMatrix().hasPerspective() ? 0x2 : 0x0;
            key |= ComputePosKey(gp.viewMatrix()) << 2;
            b->add32(key << 16 | local.fInputColorType);
        }

        virtual void setData(const GrGLProgramDataManager& pdman,
                             const GrPrimitiveProcessor& gp,
                             const GrBatchTracker& bt) SK_OVERRIDE {
            this->setUniformViewMatrix(pdman, gp.viewMatrix());

            const BatchTracker& local = bt.cast<BatchTracker>();
            if (kUniform_GrGPInput == local.fInputColorType && local.fColor != fColor) {
                GrGLfloat c[4];
                GrColorToRGBAFloat(local.fColor, c);
                pdman.set4fv(fColorUniform, 1, c);
                fColor = local.fColor;
            }
        }

    private:
        GrColor fColor;
        UniformHandle fColorUniform;

        typedef GrGLGeometryProcessor INHERITED;
    };

    virtual void getGLProcessorKey(const GrBatchTracker& bt,
                                   const GrGLCaps& caps,
                                   GrProcessorKeyBuilder* b) const SK_OVERRIDE {
        GLProcessor::GenKey(*this, bt, caps, b);
    }

    virtual GrGLPrimitiveProcessor* createGLInstance(const GrBatchTracker& bt,
                                                     const GrGLCaps&) const SK_OVERRIDE {
        return SkNEW_ARGS(GLProcessor, (*this, bt));
    }

    void initBatchTracker(GrBatchTracker* bt, const GrPipelineInfo& init) const SK_OVERRIDE {
        BatchTracker* local = bt->cast<BatchTracker>();
        // Pass true as we have color attribute part of the vertex.
        local->fInputColorType = GetColorInputType(&local->fColor, this->color(), init, true);
        local->fUsesLocalCoords = init.fUsesLocalCoords;
    }

    bool onCanMakeEqual(const GrBatchTracker& m,
                        const GrGeometryProcessor& that,
                        const GrBatchTracker& t) const SK_OVERRIDE {
        const BatchTracker& mine = m.cast<BatchTracker>();
        const BatchTracker& theirs = t.cast<BatchTracker>();
        return CanCombineLocalMatrices(*this, mine.fUsesLocalCoords,
                                       that, theirs.fUsesLocalCoords) &&
               CanCombineOutput(mine.fInputColorType, mine.fColor,
                                theirs.fInputColorType, theirs.fColor);
    }

private:
    EllipseEdgeEffect(GrColor color, bool stroke, const SkMatrix& localMatrix, bool useLocalCoords)
        : INHERITED(color, SkMatrix::I(), localMatrix) {
        this->initClassID<EllipseEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType));
        fInEllipseOffset = &this->addVertexAttrib(Attribute("inEllipseOffset",
                                                              kVec2f_GrVertexAttribType));
        fInEllipseRadii = &this->addVertexAttrib(Attribute("inEllipseRadii",
                                                             kVec4f_GrVertexAttribType));
        fInEllipseColor = &this->addVertexAttrib(Attribute("inEllipseColor",
                                                            kVec4ub_GrVertexAttribType));
        this->setHasVertexColor();
        fInLocalCoords = NULL;
        if (useLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasLocalCoords();
        }

        fStroke = stroke;
    }

    bool onIsEqual(const GrGeometryProcessor& other) const SK_OVERRIDE {
        const EllipseEdgeEffect& eee = other.cast<EllipseEdgeEffect>();
        return eee.fStroke == fStroke;
    }

    void onGetInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    struct BatchTracker {
        GrGPInput fInputColorType;
        GrColor fColor;
        bool fUsesLocalCoords;
    };

    const Attribute* fInPosition;
    const Attribute* fInEllipseOffset;
    const Attribute* fInEllipseRadii;
    const Attribute* fInEllipseColor;
    const Attribute* fInLocalCoords;
    bool fStroke;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(EllipseEdgeEffect);

GrGeometryProcessor* EllipseEdgeEffect::TestCreate(SkRandom* random,
                                                   GrContext* context,
                                                   const GrDrawTargetCaps&,
                                                   GrTexture* textures[]) {
    return EllipseEdgeEffect::Create(GrRandomColor(random),
                                     random->nextBool(),
                                     GrProcessorUnitTest::TestMatrix(random),
                                     false);
}

///////////////////////////////////////////////////////////////////////////////

/**
 * The output of this effect is a modulation of the input color and coverage for an ellipse,
 * specified as a 2D offset from center for both the outer and inner paths (if stroked). The
 * implict equation used is for a unit circle (x^2 + y^2 - 1 = 0) and the edge corrected by
 * using differentials.
 *
 * The result is device-independent and can be used with any affine matrix.
 */

class DIEllipseEdgeEffect : public GrGeometryProcessor {
public:
    enum Mode { kStroke = 0, kHairline, kFill };

    static GrGeometryProcessor* Create(GrColor color, const SkMatrix& viewMatrix, const SkMatrix& localMatrix, Mode mode, bool useLocalCoords) {
        return SkNEW_ARGS(DIEllipseEdgeEffect, (color, viewMatrix, localMatrix, mode, useLocalCoords));
    }

    virtual ~DIEllipseEdgeEffect() {}

    const char* name() const SK_OVERRIDE { return "DIEllipseEdge"; }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inEllipseOffsets0() const { return fInEllipseOffsets0; }
    const Attribute* inEllipseOffsets1() const { return fInEllipseOffsets1; }
    const Attribute* inEllipseColor() const { return fInEllipseColor; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }

    inline Mode getMode() const { return fMode; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor(const GrGeometryProcessor&,
                    const GrBatchTracker&)
            : fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) SK_OVERRIDE{
            const DIEllipseEdgeEffect& ee = args.fGP.cast<DIEllipseEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
            const BatchTracker& local = args.fBT.cast<BatchTracker>();
            GrGLVertexBuilder* vsBuilder = args.fPB->getVertexShaderBuilder();

            // emit attributes
            vsBuilder->emitAttributes(ee);

            GrGLVertToFrag offsets0(kVec2f_GrSLType);
            args.fPB->addVarying("EllipseOffsets0", &offsets0);
            vsBuilder->codeAppendf("%s = %s;", offsets0.vsOut(),
                                   ee.inEllipseOffsets0()->fName);

            GrGLVertToFrag offsets1(kVec2f_GrSLType);
            args.fPB->addVarying("EllipseOffsets1", &offsets1);
            vsBuilder->codeAppendf("%s = %s;", offsets1.vsOut(),
                                   ee.inEllipseOffsets1()->fName);

            // Setup pass through color
            this->setupColorPassThrough(pb, local.fInputColorType, args.fOutputColor, ee.inEllipseColor(),
                                        &fColorUniform);

            // Setup position
            this->setupPosition(pb, gpArgs, ee.inPosition()->fName, ee.viewMatrix());

            if (ee.inLocalCoords()) {
                // emit transforms with explicit local coords
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inLocalCoords()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            } else {
                // emit transforms with position
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inPosition()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            }

            GrGLGPFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
            SkAssertResult(fsBuilder->enableFeature(
                    GrGLFragmentShaderBuilder::kStandardDerivatives_GLSLFeature));
            // for outer curve
            fsBuilder->codeAppendf("vec2 scaledOffset = %s.xy;", offsets0.fsIn());
            fsBuilder->codeAppend("float test = dot(scaledOffset, scaledOffset) - 1.0;");
            fsBuilder->codeAppendf("vec2 duvdx = dFdx(%s);", offsets0.fsIn());
            fsBuilder->codeAppendf("vec2 duvdy = dFdy(%s);", offsets0.fsIn());
            fsBuilder->codeAppendf("vec2 grad = vec2(2.0*%s.x*duvdx.x + 2.0*%s.y*duvdx.y,"
                                   "                 2.0*%s.x*duvdy.x + 2.0*%s.y*duvdy.y);",
                                   offsets0.fsIn(), offsets0.fsIn(), offsets0.fsIn(), offsets0.fsIn());

            fsBuilder->codeAppend("float grad_dot = dot(grad, grad);");
            // avoid calling inversesqrt on zero.
            fsBuilder->codeAppend("grad_dot = max(grad_dot, 1.0e-4);");
            fsBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
            if (kHairline == ee.getMode()) {
                // can probably do this with one step
                fsBuilder->codeAppend("float edgeAlpha = clamp(1.0-test*invlen, 0.0, 1.0);");
                fsBuilder->codeAppend("edgeAlpha *= clamp(1.0+test*invlen, 0.0, 1.0);");
            } else {
                fsBuilder->codeAppend("float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");
            }

            // for inner curve
            if (kStroke == ee.getMode()) {
                fsBuilder->codeAppendf("scaledOffset = %s.xy;", offsets1.fsIn());
                fsBuilder->codeAppend("test = dot(scaledOffset, scaledOffset) - 1.0;");
                fsBuilder->codeAppendf("duvdx = dFdx(%s);", offsets1.fsIn());
                fsBuilder->codeAppendf("duvdy = dFdy(%s);", offsets1.fsIn());
                fsBuilder->codeAppendf("grad = vec2(2.0*%s.x*duvdx.x + 2.0*%s.y*duvdx.y,"
                                       "            2.0*%s.x*duvdy.x + 2.0*%s.y*duvdy.y);",
                                       offsets1.fsIn(), offsets1.fsIn(), offsets1.fsIn(),
                                       offsets1.fsIn());
                fsBuilder->codeAppend("invlen = inversesqrt(dot(grad, grad));");
                fsBuilder->codeAppend("edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);");
            }

            fsBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.fOutputCoverage);
        }

        static void GenKey(const GrGeometryProcessor& gp,
                           const GrBatchTracker& bt,
                           const GrGLCaps&,
                           GrProcessorKeyBuilder* b) {
            const BatchTracker& local = bt.cast<BatchTracker>();
            const DIEllipseEdgeEffect& ellipseEffect = gp.cast<DIEllipseEdgeEffect>();
            uint16_t key = ellipseEffect.getMode();
            key |= local.fUsesLocalCoords && gp.localMatrix().hasPerspective() ? 0x1 << 8 : 0x0;
            key |= ComputePosKey(gp.viewMatrix()) << 9;
            b->add32(key << 16 | local.fInputColorType);
        }

        virtual void setData(const GrGLProgramDataManager& pdman,
                             const GrPrimitiveProcessor& gp,
                             const GrBatchTracker& bt) SK_OVERRIDE {
            this->setUniformViewMatrix(pdman, gp.viewMatrix());

            const BatchTracker& local = bt.cast<BatchTracker>();
            if (kUniform_GrGPInput == local.fInputColorType && local.fColor != fColor) {
                GrGLfloat c[4];
                GrColorToRGBAFloat(local.fColor, c);
                pdman.set4fv(fColorUniform, 1, c);
                fColor = local.fColor;
            }
        }

    private:
        GrColor fColor;
        UniformHandle fColorUniform;

        typedef GrGLGeometryProcessor INHERITED;
    };

    virtual void getGLProcessorKey(const GrBatchTracker& bt,
                                   const GrGLCaps& caps,
                                   GrProcessorKeyBuilder* b) const SK_OVERRIDE {
        GLProcessor::GenKey(*this, bt, caps, b);
    }

    virtual GrGLPrimitiveProcessor* createGLInstance(const GrBatchTracker& bt,
                                                     const GrGLCaps&) const SK_OVERRIDE {
        return SkNEW_ARGS(GLProcessor, (*this, bt));
    }

    void initBatchTracker(GrBatchTracker* bt, const GrPipelineInfo& init) const SK_OVERRIDE {
        BatchTracker* local = bt->cast<BatchTracker>();
        // Pass true as we have color attribute part of the vertex.
        local->fInputColorType = GetColorInputType(&local->fColor, this->color(), init, true);
        local->fUsesLocalCoords = init.fUsesLocalCoords;
    }

    bool onCanMakeEqual(const GrBatchTracker& m,
                        const GrGeometryProcessor& that,
                        const GrBatchTracker& t) const SK_OVERRIDE {
        const BatchTracker& mine = m.cast<BatchTracker>();
        const BatchTracker& theirs = t.cast<BatchTracker>();
        return CanCombineLocalMatrices(*this, mine.fUsesLocalCoords,
                                       that, theirs.fUsesLocalCoords) &&
               CanCombineOutput(mine.fInputColorType, mine.fColor,
                                theirs.fInputColorType, theirs.fColor);
    }

private:
    DIEllipseEdgeEffect(GrColor color, const SkMatrix& viewMatrix, const SkMatrix& localMatrix, Mode mode, bool useLocalCoords)
        : INHERITED(color, viewMatrix, localMatrix) {
        this->initClassID<DIEllipseEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType));
        fInEllipseOffsets0 = &this->addVertexAttrib(Attribute("inEllipseOffsets0",
                                                                kVec2f_GrVertexAttribType));
        fInEllipseOffsets1 = &this->addVertexAttrib(Attribute("inEllipseOffsets1",
                                                                kVec2f_GrVertexAttribType));
        fInEllipseColor = &this->addVertexAttrib(Attribute("inEllipseColor",
                                                            kVec4ub_GrVertexAttribType));
        this->setHasVertexColor();
        fInLocalCoords = NULL;
        if (useLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasLocalCoords();
        }

        fMode = mode;
    }

    bool onIsEqual(const GrGeometryProcessor& other) const SK_OVERRIDE {
        const DIEllipseEdgeEffect& eee = other.cast<DIEllipseEdgeEffect>();
        return eee.fMode == fMode;
    }

    void onGetInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    struct BatchTracker {
        GrGPInput fInputColorType;
        GrColor fColor;
        bool fUsesLocalCoords;
    };

    const Attribute* fInPosition;
    const Attribute* fInEllipseOffsets0;
    const Attribute* fInEllipseOffsets1;
    const Attribute* fInEllipseColor;
    const Attribute* fInLocalCoords;
    Mode fMode;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(DIEllipseEdgeEffect);

GrGeometryProcessor* DIEllipseEdgeEffect::TestCreate(SkRandom* random,
                                                     GrContext* context,
                                                     const GrDrawTargetCaps&,
                                                     GrTexture* textures[]) {
    return DIEllipseEdgeEffect::Create(GrRandomColor(random),
                                       GrProcessorUnitTest::TestMatrix(random),
                                       GrProcessorUnitTest::TestMatrix(random),
                                       (Mode)(random->nextRangeU(0,2)),
                                       false);
}

///////////////////////////////////////////////////////////////////////////////

void GrOvalRenderer::reset() {
    SkSafeSetNull(fFillRRectIndexBuffer);
    SkSafeSetNull(fStrokeRRectIndexBuffer);
    SkSafeSetNull(fOvalIndexBuffer);
}

bool GrOvalRenderer::drawOval(GrDrawTarget* target,
                              GrPipelineBuilder* pipelineBuilder,
                              GrColor color,
                              const SkMatrix& viewMatrix,
                              bool useAA,
                              const SkRect& oval,
                              const SkStrokeRec& stroke)
{
    bool useCoverageAA = useAA &&
        pipelineBuilder->canUseFracCoveragePrimProc(color, *target->caps());

    if (useCoverageAA) {
        if (pipelineBuilder->getRenderTarget()->isMultisampled()
            && !target->caps()->useOvalRendererForMSAA())
            useCoverageAA = false;
    }

    if (!useCoverageAA) {
        return false;
    }

    // we can draw circles
    if (SkScalarNearlyEqual(oval.width(), oval.height()) && circle_stays_circle(viewMatrix)) {
        this->drawCircle(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval, stroke);
    // if we have shader derivative support, render as device-independent
    } else if (target->caps()->shaderDerivativeSupport()) {
        return this->drawDIEllipse(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval,
                                   stroke);
    // otherwise axis-aligned ellipses only
    } else if (viewMatrix.rectStaysRect()) {
        return this->drawEllipse(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval,
                                 stroke);
    } else {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////

class CircleBatch : public GrBatch {
public:
    struct Geometry {
        GrColor fColor;
        SkMatrix fViewMatrix;
        SkScalar fInnerRadius;
        SkScalar fOuterRadius;
        bool fStroke;
        SkRect fDevBounds;
    };

    static GrBatch* Create(const Geometry& geometry) {
        return SkNEW_ARGS(CircleBatch, (geometry));
    }

    const char* name() const SK_OVERRIDE { return "CircleBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const SK_OVERRIDE {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }

    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    void initBatchTracker(const GrPipelineInfo& init) SK_OVERRIDE {
        // Handle any color overrides
        if (init.fColorIgnored) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        } else if (GrColor_ILLEGAL != init.fOverrideColor) {
            fGeoData[0].fColor = init.fOverrideColor;
        }

        // setup batch properties
        fBatch.fColorIgnored = init.fColorIgnored;
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = init.fUsesLocalCoords;
        fBatch.fCoverageIgnored = init.fCoverageIgnored;
    }

    void generateGeometry(GrBatchTarget* batchTarget, const GrPipeline* pipeline) SK_OVERRIDE {
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        // Setup geometry processor
        SkAutoTUnref<GrGeometryProcessor> gp(CircleEdgeEffect::Create(this->color(),
                                                                      this->stroke(),
                                                                      invert,
                                                                      false));

        batchTarget->initDraw(gp, pipeline);

        // TODO this is hacky, but the only way we have to initialize the GP is to use the
        // GrPipelineInfo struct so we can generate the correct shader.  Once we have GrBatch
        // everywhere we can remove this nastiness
        GrPipelineInfo init;
        init.fColorIgnored = fBatch.fColorIgnored;
        init.fOverrideColor = GrColor_ILLEGAL;
        init.fCoverageIgnored = fBatch.fCoverageIgnored;
        init.fUsesLocalCoords = this->usesLocalCoords();
        gp->initBatchTracker(batchTarget->currentBatchTracker(), init);

        int instanceCount = fGeoData.count();
        int vertexCount = kVertsPerCircle * instanceCount;
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(CircleVertex));

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void *vertices = batchTarget->vertexPool()->makeSpace(vertexStride,
                                                              vertexCount,
                                                              &vertexBuffer,
                                                              &firstVertex);

        CircleVertex* verts = reinterpret_cast<CircleVertex*>(vertices);

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];

            SkScalar innerRadius = args.fInnerRadius;
            SkScalar outerRadius = args.fOuterRadius;

            const SkRect& bounds = args.fDevBounds;

            // The inner radius in the vertex data must be specified in normalized space.
            innerRadius = innerRadius / outerRadius;
            verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
            verts[0].fOffset = SkPoint::Make(-1, -1);
            verts[0].fOuterRadius = outerRadius;
            verts[0].fInnerRadius = innerRadius;

            verts[1].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
            verts[1].fOffset = SkPoint::Make(-1, 1);
            verts[1].fOuterRadius = outerRadius;
            verts[1].fInnerRadius = innerRadius;

            verts[2].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
            verts[2].fOffset = SkPoint::Make(1, 1);
            verts[2].fOuterRadius = outerRadius;
            verts[2].fInnerRadius = innerRadius;

            verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
            verts[3].fOffset = SkPoint::Make(1, -1);
            verts[3].fOuterRadius = outerRadius;
            verts[3].fInnerRadius = innerRadius;

            verts += kVertsPerCircle;
        }

        const GrIndexBuffer* quadIndexBuffer = batchTarget->quadIndexBuffer();

        GrDrawTarget::DrawInfo drawInfo;
        drawInfo.setPrimitiveType(kTriangles_GrPrimitiveType);
        drawInfo.setStartVertex(0);
        drawInfo.setStartIndex(0);
        drawInfo.setVerticesPerInstance(kVertsPerCircle);
        drawInfo.setIndicesPerInstance(kIndicesPerCircle);
        drawInfo.adjustStartVertex(firstVertex);
        drawInfo.setVertexBuffer(vertexBuffer);
        drawInfo.setIndexBuffer(quadIndexBuffer);

        int maxInstancesPerDraw = quadIndexBuffer->maxQuads();

        while (instanceCount) {
            drawInfo.setInstanceCount(SkTMin(instanceCount, maxInstancesPerDraw));
            drawInfo.setVertexCount(drawInfo.instanceCount() * drawInfo.verticesPerInstance());
            drawInfo.setIndexCount(drawInfo.instanceCount() * drawInfo.indicesPerInstance());

            batchTarget->draw(drawInfo);

            drawInfo.setStartVertex(drawInfo.startVertex() + drawInfo.vertexCount());
            instanceCount -= drawInfo.instanceCount();
        }
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    CircleBatch(const Geometry& geometry) {
        this->initClassID<CircleBatch>();
        fGeoData.push_back(geometry);
    }

    bool onCombineIfPossible(GrBatch* t) SK_OVERRIDE {
        CircleBatch* that = t->cast<CircleBatch>();

        // TODO use vertex color to avoid breaking batches
        if (this->color() != that->color()) {
            return false;
        }

        if (this->stroke() != that->stroke()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool stroke() const { return fBatch.fStroke; }

    struct BatchTracker {
        GrColor fColor;
        bool fStroke;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    static const int kVertsPerCircle = 4;
    static const int kIndicesPerCircle = 6;

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;
};

void GrOvalRenderer::drawCircle(GrDrawTarget* target,
                                GrPipelineBuilder* pipelineBuilder,
                                GrColor color,
                                const SkMatrix& viewMatrix,
                                bool useCoverageAA,
                                const SkRect& circle,
                                const SkStrokeRec& stroke) {
    bool useLocalCoord = false;
    SkMatrix localMatrix;

    SkPoint center = SkPoint::Make(circle.centerX(), circle.centerY());
    viewMatrix.mapPoints(&center, 1);
    SkScalar radius = viewMatrix.mapRadius(SkScalarHalf(circle.width()));
    SkScalar strokeWidth = viewMatrix.mapRadius(stroke.getWidth());

    SkScalar localStrokeWidth = stroke.getWidth();
    SkScalar localRadius = SkScalarHalf(circle.width());

    SkStrokeRec::Style style = stroke.getStyle();
    bool isStrokeOnly = SkStrokeRec::kStroke_Style == style ||
                        SkStrokeRec::kHairline_Style == style;
    bool hasStroke = isStrokeOnly || SkStrokeRec::kStrokeAndFill_Style == style;

    // use local coords for shader that is a bitmap
    if (pipelineBuilder->canOptimizeForBitmapShader()) {
        const SkMatrix& lm = pipelineBuilder->getLocalMatrix();
        GrPipelineBuilder::AutoLocalMatrixChange almc;
        almc.set(pipelineBuilder);
        useLocalCoord = true;
        localMatrix = lm;
    }

    SkScalar innerRadius = 0.0f;
    SkScalar outerRadius = radius;
    SkScalar halfWidth = 0;

    SkScalar localHalfWidth = 0;
    SkScalar localOuterRadius = localRadius;

    if (hasStroke) {
        if (SkScalarNearlyZero(strokeWidth)) {
            halfWidth = SK_ScalarHalf;
            localHalfWidth = SK_ScalarHalf;
        } else {
            halfWidth = SkScalarHalf(strokeWidth);
            localHalfWidth = SkScalarHalf(localStrokeWidth);
        }

        outerRadius += halfWidth;
        localOuterRadius += localHalfWidth;
        if (isStrokeOnly) {
            innerRadius = radius - halfWidth;
        }
    }

    SkMatrix invert;
    if (!viewMatrix.invert(&invert)) {
        return;
    }

    GrContext* context = pipelineBuilder->getRenderTarget()->getContext();

    GrIndexBuffer *oindexBuffer = this->ovalIndexBuffer(context->getGpu());
    if (NULL == oindexBuffer) {
        SkDebugf("Failed to create index buffer for oval!\n");
        return;
    }

    GrDrawTarget::AutoReleaseGeometry arg;
    GrPipelineBuilder::AutoRestoreEffects are(pipelineBuilder);

    // The radii are outset for two reasons. First, it allows the shader to simply perform simpler
    // computation because the computed alpha is zero, rather than 50%, at the radius.
    // Second, the outer radius is used to compute the verts of the bounding box that is rendered
    // and the outset ensures the box will cover all partially covered by the circle.
    outerRadius += SK_ScalarHalf;
    innerRadius -= SK_ScalarHalf;
    localOuterRadius += SK_ScalarHalf;

    SkRect bounds = SkRect::MakeLTRB(
        center.fX - outerRadius,
        center.fY - outerRadius,
        center.fX + outerRadius,
        center.fY + outerRadius
    );

    SkRect localBounds = SkRect::MakeLTRB(
        circle.centerX() - localOuterRadius,
        circle.centerY() - localOuterRadius,
        circle.centerX() + localOuterRadius,
        circle.centerY() + localOuterRadius
    );
    GrGeometryProcessor* gp;
    if (useLocalCoord) {
        gp = (CircleEdgeEffect::Create(color,
                                       isStrokeOnly && innerRadius > 0,
                                       pipelineBuilder->getLocalMatrix(),
                                       true));
        if (!arg.set(target, 4, gp->getVertexStride(), 0)) {
            SkDebugf("Failed to get space for vertices!\n");
            return;
        }
        CircleUVVertex* verts = reinterpret_cast<CircleUVVertex*>(arg.vertices());
        SkPoint pt;

        verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
        verts[0].fOffset = SkPoint::Make(-1,-1);
        verts[0].fOuterRadius = outerRadius;
        verts[0].fInnerRadius = innerRadius;
        verts[0].fColor = color;
        pt.fX = localBounds.fLeft;
        pt.fY = localBounds.fTop;
        localMatrix.mapPoints(&pt, 1);
        verts[0].fLocalPos = pt;

        verts[1].fPos = SkPoint::Make(bounds.fLeft, bounds.fBottom);
        verts[1].fOffset = SkPoint::Make(-1, 1);
        verts[1].fOuterRadius = outerRadius;
        verts[1].fInnerRadius = innerRadius;
        verts[1].fColor = color;
        pt.fX = localBounds.fLeft;
        pt.fY = localBounds.fBottom;
        localMatrix.mapPoints(&pt, 1);
        verts[1].fLocalPos = pt;

        verts[2].fPos = SkPoint::Make(bounds.fRight,  bounds.fTop);
        verts[2].fOffset = SkPoint::Make(1, -1);
        verts[2].fOuterRadius = outerRadius;
        verts[2].fInnerRadius = innerRadius;
        verts[2].fColor = color;
        pt.fX = localBounds.fRight;
        pt.fY = localBounds.fTop;
        localMatrix.mapPoints(&pt, 1);
        verts[2].fLocalPos = pt;

        verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
        verts[3].fOffset = SkPoint::Make(1, 1);
        verts[3].fOuterRadius = outerRadius;
        verts[3].fInnerRadius = innerRadius;
        verts[3].fColor = color;
        pt.fX = localBounds.fRight;
        pt.fY = localBounds.fBottom;
        localMatrix.mapPoints(&pt, 1);
        verts[3].fLocalPos = pt;
    } else {
        gp = (CircleEdgeEffect::Create(color,
                                       isStrokeOnly && innerRadius > 0,
                                       invert,
                                       false));
        if (!arg.set(target, 4, gp->getVertexStride(), 0)) {
            SkDebugf("Failed to get space for vertices!\n");
            return;
        }
	    CircleVertex* verts = reinterpret_cast<CircleVertex*>(arg.vertices());

        innerRadius = innerRadius / outerRadius;
        verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
        verts[0].fOffset = SkPoint::Make(-1, -1);
        verts[0].fOuterRadius = outerRadius;
        verts[0].fInnerRadius = innerRadius;
        verts[0].fColor = color;

        verts[1].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
        verts[1].fOffset = SkPoint::Make(-1, 1);
        verts[1].fOuterRadius = outerRadius;
        verts[1].fInnerRadius = innerRadius;
        verts[1].fColor = color;

        verts[2].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
        verts[2].fOffset = SkPoint::Make(1, -1);
        verts[2].fOuterRadius = outerRadius;
        verts[2].fInnerRadius = innerRadius;
        verts[2].fColor = color;

        verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
        verts[3].fOffset = SkPoint::Make(1, 1);
        verts[3].fOuterRadius = outerRadius;
        verts[3].fInnerRadius = innerRadius;
        verts[3].fColor = color;
	}
#if 0
    verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
    verts[0].fOffset = SkPoint::Make(-outerRadius, -outerRadius);
    verts[0].fOffset = SkPoint::Make(-1,-1);
    verts[0].fOuterRadius = outerRadius;
    verts[0].fInnerRadius = innerRadius;
    verts[0].fColor = color;

    verts[1].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
    verts[1].fOffset = SkPoint::Make(outerRadius, -outerRadius);
    verts[1].fOffset = SkPoint::Make(-1,1);
    verts[1].fOuterRadius = outerRadius;
    verts[1].fInnerRadius = innerRadius;
    verts[1].fColor = color;

    verts[2].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
    verts[2].fOffset = SkPoint::Make(-outerRadius, outerRadius);
    verts[2].fOffset = SkPoint::Make(1,1);
    verts[2].fOuterRadius = outerRadius;
    verts[2].fInnerRadius = innerRadius;
    verts[2].fColor = color;

    verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
    verts[3].fOffset = SkPoint::Make(outerRadius, outerRadius);
    verts[3].fOffset = SkPoint::Make(1,-1);
    verts[3].fOuterRadius = outerRadius;
    verts[3].fInnerRadius = innerRadius;
    verts[3].fColor = color;
#endif
    target->setIndexSourceToBuffer(oindexBuffer);
    target->drawIndexedInstances(pipelineBuilder, gp, kTriangles_GrPrimitiveType, 1, 4, 6, &bounds);
    gp->unref();
#if 0
    CircleBatch::Geometry geometry;
    geometry.fViewMatrix = viewMatrix;
    geometry.fColor = color;
    geometry.fInnerRadius = innerRadius;
    geometry.fOuterRadius = outerRadius;
    geometry.fStroke = isStrokeOnly && innerRadius > 0;
    geometry.fDevBounds = bounds;

    SkAutoTUnref<GrBatch> batch(CircleBatch::Create(geometry));
    target->drawBatch(pipelineBuilder, batch, &bounds);
#endif
}

///////////////////////////////////////////////////////////////////////////////

class EllipseBatch : public GrBatch {
public:
    struct Geometry {
        GrColor fColor;
        SkMatrix fViewMatrix;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        bool fStroke;
        SkRect fDevBounds;
    };

    static GrBatch* Create(const Geometry& geometry) {
        return SkNEW_ARGS(EllipseBatch, (geometry));
    }

    const char* name() const SK_OVERRIDE { return "EllipseBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const SK_OVERRIDE {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    void initBatchTracker(const GrPipelineInfo& init) SK_OVERRIDE {
        // Handle any color overrides
        if (init.fColorIgnored) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        } else if (GrColor_ILLEGAL != init.fOverrideColor) {
            fGeoData[0].fColor = init.fOverrideColor;
        }

        // setup batch properties
        fBatch.fColorIgnored = init.fColorIgnored;
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = init.fUsesLocalCoords;
        fBatch.fCoverageIgnored = init.fCoverageIgnored;
    }

    void generateGeometry(GrBatchTarget* batchTarget, const GrPipeline* pipeline) SK_OVERRIDE {
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        // Setup geometry processor
        SkAutoTUnref<GrGeometryProcessor> gp(EllipseEdgeEffect::Create(this->color(),
                                                                       this->stroke(),
                                                                       invert,
                                                                       false));

        batchTarget->initDraw(gp, pipeline);

        // TODO this is hacky, but the only way we have to initialize the GP is to use the
        // GrPipelineInfo struct so we can generate the correct shader.  Once we have GrBatch
        // everywhere we can remove this nastiness
        GrPipelineInfo init;
        init.fColorIgnored = fBatch.fColorIgnored;
        init.fOverrideColor = GrColor_ILLEGAL;
        init.fCoverageIgnored = fBatch.fCoverageIgnored;
        init.fUsesLocalCoords = this->usesLocalCoords();
        gp->initBatchTracker(batchTarget->currentBatchTracker(), init);

        int instanceCount = fGeoData.count();
        int vertexCount = kVertsPerEllipse * instanceCount;
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(EllipseVertex));

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void *vertices = batchTarget->vertexPool()->makeSpace(vertexStride,
                                                              vertexCount,
                                                              &vertexBuffer,
                                                              &firstVertex);

        EllipseVertex* verts = reinterpret_cast<EllipseVertex*>(vertices);

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];

            SkScalar xRadius = args.fXRadius;
            SkScalar yRadius = args.fYRadius;

            // Compute the reciprocals of the radii here to save time in the shader
            SkScalar xRadRecip = SkScalarInvert(xRadius);
            SkScalar yRadRecip = SkScalarInvert(yRadius);
            SkScalar xInnerRadRecip = SkScalarInvert(args.fInnerXRadius);
            SkScalar yInnerRadRecip = SkScalarInvert(args.fInnerYRadius);

            const SkRect& bounds = args.fDevBounds;

            // The inner radius in the vertex data must be specified in normalized space.
            verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
            verts[0].fOffset = SkPoint::Make(-xRadius, -yRadius);
            verts[0].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
            verts[0].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);

            verts[1].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
            verts[1].fOffset = SkPoint::Make(-xRadius, yRadius);
            verts[1].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
            verts[1].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);

            verts[2].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
            verts[2].fOffset = SkPoint::Make(xRadius, yRadius);
            verts[2].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
            verts[2].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);

            verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
            verts[3].fOffset = SkPoint::Make(xRadius, -yRadius);
            verts[3].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
            verts[3].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);

            verts += kVertsPerEllipse;
        }

        const GrIndexBuffer* quadIndexBuffer = batchTarget->quadIndexBuffer();

        GrDrawTarget::DrawInfo drawInfo;
        drawInfo.setPrimitiveType(kTriangles_GrPrimitiveType);
        drawInfo.setStartVertex(0);
        drawInfo.setStartIndex(0);
        drawInfo.setVerticesPerInstance(kVertsPerEllipse);
        drawInfo.setIndicesPerInstance(kIndicesPerEllipse);
        drawInfo.adjustStartVertex(firstVertex);
        drawInfo.setVertexBuffer(vertexBuffer);
        drawInfo.setIndexBuffer(quadIndexBuffer);

        int maxInstancesPerDraw = quadIndexBuffer->maxQuads();

        while (instanceCount) {
            drawInfo.setInstanceCount(SkTMin(instanceCount, maxInstancesPerDraw));
            drawInfo.setVertexCount(drawInfo.instanceCount() * drawInfo.verticesPerInstance());
            drawInfo.setIndexCount(drawInfo.instanceCount() * drawInfo.indicesPerInstance());

            batchTarget->draw(drawInfo);

            drawInfo.setStartVertex(drawInfo.startVertex() + drawInfo.vertexCount());
            instanceCount -= drawInfo.instanceCount();
        }
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    EllipseBatch(const Geometry& geometry) {
        this->initClassID<EllipseBatch>();
        fGeoData.push_back(geometry);
    }

    bool onCombineIfPossible(GrBatch* t) SK_OVERRIDE {
        EllipseBatch* that = t->cast<EllipseBatch>();

        // TODO use vertex color to avoid breaking batches
        if (this->color() != that->color()) {
            return false;
        }

        if (this->stroke() != that->stroke()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool stroke() const { return fBatch.fStroke; }

    struct BatchTracker {
        GrColor fColor;
        bool fStroke;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    static const int kVertsPerEllipse = 4;
    static const int kIndicesPerEllipse = 6;

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;
};

bool GrOvalRenderer::drawEllipse(GrDrawTarget* target,
                                 GrPipelineBuilder* pipelineBuilder,
                                 GrColor color,
                                 const SkMatrix& viewMatrix,
                                 bool useCoverageAA,
                                 const SkRect& ellipse,
                                 const SkStrokeRec& stroke) {
#ifdef SK_DEBUG
    {
        // we should have checked for this previously
        bool isAxisAlignedEllipse = viewMatrix.rectStaysRect();
        SkASSERT(useCoverageAA && isAxisAlignedEllipse);
    }
#endif

    // do any matrix crunching before we reset the draw state for device coords
    SkPoint center = SkPoint::Make(ellipse.centerX(), ellipse.centerY());
    viewMatrix.mapPoints(&center, 1);
    SkScalar ellipseXRadius = SkScalarHalf(ellipse.width());
    SkScalar ellipseYRadius = SkScalarHalf(ellipse.height());
    SkScalar xRadius = SkScalarAbs(viewMatrix[SkMatrix::kMScaleX]*ellipseXRadius +
                                   viewMatrix[SkMatrix::kMSkewY]*ellipseYRadius);
    SkScalar yRadius = SkScalarAbs(viewMatrix[SkMatrix::kMSkewX]*ellipseXRadius +
                                   viewMatrix[SkMatrix::kMScaleY]*ellipseYRadius);

    // do (potentially) anisotropic mapping of stroke
    SkVector scaledStroke;
    SkScalar strokeWidth = stroke.getWidth();
    scaledStroke.fX = SkScalarAbs(strokeWidth*(viewMatrix[SkMatrix::kMScaleX] +
                                               viewMatrix[SkMatrix::kMSkewY]));
    scaledStroke.fY = SkScalarAbs(strokeWidth*(viewMatrix[SkMatrix::kMSkewX] +
                                               viewMatrix[SkMatrix::kMScaleY]));

    SkStrokeRec::Style style = stroke.getStyle();
    bool isStrokeOnly = SkStrokeRec::kStroke_Style == style ||
                        SkStrokeRec::kHairline_Style == style;
    bool hasStroke = isStrokeOnly || SkStrokeRec::kStrokeAndFill_Style == style;
    GrContext* context = pipelineBuilder->getRenderTarget()->getContext();

    SkScalar innerXRadius = 0;
    SkScalar innerYRadius = 0;
    if (hasStroke) {
        if (SkScalarNearlyZero(scaledStroke.length())) {
            scaledStroke.set(SK_ScalarHalf, SK_ScalarHalf);
        } else {
            scaledStroke.scale(SK_ScalarHalf);
        }

        // we only handle thick strokes for near-circular ellipses
        if (scaledStroke.length() > SK_ScalarHalf &&
            (SK_ScalarHalf*xRadius > yRadius || SK_ScalarHalf*yRadius > xRadius)) {
            return false;
        }

        // we don't handle it if curvature of the stroke is less than curvature of the ellipse
        if (scaledStroke.fX*(yRadius*yRadius) < (scaledStroke.fY*scaledStroke.fY)*xRadius ||
            scaledStroke.fY*(xRadius*xRadius) < (scaledStroke.fX*scaledStroke.fX)*yRadius) {
            return false;
        }

        // this is legit only if scale & translation (which should be the case at the moment)
        if (isStrokeOnly) {
            innerXRadius = xRadius - scaledStroke.fX;
            innerYRadius = yRadius - scaledStroke.fY;
        }

        xRadius += scaledStroke.fX;
        yRadius += scaledStroke.fY;
    }

    SkMatrix invert;
    if (!viewMatrix.invert(&invert)) {
        return false;
    }

    GrDrawTarget::AutoReleaseGeometry arg;
    GrPipelineBuilder::AutoRestoreEffects are(pipelineBuilder);
    SkAutoTUnref<GrGeometryProcessor> gp(EllipseEdgeEffect::Create(color,
                                                                   innerXRadius > 0 && innerYRadius > 0,
                                                                   invert,
                                                                   false));
    if (!arg.set(target, 4, gp->getVertexStride(), 0)) {
        SkDebugf("Failed to get space for vertices!\n");
        return false;
    }

    // Compute the reciprocals of the radii here to save time in the shader
    SkScalar xRadRecip = SkScalarInvert(xRadius);
    SkScalar yRadRecip = SkScalarInvert(yRadius);
    SkScalar xInnerRadRecip = SkScalarInvert(innerXRadius);
    SkScalar yInnerRadRecip = SkScalarInvert(innerYRadius);

    EllipseVertex* verts = reinterpret_cast<EllipseVertex*>(arg.vertices());
    // We've extended the outer x radius out half a pixel to antialias.
    // This will also expand the rect so all the pixels will be captured.
    // TODO: Consider if we should use sqrt(2)/2 instead
    xRadius += SK_ScalarHalf;
    yRadius += SK_ScalarHalf;

    SkRect bounds = SkRect::MakeLTRB(
        center.fX - xRadius,
        center.fY - yRadius,
        center.fX + xRadius,
        center.fY + yRadius
    );

    verts[0].fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
    verts[0].fOffset = SkPoint::Make(-xRadius, -yRadius);
    verts[0].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
    verts[0].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
    verts[0].fColor = color;

    verts[1].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
    verts[1].fOffset = SkPoint::Make(-xRadius, yRadius);
    verts[1].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
    verts[1].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
    verts[1].fColor = color;

    verts[2].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
    verts[2].fOffset = SkPoint::Make(xRadius, yRadius);
    verts[2].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
    verts[2].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
    verts[2].fColor = color;

    verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
    verts[3].fOffset = SkPoint::Make(xRadius, -yRadius);
    verts[3].fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
    verts[3].fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
    verts[3].fColor = color;
    target->setIndexSourceToBuffer(context->getGpu()->getQuadIndexBuffer());
    target->drawIndexedInstances(pipelineBuilder, gp, kTriangles_GrPrimitiveType, 1, 4, 6, &bounds);
    target->resetIndexSource();

#if 0
    EllipseBatch::Geometry geometry;
    geometry.fViewMatrix = viewMatrix;
    geometry.fColor = color;
    geometry.fXRadius = xRadius;
    geometry.fYRadius = yRadius;
    geometry.fInnerXRadius = innerXRadius;
    geometry.fInnerYRadius = innerYRadius;
    geometry.fStroke = isStrokeOnly && innerXRadius > 0 && innerYRadius > 0;
    geometry.fDevBounds = bounds;

    SkAutoTUnref<GrBatch> batch(EllipseBatch::Create(geometry));
    target->drawBatch(pipelineBuilder, batch, &bounds);
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

class DIEllipseBatch : public GrBatch {
public:
    struct Geometry {
        GrColor fColor;
        SkMatrix fViewMatrix;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        SkScalar fGeoDx;
        SkScalar fGeoDy;
        DIEllipseEdgeEffect::Mode fMode;
        SkRect fDevBounds;
    };

    static GrBatch* Create(const Geometry& geometry) {
        return SkNEW_ARGS(DIEllipseBatch, (geometry));
    }

    const char* name() const SK_OVERRIDE { return "DIEllipseBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const SK_OVERRIDE {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    void initBatchTracker(const GrPipelineInfo& init) SK_OVERRIDE {
        // Handle any color overrides
        if (init.fColorIgnored) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        } else if (GrColor_ILLEGAL != init.fOverrideColor) {
            fGeoData[0].fColor = init.fOverrideColor;
        }

        // setup batch properties
        fBatch.fColorIgnored = init.fColorIgnored;
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fMode = fGeoData[0].fMode;
        fBatch.fUsesLocalCoords = init.fUsesLocalCoords;
        fBatch.fCoverageIgnored = init.fCoverageIgnored;
    }

    void generateGeometry(GrBatchTarget* batchTarget, const GrPipeline* pipeline) SK_OVERRIDE {
        // Setup geometry processor
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        SkAutoTUnref<GrGeometryProcessor> gp(DIEllipseEdgeEffect::Create(this->color(),
                                                                         this->viewMatrix(),
                                                                         invert,
                                                                         this->mode(),
                                                                         false));

        batchTarget->initDraw(gp, pipeline);

        // TODO this is hacky, but the only way we have to initialize the GP is to use the
        // GrPipelineInfo struct so we can generate the correct shader.  Once we have GrBatch
        // everywhere we can remove this nastiness
        GrPipelineInfo init;
        init.fColorIgnored = fBatch.fColorIgnored;
        init.fOverrideColor = GrColor_ILLEGAL;
        init.fCoverageIgnored = fBatch.fCoverageIgnored;
        init.fUsesLocalCoords = this->usesLocalCoords();
        gp->initBatchTracker(batchTarget->currentBatchTracker(), init);

        int instanceCount = fGeoData.count();
        int vertexCount = kVertsPerEllipse * instanceCount;
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(DIEllipseVertex));

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void *vertices = batchTarget->vertexPool()->makeSpace(vertexStride,
                                                              vertexCount,
                                                              &vertexBuffer,
                                                              &firstVertex);

        DIEllipseVertex* verts = reinterpret_cast<DIEllipseVertex*>(vertices);

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];

            SkScalar xRadius = args.fXRadius;
            SkScalar yRadius = args.fYRadius;

            const SkRect& bounds = args.fDevBounds;

            // This adjusts the "radius" to include the half-pixel border
            SkScalar offsetDx = SkScalarDiv(args.fGeoDx, xRadius);
            SkScalar offsetDy = SkScalarDiv(args.fGeoDy, yRadius);

            SkScalar innerRatioX = SkScalarDiv(xRadius, args.fInnerXRadius);
            SkScalar innerRatioY = SkScalarDiv(yRadius, args.fInnerYRadius);

            verts[0].fPos = SkPoint::Make(bounds.fLeft, bounds.fTop);
            verts[0].fOuterOffset = SkPoint::Make(-1.0f - offsetDx, -1.0f - offsetDy);
            verts[0].fInnerOffset = SkPoint::Make(-innerRatioX - offsetDx, -innerRatioY - offsetDy);

            verts[1].fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
            verts[1].fOuterOffset = SkPoint::Make(-1.0f - offsetDx, 1.0f + offsetDy);
            verts[1].fInnerOffset = SkPoint::Make(-innerRatioX - offsetDx, innerRatioY + offsetDy);

            verts[2].fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
            verts[2].fOuterOffset = SkPoint::Make(1.0f + offsetDx, 1.0f + offsetDy);
            verts[2].fInnerOffset = SkPoint::Make(innerRatioX + offsetDx, innerRatioY + offsetDy);

            verts[3].fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
            verts[3].fOuterOffset = SkPoint::Make(1.0f + offsetDx, -1.0f - offsetDy);
            verts[3].fInnerOffset = SkPoint::Make(innerRatioX + offsetDx, -innerRatioY - offsetDy);

            verts += kVertsPerEllipse;
        }

        const GrIndexBuffer* quadIndexBuffer = batchTarget->quadIndexBuffer();

        GrDrawTarget::DrawInfo drawInfo;
        drawInfo.setPrimitiveType(kTriangles_GrPrimitiveType);
        drawInfo.setStartVertex(0);
        drawInfo.setStartIndex(0);
        drawInfo.setVerticesPerInstance(kVertsPerEllipse);
        drawInfo.setIndicesPerInstance(kIndicesPerEllipse);
        drawInfo.adjustStartVertex(firstVertex);
        drawInfo.setVertexBuffer(vertexBuffer);
        drawInfo.setIndexBuffer(quadIndexBuffer);

        int maxInstancesPerDraw = quadIndexBuffer->maxQuads();

        while (instanceCount) {
            drawInfo.setInstanceCount(SkTMin(instanceCount, maxInstancesPerDraw));
            drawInfo.setVertexCount(drawInfo.instanceCount() * drawInfo.verticesPerInstance());
            drawInfo.setIndexCount(drawInfo.instanceCount() * drawInfo.indicesPerInstance());

            batchTarget->draw(drawInfo);

            drawInfo.setStartVertex(drawInfo.startVertex() + drawInfo.vertexCount());
            instanceCount -= drawInfo.instanceCount();
        }
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    DIEllipseBatch(const Geometry& geometry) {
        this->initClassID<DIEllipseBatch>();
        fGeoData.push_back(geometry);
    }

    bool onCombineIfPossible(GrBatch* t) SK_OVERRIDE {
        DIEllipseBatch* that = t->cast<DIEllipseBatch>();

        // TODO use vertex color to avoid breaking batches
        if (this->color() != that->color()) {
            return false;
        }

        if (this->mode() != that->mode()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    DIEllipseEdgeEffect::Mode mode() const { return fBatch.fMode; }

    struct BatchTracker {
        GrColor fColor;
        DIEllipseEdgeEffect::Mode fMode;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    static const int kVertsPerEllipse = 4;
    static const int kIndicesPerEllipse = 6;

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;
};

bool GrOvalRenderer::drawDIEllipse(GrDrawTarget* target,
                                   GrPipelineBuilder* pipelineBuilder,
                                   GrColor color,
                                   const SkMatrix& viewMatrix,
                                   bool useCoverageAA,
                                   const SkRect& ellipse,
                                   const SkStrokeRec& stroke) {
    const SkMatrix &vm = viewMatrix;
    SkMatrix localMatrix;
    bool useLocalCoord = false;

    SkPoint center = SkPoint::Make(ellipse.centerX(), ellipse.centerY());
    SkScalar xRadius = SkScalarHalf(ellipse.width());
    SkScalar yRadius = SkScalarHalf(ellipse.height());
    SkPoint localCenter = center;
    SkScalar xLocalRadius = xRadius;
    SkScalar yLocalRadius = yRadius;

    SkStrokeRec::Style style = stroke.getStyle();
    DIEllipseEdgeEffect::Mode mode = (SkStrokeRec::kStroke_Style == style) ?
                                    DIEllipseEdgeEffect::kStroke :
                                    (SkStrokeRec::kHairline_Style == style) ?
                                    DIEllipseEdgeEffect::kHairline : DIEllipseEdgeEffect::kFill;

    SkScalar innerXRadius = 0;
    SkScalar innerYRadius = 0;
    if (SkStrokeRec::kFill_Style != style && SkStrokeRec::kHairline_Style != style) {
        SkScalar strokeWidth = stroke.getWidth();

        if (SkScalarNearlyZero(strokeWidth)) {
            strokeWidth = SK_ScalarHalf;
        } else {
            strokeWidth *= SK_ScalarHalf;
        }

        // we only handle thick strokes for near-circular ellipses
        if (strokeWidth > SK_ScalarHalf &&
            (SK_ScalarHalf*xRadius > yRadius || SK_ScalarHalf*yRadius > xRadius)) {
            return false;
        }

        // we don't handle it if curvature of the stroke is less than curvature of the ellipse
        if (strokeWidth*(yRadius*yRadius) < (strokeWidth*strokeWidth)*xRadius ||
            strokeWidth*(xRadius*xRadius) < (strokeWidth*strokeWidth)*yRadius) {
            return false;
        }

        // set inner radius (if needed)
        if (SkStrokeRec::kStroke_Style == style) {
            innerXRadius = xRadius - strokeWidth;
            innerYRadius = yRadius - strokeWidth;
        }

        xRadius += strokeWidth;
        yRadius += strokeWidth;

        xLocalRadius += strokeWidth;
        yLocalRadius += strokeWidth;
    }

    // use local coords for shader that is a bitmap
    if (pipelineBuilder->canOptimizeForBitmapShader()) {
        const SkMatrix& lm = pipelineBuilder->getLocalMatrix();
        GrPipelineBuilder::AutoLocalMatrixChange almc;
        almc.set(pipelineBuilder);
        useLocalCoord = true;
        localMatrix = lm;
    }

    if (DIEllipseEdgeEffect::kStroke == mode) {
        mode = (innerXRadius > 0 && innerYRadius > 0) ? DIEllipseEdgeEffect::kStroke :
                                                        DIEllipseEdgeEffect::kFill;
    }

    GrContext* context = pipelineBuilder->getRenderTarget()->getContext();

    GrIndexBuffer *oindexBuffer = this->ovalIndexBuffer(context->getGpu());
    if (NULL == oindexBuffer) {
        SkDebugf("Failed to create index buffer for oval!\n");
        return false;
    }

    // This expands the outer rect so that after CTM we end up with a half-pixel border
    SkScalar a = vm[SkMatrix::kMScaleX];
    SkScalar b = vm[SkMatrix::kMSkewX];
    SkScalar c = vm[SkMatrix::kMSkewY];
    SkScalar d = vm[SkMatrix::kMScaleY];
    SkScalar geoDx = SkScalarDiv(SK_ScalarHalf, SkScalarSqrt(a*a + c*c));
    SkScalar geoDy = SkScalarDiv(SK_ScalarHalf, SkScalarSqrt(b*b + d*d));

    xLocalRadius += SK_ScalarHalf;
    yLocalRadius += SK_ScalarHalf;

    SkRect bounds = SkRect::MakeLTRB(
        center.fX - xRadius - geoDx,
        center.fY - yRadius - geoDy,
        center.fX + xRadius + geoDx,
        center.fY + yRadius + geoDy
    );

    SkRect localBounds = SkRect::MakeLTRB(
        localCenter.fX - xLocalRadius,
        localCenter.fY - yLocalRadius,
        localCenter.fX + xLocalRadius,
        localCenter.fY + yLocalRadius
    );

    SkRect mappedBounds;
    SkPoint points[8];
    SkPoint mappedPoints[8];
    vm.mapRect(&mappedBounds, bounds);

    // This adjusts the "radius" to include the half-pixel border
    SkScalar offsetDx = SkScalarDiv(geoDx, xRadius);
    SkScalar offsetDy = SkScalarDiv(geoDy, yRadius);
    SkScalar innerRatioX = SkScalarDiv(xRadius, innerXRadius);
    SkScalar innerRatioY = SkScalarDiv(yRadius, innerYRadius);
    points[0] = SkPoint::Make(-1.0f - offsetDx, -1.0f - offsetDy);
    points[1] = SkPoint::Make(-innerRatioX - offsetDx, -innerRatioY - offsetDy);
    points[2] = SkPoint::Make(1.0f + offsetDx, -1.0f - offsetDy);
    points[3] = SkPoint::Make(innerRatioX + offsetDx, -innerRatioY - offsetDy);
    points[4] = SkPoint::Make(-1.0f - offsetDx, 1.0f + offsetDy);
    points[5] = SkPoint::Make(-innerRatioX - offsetDx, innerRatioY + offsetDy);
    points[6] = SkPoint::Make(1.0f + offsetDx, 1.0f + offsetDy);
    points[7] = SkPoint::Make(innerRatioX + offsetDx, innerRatioY + offsetDy);

    SkScalar leftPt = center.fX - xRadius - geoDx;
    SkScalar rightPt = center.fX + xRadius + geoDx;
    SkScalar topPt = center.fY - yRadius - geoDy;
    SkScalar bottomPt = center.fY + yRadius + geoDy;

    SkPoint boundPts[4];
    boundPts[0].fX = leftPt;
    boundPts[0].fY = topPt;
    boundPts[1].fX = leftPt;
    boundPts[1].fY = bottomPt;
    boundPts[2].fX = rightPt;
    boundPts[2].fY = bottomPt;
    boundPts[3].fX = rightPt;
    boundPts[3].fY = topPt;

    vm.mapPoints(mappedPoints, points, 8);
    SkPoint mappedBoundPts[4];
    vm.mapPoints(mappedBoundPts, boundPts, 4);

    SkMatrix invert;
    if (!viewMatrix.invert(&invert)) {
        return false;
    }

    GrDrawTarget::AutoReleaseGeometry arg;
    GrPipelineBuilder::AutoRestoreEffects are(pipelineBuilder);

    GrGeometryProcessor* gp;
    if (useLocalCoord) {
        gp = (DIEllipseEdgeEffect::Create(color, SkMatrix::I(), pipelineBuilder->getLocalMatrix(), mode, true));
        if (!arg.set(target, 4, gp->getVertexStride(), 0)) {
            SkDebugf("Failed to get space for vertices!\n");
            return false;
        }
        DIEllipseUVVertex* verts = reinterpret_cast<DIEllipseUVVertex*>(arg.vertices());
        SkPoint pt;

        verts[0].fPos = mappedBoundPts[0];
        verts[0].fOuterOffset = points[0];
        verts[0].fInnerOffset = points[1];
        verts[0].fColor = color;
        pt.fX = localBounds.fLeft;
        pt.fY = localBounds.fTop;
        localMatrix.mapPoints(&pt, 1);
        verts[0].fLocalPos = pt;

        verts[1].fPos = mappedBoundPts[1];
        verts[1].fOuterOffset = points[4];
        verts[1].fInnerOffset = points[5];
        verts[1].fColor = color;
        pt.fX = localBounds.fLeft;
        pt.fY = localBounds.fBottom;
        localMatrix.mapPoints(&pt, 1);
        verts[1].fLocalPos = pt;

        verts[2].fPos = mappedBoundPts[3];
        verts[2].fOuterOffset = points[2];
        verts[2].fInnerOffset = points[3];
        verts[2].fColor = color;
        pt.fX = localBounds.fRight;
        pt.fY = localBounds.fTop;
        localMatrix.mapPoints(&pt, 1);
        verts[2].fLocalPos = pt;

        verts[3].fPos = mappedBoundPts[2];
        verts[3].fOuterOffset = points[6];
        verts[3].fInnerOffset = points[7];
        verts[3].fColor = color;
        pt.fX = localBounds.fRight;
        pt.fY = localBounds.fBottom;
        localMatrix.mapPoints(&pt, 1);
        verts[3].fLocalPos = pt;
    }
    else {
        gp = (DIEllipseEdgeEffect::Create(color, SkMatrix::I(), invert, mode, false));
        if (!arg.set(target, 4, gp->getVertexStride(), 0)) {
            SkDebugf("Failed to get space for vertices!\n");
            return false;
        }
        DIEllipseVertex* verts = reinterpret_cast<DIEllipseVertex*>(arg.vertices());

        verts[0].fPos = mappedBoundPts[0];
        verts[0].fOuterOffset = points[0];
        verts[0].fInnerOffset = points[1];
        verts[0].fColor = color;

        verts[1].fPos = mappedBoundPts[1];
        verts[1].fOuterOffset = points[4];
        verts[1].fInnerOffset = points[5];
        verts[1].fColor = color;

        verts[2].fPos = mappedBoundPts[3];
        verts[2].fOuterOffset = points[2];
        verts[2].fInnerOffset = points[3];
        verts[2].fColor = color;

        verts[3].fPos = mappedBoundPts[2];
        verts[3].fOuterOffset = points[6];
        verts[3].fInnerOffset = points[7];
        verts[3].fColor = color;
    }
#if 0
    DIEllipseBatch::Geometry geometry;
    geometry.fViewMatrix = viewMatrix;
    geometry.fColor = color;
    geometry.fXRadius = xRadius;
    geometry.fYRadius = yRadius;
    geometry.fInnerXRadius = innerXRadius;
    geometry.fInnerYRadius = innerYRadius;
    geometry.fGeoDx = geoDx;
    geometry.fGeoDy = geoDy;
    geometry.fMode = mode;
    geometry.fDevBounds = bounds;

    SkAutoTUnref<GrBatch> batch(DIEllipseBatch::Create(geometry));
    target->drawBatch(pipelineBuilder, batch, &bounds);
#endif
    target->setIndexSourceToBuffer(oindexBuffer);
    target->drawIndexedInstances(pipelineBuilder, gp, kTriangles_GrPrimitiveType, 1, 4, 6, &mappedBounds);
    gp->unref();
    return true;
}

///////////////////////////////////////////////////////////////////////////////

static const uint16_t gRRectIndices[] = {
    // corners
    0, 1, 5, 0, 5, 4,
    2, 3, 7, 2, 7, 6,
    8, 9, 13, 8, 13, 12,
    10, 11, 15, 10, 15, 14,

    // edges
    1, 2, 6, 1, 6, 5,
    4, 5, 9, 4, 9, 8,
    6, 7, 11, 6, 11, 10,
    9, 10, 14, 9, 14, 13,

    // center
    // we place this at the end so that we can ignore these indices when rendering stroke-only
    5, 6, 10, 5, 10, 9
};

static const uint16_t gRRectStrokeIndices[] = {
    // corners
    0, 1, 5, 0, 5, 4,
    2, 3, 7, 2, 7, 6,
    8, 9, 13, 8, 13, 12,
    10, 11, 15, 10, 15, 14,

    // edges
    1, 2, 6, 1, 6, 5,
    4, 5, 9, 4, 9, 8,
    6, 7, 11, 6, 11, 10,
    9, 10, 14, 9, 14, 13,
};

static const int MAX_RRECTS = 300; // 32768 * 4 / (28 * 16)
static const int kIndicesPerStrokeRRect = SK_ARRAY_COUNT(gRRectIndices) - 6;
static const int kIndicesPerRRect = SK_ARRAY_COUNT(gRRectIndices);
static const int kVertsPerRRect = 16;
static const int kNumRRectsInIndexBuffer = 256;

static inline void fill_indices(uint16_t *indices, const uint16_t *src,
                                const int srcSize, const int indicesCount, const int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < srcSize; j++)
            indices[i * srcSize + j] = src[j] + i * indicesCount;
    }
}

GrIndexBuffer* GrOvalRenderer::rRectFillIndexBuffer(GrGpu* gpu) {
    if (NULL == fFillRRectIndexBuffer) {
        static const int SIZE = sizeof(gRRectIndices) * MAX_RRECTS;
        fFillRRectIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != fFillRRectIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)fFillRRectIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gRRectIndices,
                             sizeof(gRRectIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                fFillRRectIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gRRectIndices),
                             sizeof(gRRectIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                if (!fFillRRectIndexBuffer->updateData(indices, SIZE)) {
                    fFillRRectIndexBuffer->unref();
                    fFillRRectIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fFillRRectIndexBuffer;
}

GrIndexBuffer* GrOvalRenderer::rRectStrokeIndexBuffer(GrGpu* gpu) {
    if (NULL == fStrokeRRectIndexBuffer) {
        static const int SIZE = sizeof(gRRectStrokeIndices) * MAX_RRECTS;
        fStrokeRRectIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != fStrokeRRectIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)fStrokeRRectIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gRRectStrokeIndices,
                             sizeof(gRRectStrokeIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                fStrokeRRectIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gRRectStrokeIndices),
                             sizeof(gRRectStrokeIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                if (!fStrokeRRectIndexBuffer->updateData(indices, SIZE)) {
                    fStrokeRRectIndexBuffer->unref();
                    fStrokeRRectIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fStrokeRRectIndexBuffer;
}

static const uint16_t gOvalIndices[] = {
    // corners
    0, 1, 2, 1, 2, 3
};

static const int MAX_OVALS = 1170; // 32768 * 4 / (28 * 4)

GrIndexBuffer* GrOvalRenderer::ovalIndexBuffer(GrGpu *gpu) {
    if (NULL == fOvalIndexBuffer) {
        static const int SIZE = sizeof(gOvalIndices) * MAX_OVALS;
        fOvalIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != fOvalIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)fOvalIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gOvalIndices,
                             sizeof(gOvalIndices)/sizeof(uint16_t),
                             4, MAX_OVALS);
                fOvalIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gOvalIndices),
                             sizeof(gOvalIndices) / sizeof(uint16_t),
                             4, MAX_OVALS);
                if (!fOvalIndexBuffer->updateData(indices, SIZE)) {
                    fOvalIndexBuffer->unref();
                    fOvalIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fOvalIndexBuffer;
}

bool GrOvalRenderer::drawDRRect(GrDrawTarget* target,
                                GrPipelineBuilder* pipelineBuilder,
                                GrColor color,
                                const SkMatrix& viewMatrix,
                                bool useAA,
                                const SkRRect& origOuter,
                                const SkRRect& origInner) {
    bool applyAA = useAA &&
                   !pipelineBuilder->getRenderTarget()->isMultisampled() &&
                   pipelineBuilder->canUseFracCoveragePrimProc(color, *target->caps());
    GrPipelineBuilder::AutoRestoreEffects are;
    if (!origInner.isEmpty()) {
        SkTCopyOnFirstWrite<SkRRect> inner(origInner);
        if (!viewMatrix.isIdentity()) {
            if (!origInner.transform(viewMatrix, inner.writable())) {
                return false;
            }
        }
        GrPrimitiveEdgeType edgeType = applyAA ?
                kInverseFillAA_GrProcessorEdgeType :
                kInverseFillBW_GrProcessorEdgeType;
        // TODO this needs to be a geometry processor
        GrFragmentProcessor* fp = GrRRectEffect::Create(edgeType, *inner);
        if (NULL == fp) {
            return false;
        }
        are.set(pipelineBuilder);
        pipelineBuilder->addCoverageProcessor(fp)->unref();
    }

    SkStrokeRec fillRec(SkStrokeRec::kFill_InitStyle);
    if (this->drawRRect(target, pipelineBuilder, color, viewMatrix, useAA, origOuter, fillRec)) {
        return true;
    }

    SkASSERT(!origOuter.isEmpty());
    SkTCopyOnFirstWrite<SkRRect> outer(origOuter);
    if (!viewMatrix.isIdentity()) {
        if (!origOuter.transform(viewMatrix, outer.writable())) {
            return false;
        }
    }
    GrPrimitiveEdgeType edgeType = applyAA ? kFillAA_GrProcessorEdgeType :
                                             kFillBW_GrProcessorEdgeType;
    GrFragmentProcessor* effect = GrRRectEffect::Create(edgeType, *outer);
    if (NULL == effect) {
        return false;
    }
    if (!are.isSet()) {
        are.set(pipelineBuilder);
    }

    SkMatrix invert;
    if (!viewMatrix.invert(&invert)) {
        return false;
    }

    pipelineBuilder->addCoverageProcessor(effect)->unref();
    SkRect bounds = outer->getBounds();
    if (applyAA) {
        bounds.outset(SK_ScalarHalf, SK_ScalarHalf);
    }
    target->drawRect(pipelineBuilder, color, SkMatrix::I(), bounds, NULL, &invert);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class RRectCircleRendererBatch : public GrBatch {
public:
    struct Geometry {
        GrColor fColor;
        SkMatrix fViewMatrix;
        SkScalar fInnerRadius;
        SkScalar fOuterRadius;
        bool fStroke;
        SkRect fDevBounds;
    };

    static GrBatch* Create(const Geometry& geometry, const GrIndexBuffer* indexBuffer) {
        return SkNEW_ARGS(RRectCircleRendererBatch, (geometry, indexBuffer));
    }

    const char* name() const SK_OVERRIDE { return "RRectCircleBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const SK_OVERRIDE {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    void initBatchTracker(const GrPipelineInfo& init) SK_OVERRIDE {
        // Handle any color overrides
        if (init.fColorIgnored) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        } else if (GrColor_ILLEGAL != init.fOverrideColor) {
            fGeoData[0].fColor = init.fOverrideColor;
        }

        // setup batch properties
        fBatch.fColorIgnored = init.fColorIgnored;
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = init.fUsesLocalCoords;
        fBatch.fCoverageIgnored = init.fCoverageIgnored;
    }

    void generateGeometry(GrBatchTarget* batchTarget, const GrPipeline* pipeline) SK_OVERRIDE {
        // reset to device coordinates
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return;
        }

        // Setup geometry processor
        SkAutoTUnref<GrGeometryProcessor> gp(CircleEdgeEffect::Create(this->color(),
                                                                      this->stroke(),
                                                                      invert,
                                                                      false));

        batchTarget->initDraw(gp, pipeline);

        // TODO this is hacky, but the only way we have to initialize the GP is to use the
        // GrPipelineInfo struct so we can generate the correct shader.  Once we have GrBatch
        // everywhere we can remove this nastiness
        GrPipelineInfo init;
        init.fColorIgnored = fBatch.fColorIgnored;
        init.fOverrideColor = GrColor_ILLEGAL;
        init.fCoverageIgnored = fBatch.fCoverageIgnored;
        init.fUsesLocalCoords = this->usesLocalCoords();
        gp->initBatchTracker(batchTarget->currentBatchTracker(), init);

        int instanceCount = fGeoData.count();
        int vertexCount = kVertsPerRRect * instanceCount;
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(CircleVertex));

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void *vertices = batchTarget->vertexPool()->makeSpace(vertexStride,
                                                              vertexCount,
                                                              &vertexBuffer,
                                                              &firstVertex);

        CircleVertex* verts = reinterpret_cast<CircleVertex*>(vertices);

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];

            SkScalar outerRadius = args.fOuterRadius;

            const SkRect& bounds = args.fDevBounds;

            SkScalar yCoords[4] = {
                bounds.fTop,
                bounds.fTop + outerRadius,
                bounds.fBottom - outerRadius,
                bounds.fBottom
            };

            SkScalar yOuterRadii[4] = {-1, 0, 0, 1 };
            // The inner radius in the vertex data must be specified in normalized space.
            SkScalar innerRadius = args.fInnerRadius / args.fOuterRadius;
            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(-1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts++;
            }
        }

        // drop out the middle quad if we're stroked
        int indexCnt = this->stroke() ? SK_ARRAY_COUNT(gRRectIndices) - 6 :
                                        SK_ARRAY_COUNT(gRRectIndices);


        GrDrawTarget::DrawInfo drawInfo;
        drawInfo.setPrimitiveType(kTriangles_GrPrimitiveType);
        drawInfo.setStartVertex(0);
        drawInfo.setStartIndex(0);
        drawInfo.setVerticesPerInstance(kVertsPerRRect);
        drawInfo.setIndicesPerInstance(indexCnt);
        drawInfo.adjustStartVertex(firstVertex);
        drawInfo.setVertexBuffer(vertexBuffer);
        drawInfo.setIndexBuffer(fIndexBuffer);

        int maxInstancesPerDraw = kNumRRectsInIndexBuffer;

        while (instanceCount) {
            drawInfo.setInstanceCount(SkTMin(instanceCount, maxInstancesPerDraw));
            drawInfo.setVertexCount(drawInfo.instanceCount() * drawInfo.verticesPerInstance());
            drawInfo.setIndexCount(drawInfo.instanceCount() * drawInfo.indicesPerInstance());

            batchTarget->draw(drawInfo);

            drawInfo.setStartVertex(drawInfo.startVertex() + drawInfo.vertexCount());
            instanceCount -= drawInfo.instanceCount();
        }
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    RRectCircleRendererBatch(const Geometry& geometry, const GrIndexBuffer* indexBuffer)
        : fIndexBuffer(indexBuffer) {
        this->initClassID<RRectCircleRendererBatch>();
        fGeoData.push_back(geometry);
    }

    bool onCombineIfPossible(GrBatch* t) SK_OVERRIDE {
        RRectCircleRendererBatch* that = t->cast<RRectCircleRendererBatch>();

        // TODO use vertex color to avoid breaking batches
        if (this->color() != that->color()) {
            return false;
        }

        if (this->stroke() != that->stroke()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool stroke() const { return fBatch.fStroke; }

    struct BatchTracker {
        GrColor fColor;
        bool fStroke;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;
    const GrIndexBuffer* fIndexBuffer;
};

class RRectEllipseRendererBatch : public GrBatch {
public:
    struct Geometry {
        GrColor fColor;
        SkMatrix fViewMatrix;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        bool fStroke;
        SkRect fDevBounds;
    };

    static GrBatch* Create(const Geometry& geometry, const GrIndexBuffer* indexBuffer) {
        return SkNEW_ARGS(RRectEllipseRendererBatch, (geometry, indexBuffer));
    }

    const char* name() const SK_OVERRIDE { return "RRectEllipseRendererBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const SK_OVERRIDE {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const SK_OVERRIDE {
        out->setUnknownSingleComponent();
    }

    void initBatchTracker(const GrPipelineInfo& init) SK_OVERRIDE {
        // Handle any color overrides
        if (init.fColorIgnored) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        } else if (GrColor_ILLEGAL != init.fOverrideColor) {
            fGeoData[0].fColor = init.fOverrideColor;
        }

        // setup batch properties
        fBatch.fColorIgnored = init.fColorIgnored;
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = init.fUsesLocalCoords;
        fBatch.fCoverageIgnored = init.fCoverageIgnored;
    }

    void generateGeometry(GrBatchTarget* batchTarget, const GrPipeline* pipeline) SK_OVERRIDE {
        // reset to device coordinates
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return;
        }

        // Setup geometry processor
        SkAutoTUnref<GrGeometryProcessor> gp(EllipseEdgeEffect::Create(this->color(),
                                                                       this->stroke(),
                                                                       invert,
                                                                       false));

        batchTarget->initDraw(gp, pipeline);

        // TODO this is hacky, but the only way we have to initialize the GP is to use the
        // GrPipelineInfo struct so we can generate the correct shader.  Once we have GrBatch
        // everywhere we can remove this nastiness
        GrPipelineInfo init;
        init.fColorIgnored = fBatch.fColorIgnored;
        init.fOverrideColor = GrColor_ILLEGAL;
        init.fCoverageIgnored = fBatch.fCoverageIgnored;
        init.fUsesLocalCoords = this->usesLocalCoords();
        gp->initBatchTracker(batchTarget->currentBatchTracker(), init);

        int instanceCount = fGeoData.count();
        int vertexCount = kVertsPerRRect * instanceCount;
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(EllipseVertex));

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void *vertices = batchTarget->vertexPool()->makeSpace(vertexStride,
                                                              vertexCount,
                                                              &vertexBuffer,
                                                              &firstVertex);

        EllipseVertex* verts = reinterpret_cast<EllipseVertex*>(vertices);

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];

            // Compute the reciprocals of the radii here to save time in the shader
            SkScalar xRadRecip = SkScalarInvert(args.fXRadius);
            SkScalar yRadRecip = SkScalarInvert(args.fYRadius);
            SkScalar xInnerRadRecip = SkScalarInvert(args.fInnerXRadius);
            SkScalar yInnerRadRecip = SkScalarInvert(args.fInnerYRadius);

            // Extend the radii out half a pixel to antialias.
            SkScalar xOuterRadius = args.fXRadius + SK_ScalarHalf;
            SkScalar yOuterRadius = args.fYRadius + SK_ScalarHalf;

            const SkRect& bounds = args.fDevBounds;

            SkScalar yCoords[4] = {
                bounds.fTop,
                bounds.fTop + yOuterRadius,
                bounds.fBottom - yOuterRadius,
                bounds.fBottom
            };

            SkScalar yOuterOffsets[4] = {
                yOuterRadius,
                SK_ScalarNearlyZero, // we're using inversesqrt() in shader, so can't be exactly 0
                SK_ScalarNearlyZero,
                yOuterRadius
            };

            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts++;
            }
        }

        // drop out the middle quad if we're stroked
        int indexCnt = this->stroke() ? SK_ARRAY_COUNT(gRRectIndices) - 6 :
                                        SK_ARRAY_COUNT(gRRectIndices);

        GrDrawTarget::DrawInfo drawInfo;
        drawInfo.setPrimitiveType(kTriangles_GrPrimitiveType);
        drawInfo.setStartVertex(0);
        drawInfo.setStartIndex(0);
        drawInfo.setVerticesPerInstance(kVertsPerRRect);
        drawInfo.setIndicesPerInstance(indexCnt);
        drawInfo.adjustStartVertex(firstVertex);
        drawInfo.setVertexBuffer(vertexBuffer);
        drawInfo.setIndexBuffer(fIndexBuffer);

        int maxInstancesPerDraw = kNumRRectsInIndexBuffer;

        while (instanceCount) {
            drawInfo.setInstanceCount(SkTMin(instanceCount, maxInstancesPerDraw));
            drawInfo.setVertexCount(drawInfo.instanceCount() * drawInfo.verticesPerInstance());
            drawInfo.setIndexCount(drawInfo.instanceCount() * drawInfo.indicesPerInstance());

            batchTarget->draw(drawInfo);

            drawInfo.setStartVertex(drawInfo.startVertex() + drawInfo.vertexCount());
            instanceCount -= drawInfo.instanceCount();
        }
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    RRectEllipseRendererBatch(const Geometry& geometry, const GrIndexBuffer* indexBuffer)
        : fIndexBuffer(indexBuffer) {
        this->initClassID<RRectEllipseRendererBatch>();
        fGeoData.push_back(geometry);
    }

    bool onCombineIfPossible(GrBatch* t) SK_OVERRIDE {
        RRectEllipseRendererBatch* that = t->cast<RRectEllipseRendererBatch>();

        // TODO use vertex color to avoid breaking batches
        if (this->color() != that->color()) {
            return false;
        }

        if (this->stroke() != that->stroke()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool stroke() const { return fBatch.fStroke; }

    struct BatchTracker {
        GrColor fColor;
        bool fStroke;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;
    const GrIndexBuffer* fIndexBuffer;
};

bool GrOvalRenderer::drawRRect(GrDrawTarget* target,
                               GrPipelineBuilder* pipelineBuilder,
                               GrColor color,
                               const SkMatrix& viewMatrix,
                               bool useAA,
                               const SkRRect& rrect,
                               const SkStrokeRec& stroke) {
    const SkMatrix vm = viewMatrix;
    if (rrect.isOval()) {
        return this->drawOval(target, pipelineBuilder, color, viewMatrix, useAA, rrect.getBounds(),
                              stroke);
    }

    bool useCoverageAA = useAA &&
        pipelineBuilder->canUseFracCoveragePrimProc(color, *target->caps());

    // only anti-aliased rrects for now
    if (!useCoverageAA) {
        return false;
    }

    if (!vm.rectStaysRect() || !rrect.isSimple()) {
        return false;
    }

    // do any matrix crunching before we reset the draw state for device coords
    const SkRect& rrectBounds = rrect.getBounds();
    SkRect bounds;
    vm.mapRect(&bounds, rrectBounds);

    SkRect localBounds = rrectBounds;
    SkMatrix localMatrix;
    bool useLocalCoord = false;

    SkVector radii = rrect.getSimpleRadii();
    SkScalar xRadius = SkScalarAbs(vm[SkMatrix::kMScaleX]*radii.fX +
                                   vm[SkMatrix::kMSkewY]*radii.fY);
    SkScalar yRadius = SkScalarAbs(vm[SkMatrix::kMSkewX]*radii.fX +
                                   vm[SkMatrix::kMScaleY]*radii.fY);

    SkScalar xLocalRadius = radii.fX;
    SkScalar yLocalRadius = radii.fY;

    SkStrokeRec::Style style = stroke.getStyle();

    // do (potentially) anisotropic mapping of stroke
    SkVector scaledStroke;
    SkScalar strokeWidth = stroke.getWidth();
    SkScalar localStrokeWidth = strokeWidth;

    bool isStrokeOnly = SkStrokeRec::kStroke_Style == style ||
                        SkStrokeRec::kHairline_Style == style;
    bool hasStroke = isStrokeOnly || SkStrokeRec::kStrokeAndFill_Style == style;

    // use local coords for shader that is a bitmap
    if (pipelineBuilder->canOptimizeForBitmapShader()) {
        const SkMatrix& lm = pipelineBuilder->getLocalMatrix();
        GrPipelineBuilder::AutoLocalMatrixChange almc;
        almc.set(pipelineBuilder);
        useLocalCoord = true;
        localMatrix = lm;
    }

    if (hasStroke) {
        if (SkStrokeRec::kHairline_Style == style) {
            scaledStroke.set(1, 1);
        } else {
            scaledStroke.fX = SkScalarAbs(strokeWidth*(vm[SkMatrix::kMScaleX] +
                                                       vm[SkMatrix::kMSkewY]));
            scaledStroke.fY = SkScalarAbs(strokeWidth*(vm[SkMatrix::kMSkewX] +
                                                       vm[SkMatrix::kMScaleY]));
        }

        // if half of strokewidth is greater than radius, we don't handle that right now
        if (SK_ScalarHalf*scaledStroke.fX > xRadius || SK_ScalarHalf*scaledStroke.fY > yRadius) {
            return false;
        }
    }

    // The way the effect interpolates the offset-to-ellipse/circle-center attribute only works on
    // the interior of the rrect if the radii are >= 0.5. Otherwise, the inner rect of the nine-
    // patch will have fractional coverage. This only matters when the interior is actually filled.
    // We could consider falling back to rect rendering here, since a tiny radius is
    // indistinguishable from a square corner.
    if (!isStrokeOnly && (SK_ScalarHalf > xRadius || SK_ScalarHalf > yRadius)) {
        return false;
    }

    GrContext* context = pipelineBuilder->getRenderTarget()->getContext();
    GrIndexBuffer* indexBuffer = NULL;
    if (isStrokeOnly)
        indexBuffer = this->rRectStrokeIndexBuffer(context->getGpu());
    else
        indexBuffer = this->rRectFillIndexBuffer(context->getGpu());
    if (NULL == indexBuffer) {
        SkDebugf("Failed to create index buffer!\n");
        return false;
    }

    GrDrawTarget::AutoReleaseGeometry arg;
    GrPipelineBuilder::AutoRestoreEffects are(pipelineBuilder);

    // if the corners are circles, use the circle renderer
    if ((!hasStroke || scaledStroke.fX == scaledStroke.fY) && xRadius == yRadius) {
        SkScalar innerRadius = 0.0f;
        SkScalar outerRadius = xRadius;
        SkScalar localOuterRadius = xLocalRadius;
        SkScalar halfWidth = 0;
        SkScalar localHalfWidth = 0;
        if (hasStroke) {
            if (SkScalarNearlyZero(scaledStroke.fX)) {
                halfWidth = SK_ScalarHalf;
                localHalfWidth = SK_ScalarHalf;
            } else {
                halfWidth = SkScalarHalf(scaledStroke.fX);
                localHalfWidth = SkScalarHalf(localStrokeWidth);
            }

            if (isStrokeOnly) {
                innerRadius = xRadius - halfWidth;
            }
            outerRadius += halfWidth;
            bounds.outset(halfWidth, halfWidth);

            localOuterRadius += localHalfWidth;
            localBounds.outset(localHalfWidth, localHalfWidth);
        }

        SkMatrix invert;
        if (!viewMatrix.invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return false;
        }
        isStrokeOnly = (isStrokeOnly && innerRadius >= 0);

        // The radii are outset for two reasons. First, it allows the shader to simply perform
        // simpler computation because the computed alpha is zero, rather than 50%, at the radius.
        // Second, the outer radius is used to compute the verts of the bounding box that is
        // rendered and the outset ensures the box will cover all partially covered by the rrect
        // corners.
        outerRadius += SK_ScalarHalf;
        innerRadius -= SK_ScalarHalf;
        localOuterRadius += SK_ScalarHalf;
        // Expand the rect so all the pixels will be captured.
        bounds.outset(SK_ScalarHalf, SK_ScalarHalf);
        localBounds.outset(SK_ScalarHalf, SK_ScalarHalf);

        SkScalar yCoords[4] = {
            bounds.fTop,
            bounds.fTop + outerRadius,
            bounds.fBottom - outerRadius,
            bounds.fBottom
        };
        SkScalar yLocalCoords[4] = {
            localBounds.fTop,
            localBounds.fTop + localOuterRadius,
            localBounds.fBottom - localOuterRadius,
            localBounds.fBottom
        };

        GrGeometryProcessor* gp;
        if (useLocalCoord) {
            gp = (CircleEdgeEffect::Create(color,
                                           isStrokeOnly,
                                           pipelineBuilder->getLocalMatrix(),
                                           true));
            if (!arg.set(target, 16, gp->getVertexStride(), 0)) {
                if (useLocalCoord) {
                    // restore transformation matrix
                    GrPipelineBuilder::AutoLocalMatrixRestore almr;
                    SkMatrix inv;
                    if (localMatrix.invert(&inv))
                        almr.set(pipelineBuilder, inv);
                }
                SkDebugf("Failed to get space for vertices!\n");
                return false;
            }
            CircleUVVertex* verts = reinterpret_cast<CircleUVVertex*>(arg.vertices());
            SkScalar yOuterRadii[4] = {-1, 0, 0, 1 };
            SkPoint localPt;
            SkPoint mappedPt;
            // The inner radius in the vertex data must be specified in normalized space.
            innerRadius = innerRadius / outerRadius;
            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(-1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                localPt.fX = localBounds.fLeft;
                localPt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &localPt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                localPt.fX = localBounds.fLeft + localOuterRadius;
                localPt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &localPt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                localPt.fX = localBounds.fRight - localOuterRadius;
                localPt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &localPt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                localPt.fX = localBounds.fRight;
                localPt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &localPt, 1);
                verts->fLocalPos = mappedPt;
                verts++;
            }
        }
        else {
            gp = (CircleEdgeEffect::Create(color,
                                           isStrokeOnly,
                                           invert,
                                           false));
            if (!arg.set(target, 16, gp->getVertexStride(), 0)) {
                if (useLocalCoord) {
                    // restore transformation matrix
                    GrPipelineBuilder::AutoLocalMatrixRestore almr;
                    SkMatrix inv;
                    if (localMatrix.invert(&inv))
                        almr.set(pipelineBuilder, inv);
                }
                SkDebugf("Failed to get space for vertices!\n");
                return false;
            }
            CircleVertex* verts = reinterpret_cast<CircleVertex*>(arg.vertices());
            SkScalar yOuterRadii[4] = {-1, 0, 0, 1 };
            // The inner radius in the vertex data must be specified in normalized space.
            innerRadius = innerRadius / outerRadius;
            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(-1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - outerRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(1, yOuterRadii[i]);
                verts->fOuterRadius = outerRadius;
                verts->fInnerRadius = innerRadius;
                verts->fColor = color;
                verts++;
            }
        }

        // drop out the middle quad if we're stroked
        int indexCnt = isStrokeOnly ? SK_ARRAY_COUNT(gRRectStrokeIndices) :
                                      SK_ARRAY_COUNT(gRRectIndices);
        target->setIndexSourceToBuffer(indexBuffer);
        target->drawIndexedInstances(pipelineBuilder, gp, kTriangles_GrPrimitiveType, 1, 16, indexCnt, &bounds);
        gp->unref();

#if 0
        RRectCircleRendererBatch::Geometry geometry;
        geometry.fViewMatrix = viewMatrix;
        geometry.fColor = color;
        geometry.fInnerRadius = innerRadius;
        geometry.fOuterRadius = outerRadius;
        geometry.fStroke = isStrokeOnly;
        geometry.fDevBounds = bounds;

        SkAutoTUnref<GrBatch> batch(RRectCircleRendererBatch::Create(geometry, indexBuffer));
        target->drawBatch(pipelineBuilder, batch, &bounds);
#endif
    // otherwise we use the ellipse renderer
    } else {
        SkScalar innerXRadius = 0.0f;
        SkScalar innerYRadius = 0.0f;
        SkScalar localHalfWidth = 0.0f;
        if (hasStroke) {
            if (SkScalarNearlyZero(scaledStroke.length())) {
                scaledStroke.set(SK_ScalarHalf, SK_ScalarHalf);
                localHalfWidth = SK_ScalarHalf;
            } else {
                scaledStroke.scale(SK_ScalarHalf);
                localHalfWidth = SkScalarHalf(localStrokeWidth);
            }

            // we only handle thick strokes for near-circular ellipses
            if (scaledStroke.length() > SK_ScalarHalf &&
                (SK_ScalarHalf*xRadius > yRadius || SK_ScalarHalf*yRadius > xRadius)) {
                if (useLocalCoord) {
                    GrPipelineBuilder::AutoLocalMatrixRestore almr;
                    SkMatrix inv;

                    if (localMatrix.invert(&inv))
                        almr.set(pipelineBuilder, inv);
                }
                return false;
            }

            // we don't handle it if curvature of the stroke is less than curvature of the ellipse
            if (scaledStroke.fX*(yRadius*yRadius) < (scaledStroke.fY*scaledStroke.fY)*xRadius ||
                scaledStroke.fY*(xRadius*xRadius) < (scaledStroke.fX*scaledStroke.fX)*yRadius) {
                if (useLocalCoord) {
                    GrPipelineBuilder::AutoLocalMatrixRestore almr;
                    SkMatrix inv;

                    if (localMatrix.invert(&inv))
                        almr.set(pipelineBuilder, inv);
                }
                return false;
            }

            // this is legit only if scale & translation (which should be the case at the moment)
            if (isStrokeOnly) {
                innerXRadius = xRadius - scaledStroke.fX;
                innerYRadius = yRadius - scaledStroke.fY;
            }

            xRadius += scaledStroke.fX;
            yRadius += scaledStroke.fY;

            xLocalRadius += SK_ScalarHalf;
            yLocalRadius += SK_ScalarHalf;
            bounds.outset(scaledStroke.fX, scaledStroke.fY);
            localBounds.outset(localHalfWidth, localHalfWidth);
        }

        isStrokeOnly = (isStrokeOnly && innerXRadius >= 0 && innerYRadius >= 0);

        // Expand the rect so all the pixels will be captured.
        bounds.outset(SK_ScalarHalf, SK_ScalarHalf);


        SkMatrix invert;
        if (!viewMatrix.invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return false;
        }


        // Compute the reciprocals of the radii here to save time in the shader
        SkScalar xRadRecip = SkScalarInvert(xRadius);
        SkScalar yRadRecip = SkScalarInvert(yRadius);
        SkScalar xInnerRadRecip = SkScalarInvert(innerXRadius);
        SkScalar yInnerRadRecip = SkScalarInvert(innerYRadius);

        // Extend the radii out half a pixel to antialias.
        SkScalar xOuterRadius = xRadius + SK_ScalarHalf;
        SkScalar yOuterRadius = yRadius + SK_ScalarHalf;

        SkScalar xLocalOuterRadius = xLocalRadius + SK_ScalarHalf;
        SkScalar yLocalOuterRadius = yLocalRadius + SK_ScalarHalf;
        SkScalar yCoords[4] = {
            bounds.fTop,
            bounds.fTop + yOuterRadius,
            bounds.fBottom - yOuterRadius,
            bounds.fBottom
        };
        SkScalar yLocalCoords[4] = {
            localBounds.fTop,
            localBounds.fTop + yLocalOuterRadius,
            localBounds.fBottom - yLocalOuterRadius,
            localBounds.fBottom
        };

        SkScalar yOuterOffsets[4] = {
            yOuterRadius,
            SK_ScalarNearlyZero, // we're using inversesqrt() in shader, so can't be exactly 0
            SK_ScalarNearlyZero,
            yOuterRadius
        };

        GrGeometryProcessor* gp;
        if(useLocalCoord) {
            SkPoint pt;
            SkPoint mappedPt;
            gp = (EllipseEdgeEffect::Create(color,
                                            isStrokeOnly,
                                            pipelineBuilder->getLocalMatrix(),
                                            true));
            if (!arg.set(target, 16, gp->getVertexStride(), 0)) {
                SkDebugf("Failed to get space for vertices!\n");
                return false;
            }
            EllipseUVVertex* verts = reinterpret_cast<EllipseUVVertex*>(arg.vertices());

            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                pt.fX = localBounds.fLeft;
                pt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &pt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                pt.fX = localBounds.fLeft + xLocalOuterRadius;
                pt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &pt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                pt.fX = localBounds.fRight - xLocalOuterRadius;
                pt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &pt, 1);
                verts->fLocalPos = mappedPt;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                pt.fX = localBounds.fRight;
                pt.fY = yLocalCoords[i];
                localMatrix.mapPoints(&mappedPt, &pt, 1);
                verts->fLocalPos = mappedPt;
                verts++;
            }
        }
        else{
            gp = (EllipseEdgeEffect::Create(color,
                                            isStrokeOnly,
                                            invert,
                                            false));
            if (!arg.set(target, 16, gp->getVertexStride(), 0)) {
                SkDebugf("Failed to get space for vertices!\n");
                return false;
            }
            EllipseVertex* verts = reinterpret_cast<EllipseVertex*>(arg.vertices());

            for (int i = 0; i < 4; ++i) {
                verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fLeft + xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight - xOuterRadius, yCoords[i]);
                verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                verts++;

                verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                verts->fColor = color;
                verts++;
            }
        }

        // drop out the middle quad if we're stroked
        int indexCnt = isStrokeOnly ? SK_ARRAY_COUNT(gRRectStrokeIndices) :
                                      SK_ARRAY_COUNT(gRRectIndices);
        target->setIndexSourceToBuffer(indexBuffer);
        target->drawIndexedInstances(pipelineBuilder, gp, kTriangles_GrPrimitiveType, 1, 16, indexCnt, &bounds);
        gp->unref();
#if 0
        RRectEllipseRendererBatch::Geometry geometry;
        geometry.fViewMatrix = viewMatrix;
        geometry.fColor = color;
        geometry.fXRadius = xRadius;
        geometry.fYRadius = yRadius;
        geometry.fInnerXRadius = innerXRadius;
        geometry.fInnerYRadius = innerYRadius;
        geometry.fStroke = isStrokeOnly;
        geometry.fDevBounds = bounds;

        SkAutoTUnref<GrBatch> batch(RRectEllipseRendererBatch::Create(geometry, indexBuffer));
        target->drawBatch(pipelineBuilder, batch, &bounds);
#endif
    }
    return true;
}
