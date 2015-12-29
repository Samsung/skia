/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrOvalRenderer.h"

#include "GrBatchFlushState.h"
#include "GrBatchTest.h"
#include "GrDrawTarget.h"
#include "GrGeometryProcessor.h"
#include "GrInvariantOutput.h"
#include "GrPipelineBuilder.h"
#include "GrProcessor.h"
#include "GrResourceProvider.h"
#include "GrVertexBuffer.h"
#include "SkRRect.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "batches/GrVertexBatch.h"
#include "effects/GrRRectEffect.h"
#include "gl/GrGLProcessor.h"
#include "gl/GrGLGeometryProcessor.h"
#include "gl/builders/GrGLProgramBuilder.h"

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

GrIndexBuffer *gOvalIndexBuffer = NULL;
GrIndexBuffer *gRectFillIndexBuffer = NULL;
GrIndexBuffer *gRectStrokeIndexBuffer = NULL;
static const int MAX_OVALS = 1170; // 32768 * 4 / (28 * 4)
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
    static GrGeometryProcessor* Create(GrColor color, bool stroke, const SkMatrix& localMatrix,
                                       bool usesLocalCoords) {
        return new CircleEdgeEffect(color, stroke, localMatrix, usesLocalCoords);
    }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inCircleEdge() const { return fInCircleEdge; }
    const Attribute* inCircleColor() const { return fInCircleColor; }
    GrColor color() const { return fColor; }
    bool colorIgnored() const { return GrColor_ILLEGAL == fColor; }
    const SkMatrix& localMatrix() const { return fLocalMatrix; }
    bool usesLocalCoords() const { return fUsesLocalCoords; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }
    virtual ~CircleEdgeEffect() {}

    const char* name() const override { return "CircleEdge"; }

    inline bool isStroked() const { return fStroke; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor()
            : fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) override{
            const CircleEdgeEffect& ce = args.fGP.cast<CircleEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
            GrGLVertexBuilder* vsBuilder = args.fPB->getVertexShaderBuilder();

            // emit attributes
            vsBuilder->emitAttributes(ce);

            GrGLVertToFrag v(kVec4f_GrSLType);
            args.fPB->addVarying("CircleEdge", &v);
            vsBuilder->codeAppendf("%s = %s;", v.vsOut(), ce.inCircleEdge()->fName);

            // setup pass through color
            if (!ce.colorIgnored())
                pb->addPassThroughAttribute(ce.inCircleColor(), args.fOutputColor);

            // Setup position
            this->setupPosition(pb, gpArgs, ce.inPosition()->fName);

            if (ce.inLocalCoords()) {
                // emit transforms with explicit local coords
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ce.inLocalCoords()->fName,
                                     ce.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            } else {
                // emit transforms with position
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ce.inPosition()->fName,
                                     ce.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            }

            GrGLFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
            fsBuilder->codeAppendf("float d = length(%s.xy);", v.fsIn());
            fsBuilder->codeAppendf("float edgeAlpha = clamp(%s.z * (1.0 - d), 0.0, 1.0);", v.fsIn());
            if (ce.isStroked()) {
                fsBuilder->codeAppendf("float innerAlpha = (clamp(%s.z * (d - %s.w), 0.0, 1.0));",
                                       v.fsIn(), v.fsIn());
                fsBuilder->codeAppend("edgeAlpha *= innerAlpha;");
            }

            fsBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.fOutputCoverage);
        }

        static void GenKey(const GrGeometryProcessor& gp,
                           const GrGLSLCaps&,
                           GrProcessorKeyBuilder* b) {
            const CircleEdgeEffect& ce = gp.cast<CircleEdgeEffect>();
            uint16_t key = ce.isStroked() ? 0x1 : 0x0;
            key |= ce.usesLocalCoords() && ce.localMatrix().hasPerspective() ? 0x2 : 0x0;
            key |= ce.colorIgnored() ? 0x4 : 0x0;
            b->add32(key);
        }

        void setData(const GrGLProgramDataManager& pdman, const GrPrimitiveProcessor& gp) override {
            //const CircleEdgeEffect& ce = gp.cast<CircleEdgeEffect>();
        }

        void setTransformData(const GrPrimitiveProcessor& primProc,
                              const GrGLProgramDataManager& pdman,
                              int index,
                              const SkTArray<const GrCoordTransform*, true>& transforms) override {
            this->setTransformDataHelper<CircleEdgeEffect>(primProc, pdman, index, transforms);
        }

    private:
        GrColor fColor;
        UniformHandle fColorUniform;
        typedef GrGLGeometryProcessor INHERITED;
    };

    void getGLProcessorKey(const GrGLSLCaps& caps, GrProcessorKeyBuilder* b) const override {
        GLProcessor::GenKey(*this, caps, b);
    }

    GrGLPrimitiveProcessor* createGLInstance(const GrGLSLCaps&) const override {
        return new GLProcessor();
    }

private:
    CircleEdgeEffect(GrColor color, bool stroke, const SkMatrix& localMatrix, bool usesLocalCoords)
        : fColor(color)
        , fLocalMatrix(localMatrix)
        , fUsesLocalCoords(usesLocalCoords) {
        this->initClassID<CircleEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType,
                                                       kHigh_GrSLPrecision));
        fInCircleEdge = &this->addVertexAttrib(Attribute("inCircleEdge",
                                                           kVec4f_GrVertexAttribType));
        fInCircleColor = &this->addVertexAttrib(Attribute("inCircleColor",
                                                            kVec4ub_GrVertexAttribType));
        fInLocalCoords = NULL;
        if (usesLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasExplicitLocalCoords();
        }

        fStroke = stroke;
    }

    GrColor fColor;
    SkMatrix fLocalMatrix;
    const Attribute* fInPosition;
    const Attribute* fInCircleEdge;
    const Attribute* fInCircleColor;
    const Attribute* fInLocalCoords;
    bool fStroke;
    bool fUsesLocalCoords;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(CircleEdgeEffect);

const GrGeometryProcessor* CircleEdgeEffect::TestCreate(GrProcessorTestData* d) {
    return CircleEdgeEffect::Create(GrRandomColor(d->fRandom),
                                    d->fRandom->nextBool(),
                                    GrTest::TestMatrix(d->fRandom),
                                    d->fRandom->nextBool());
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
    static GrGeometryProcessor* Create(GrColor color, bool stroke, const SkMatrix& localMatrix,
                                       bool usesLocalCoords) {
        return new EllipseEdgeEffect(color, stroke, localMatrix, usesLocalCoords);
    }

    virtual ~EllipseEdgeEffect() {}

    const char* name() const override { return "EllipseEdge"; }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inEllipseOffset() const { return fInEllipseOffset; }
    const Attribute* inEllipseRadii() const { return fInEllipseRadii; }
    const Attribute* inEllipseColor() const { return fInEllipseColor; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }

    GrColor color() const { return fColor; }
    bool colorIgnored() const { return GrColor_ILLEGAL == fColor; }
    const SkMatrix& localMatrix() const { return fLocalMatrix; }
    bool usesLocalCoords() const { return fUsesLocalCoords; }

    inline bool isStroked() const { return fStroke; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor()
            : fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) override{
            const EllipseEdgeEffect& ee = args.fGP.cast<EllipseEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
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

            // setup pass through color
            if (!ee.colorIgnored())
                pb->addPassThroughAttribute(ee.inEllipseColor(), args.fOutputColor);

            // Setup position
            this->setupPosition(pb, gpArgs, ee.inPosition()->fName);

            // emit transforms
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
            GrGLFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
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
                           const GrGLSLCaps&,
                           GrProcessorKeyBuilder* b) {
            const EllipseEdgeEffect& ee = gp.cast<EllipseEdgeEffect>();
            uint16_t key = ee.isStroked() ? 0x1 : 0x0;
            key |= ee.usesLocalCoords() && ee.localMatrix().hasPerspective() ? 0x2 : 0x0;
            key |= ee.colorIgnored() ? 0x4 : 0x0;
            b->add32(key);
        }

        void setData(const GrGLProgramDataManager& pdman, const GrPrimitiveProcessor& gp) override {
            //const EllipseEdgeEffect& ee = gp.cast<EllipseEdgeEffect>();
        }

        void setTransformData(const GrPrimitiveProcessor& primProc,
                              const GrGLProgramDataManager& pdman,
                              int index,
                              const SkTArray<const GrCoordTransform*, true>& transforms) override {
            this->setTransformDataHelper<EllipseEdgeEffect>(primProc, pdman, index, transforms);
        }

    private:
        GrColor fColor;
        UniformHandle fColorUniform;

        typedef GrGLGeometryProcessor INHERITED;
    };

    void getGLProcessorKey(const GrGLSLCaps& caps, GrProcessorKeyBuilder* b) const override {
        GLProcessor::GenKey(*this, caps, b);
    }

    GrGLPrimitiveProcessor* createGLInstance(const GrGLSLCaps&) const override {
        return new GLProcessor();
    }

private:
    EllipseEdgeEffect(GrColor color, bool stroke, const SkMatrix& localMatrix,
                      bool usesLocalCoords)
        : fColor(color)
        , fLocalMatrix(localMatrix)
        , fUsesLocalCoords(usesLocalCoords) {
        this->initClassID<EllipseEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType));
        fInEllipseOffset = &this->addVertexAttrib(Attribute("inEllipseOffset",
                                                            kVec2f_GrVertexAttribType));
        fInEllipseRadii = &this->addVertexAttrib(Attribute("inEllipseRadii",
                                                           kVec4f_GrVertexAttribType));
        fInEllipseColor = &this->addVertexAttrib(Attribute("inEllipseColor",
                                                            kVec4ub_GrVertexAttribType));
        fInLocalCoords = NULL;
        if (usesLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasExplicitLocalCoords();
        }
        fStroke = stroke;
    }

    const Attribute* fInPosition;
    const Attribute* fInEllipseOffset;
    const Attribute* fInEllipseRadii;
    const Attribute* fInEllipseColor;
    const Attribute* fInLocalCoords;
    GrColor fColor;
    SkMatrix fLocalMatrix;
    bool fStroke;
    bool fUsesLocalCoords;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(EllipseEdgeEffect);

const GrGeometryProcessor* EllipseEdgeEffect::TestCreate(GrProcessorTestData* d) {
    return EllipseEdgeEffect::Create(GrRandomColor(d->fRandom),
                                     d->fRandom->nextBool(),
                                     GrTest::TestMatrix(d->fRandom),
                                     d->fRandom->nextBool());
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

    static GrGeometryProcessor* Create(GrColor color, const SkMatrix& viewMatrix, const SkMatrix& localMatrix, Mode mode,
                                       bool usesLocalCoords) {
        return new DIEllipseEdgeEffect(color, viewMatrix, localMatrix, mode, usesLocalCoords);
    }

    virtual ~DIEllipseEdgeEffect() {}

    const char* name() const override { return "DIEllipseEdge"; }

    const Attribute* inPosition() const { return fInPosition; }
    const Attribute* inEllipseOffsets0() const { return fInEllipseOffsets0; }
    const Attribute* inEllipseOffsets1() const { return fInEllipseOffsets1; }
    const Attribute* inEllipseColor() const { return fInEllipseColor; }
    const Attribute* inLocalCoords() const { return fInLocalCoords; }
    GrColor color() const { return fColor; }
    bool colorIgnored() const { return GrColor_ILLEGAL == fColor; }
    const SkMatrix& viewMatrix() const { return fViewMatrix; }
    const SkMatrix& localMatrix() const { return fLocalMatrix; }
    bool usesLocalCoords() const { return fUsesLocalCoords; }

    inline Mode getMode() const { return fMode; }

    class GLProcessor : public GrGLGeometryProcessor {
    public:
        GLProcessor()
            : fViewMatrix(SkMatrix::InvalidMatrix()), fColor(GrColor_ILLEGAL) {}

        void onEmitCode(EmitArgs& args, GrGPArgs* gpArgs) override {
            const DIEllipseEdgeEffect& ee = args.fGP.cast<DIEllipseEdgeEffect>();
            GrGLGPBuilder* pb = args.fPB;
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

            // setup pass through color
            if (!ee.colorIgnored())
                pb->addPassThroughAttribute(ee.inEllipseColor(), args.fOutputColor);

            // Setup position
            this->setupPosition(pb, gpArgs, ee.inPosition()->fName, ee.viewMatrix(),
                                &fViewMatrixUniform);

            if (ee.inLocalCoords()) {
                // emit transforms with explicit local coords
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inLocalCoords()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            } else {
                // emit transforms with position
                this->emitTransforms(args.fPB, gpArgs->fPositionVar, ee.inPosition()->fName,
                                     ee.localMatrix(), args.fTransformsIn, args.fTransformsOut);
            }

            GrGLFragmentBuilder* fsBuilder = args.fPB->getFragmentShaderBuilder();
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
                           const GrGLSLCaps&,
                           GrProcessorKeyBuilder* b) {
            const DIEllipseEdgeEffect& ellipseEffect = gp.cast<DIEllipseEdgeEffect>();
            uint16_t key = ellipseEffect.getMode();
            key |= ellipseEffect.colorIgnored() << 9;
            key |= ComputePosKey(ellipseEffect.viewMatrix()) << 10;
            b->add32(key);
        }

        void setData(const GrGLProgramDataManager& pdman, const GrPrimitiveProcessor& gp) override {
            const DIEllipseEdgeEffect& dee = gp.cast<DIEllipseEdgeEffect>();
            /*if (!dee.viewMatrix().isIdentity() && !fViewMatrix.cheapEqualTo(dee.viewMatrix())) {
                fViewMatrix = dee.viewMatrix();
                fLocalMatrix = dee.localMatrix();
                GrGLfloat viewMatrix[3 * 3];
                GrGLGetMatrix<3>(viewMatrix, fViewMatrix);
                pdman.setMatrix3f(fViewMatrixUniform, viewMatrix);
            }*/

            if (dee.color() != fColor) {
            }
        }

    private:
        SkMatrix fViewMatrix;
        SkMatrix fLocalMatrix;
        GrColor fColor;
        UniformHandle fColorUniform;
        UniformHandle fViewMatrixUniform;

        typedef GrGLGeometryProcessor INHERITED;
    };

    void getGLProcessorKey(const GrGLSLCaps& caps, GrProcessorKeyBuilder* b) const override {
        GLProcessor::GenKey(*this, caps, b);
    }

    GrGLPrimitiveProcessor* createGLInstance(const GrGLSLCaps&) const override {
        return new GLProcessor();
    }

private:
    DIEllipseEdgeEffect(GrColor color, const SkMatrix& viewMatrix, const SkMatrix& localMatrix, Mode mode,
                        bool usesLocalCoords)
        : fColor(color)
        , fViewMatrix(viewMatrix)
        , fLocalMatrix(localMatrix)
        , fUsesLocalCoords(usesLocalCoords) {
        this->initClassID<DIEllipseEdgeEffect>();
        fInPosition = &this->addVertexAttrib(Attribute("inPosition", kVec2f_GrVertexAttribType,
                                                       kHigh_GrSLPrecision));
        fInEllipseOffsets0 = &this->addVertexAttrib(Attribute("inEllipseOffsets0",
                                                              kVec2f_GrVertexAttribType));
        fInEllipseOffsets1 = &this->addVertexAttrib(Attribute("inEllipseOffsets1",
                                                              kVec2f_GrVertexAttribType));
        fInEllipseColor = &this->addVertexAttrib(Attribute("inEllipseColor",
                                                            kVec4ub_GrVertexAttribType));
        fInLocalCoords = NULL;
        if (usesLocalCoords) {
            fInLocalCoords = &this->addVertexAttrib(Attribute("inLocalCoord",
                                                               kVec2f_GrVertexAttribType));
            this->setHasExplicitLocalCoords();
        }
        fMode = mode;
    }

    const Attribute* fInPosition;
    const Attribute* fInEllipseOffsets0;
    const Attribute* fInEllipseOffsets1;
    const Attribute* fInEllipseColor;
    const Attribute* fInLocalCoords;
    GrColor fColor;
    SkMatrix fViewMatrix;
    SkMatrix fLocalMatrix;
    Mode fMode;
    bool fUsesLocalCoords;

    GR_DECLARE_GEOMETRY_PROCESSOR_TEST;

    typedef GrGeometryProcessor INHERITED;
};

GR_DEFINE_GEOMETRY_PROCESSOR_TEST(DIEllipseEdgeEffect);

const GrGeometryProcessor* DIEllipseEdgeEffect::TestCreate(GrProcessorTestData* d) {
    return DIEllipseEdgeEffect::Create(GrRandomColor(d->fRandom),
                                       GrTest::TestMatrix(d->fRandom),
                                       GrTest::TestMatrix(d->fRandom),
                                       (Mode)(d->fRandom->nextRangeU(0,2)),
                                       d->fRandom->nextBool());
}

///////////////////////////////////////////////////////////////////////////////

bool GrOvalRenderer::DrawOval(GrDrawTarget* target,
                              const GrPipelineBuilder& pipelineBuilder,
                              GrColor color,
                              const SkMatrix& viewMatrix,
                              bool useAA,
                              const SkRect& oval,
                              const SkStrokeRec& stroke) {
    bool useCoverageAA = useAA;

    if (!useCoverageAA) {
        return false;
    }

    // we can draw circles
    if (SkScalarNearlyEqual(oval.width(), oval.height()) && circle_stays_circle(viewMatrix)) {
        DrawCircle(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval, stroke);
    // if we have shader derivative support, render as device-independent
    } else if (target->caps()->shaderCaps()->shaderDerivativeSupport()) {
        return DrawDIEllipse(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval,
                             stroke);
    // otherwise axis-aligned ellipses only
    } else if (viewMatrix.rectStaysRect()) {
        return DrawEllipse(target, pipelineBuilder, color, viewMatrix, useCoverageAA, oval,
                           stroke);
    } else {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////

class CircleBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkMatrix fLocalMatrix;
        SkRect fDevBounds;
        SkRect fLocalBounds;
        bool fUsesLocalCoord;
        SkScalar fInnerRadius;
        SkScalar fOuterRadius;
        SkScalar fLocalOuterRadius;
        GrColor fColor;
        bool fStroke;
    };

    static GrDrawBatch* Create(const Geometry& geometry) { return new CircleBatch(geometry); }

    const char* name() const override { return "CircleBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }

    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:
    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    void onPrepareDraws(Target* target) override {
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        // Setup geometry processor
        GrGeometryProcessor *gp = NULL;
        if (this->usesLocalCoords() && !this->stroke()) {
            gp = CircleEdgeEffect::Create(this->color(),
                                          this->stroke(),
                                          this->localMatrix(),
                                          this->usesLocalCoords() && !this->stroke());
        } else {
            gp = CircleEdgeEffect::Create(this->color(),
                                          this->stroke(),
                                          invert,
                                          this->usesLocalCoords() && !this->stroke());
        }

        target->initDraw(gp, this->pipeline());

        int instanceCount = fGeoData.count();
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(CircleVertex));

        InstancedHelper helper;
        CircleVertex *verts = NULL;
        CircleUVVertex *uv_verts = NULL;
        if (this->usesLocalCoords() && !this->stroke()) {
            uv_verts = reinterpret_cast<CircleUVVertex*>(helper.init(target,
                    kTriangles_GrPrimitiveType, vertexStride, gOvalIndexBuffer, 4, 6, instanceCount));
            if (!uv_verts)
                return;
        } else {
            verts = reinterpret_cast<CircleVertex*>(helper.init(target,
                    kTriangles_GrPrimitiveType, vertexStride, gOvalIndexBuffer, 4, 6, instanceCount));
            if (!verts)
                return;
        }

        for (int i = 0; i < instanceCount; i++) {
            Geometry& geom = fGeoData[i];

            if (this->usesLocalCoords() && !this->stroke()) {
                SkPoint pt;

                SkScalar innerRadius = geom.fInnerRadius;
                SkScalar outerRadius = geom.fOuterRadius;
                const SkMatrix& localMatrix = geom.fLocalMatrix;

                const SkRect& bounds = geom.fDevBounds;
                const SkRect& localBounds = geom.fLocalBounds;
                const SkColor& color = geom.fColor;

                (*uv_verts).fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
                (*uv_verts).fOffset = SkPoint::Make(-1,-1);
                (*uv_verts).fOuterRadius = outerRadius;
                (*uv_verts).fInnerRadius = innerRadius;
                (*uv_verts).fColor = color;
                pt.fX = localBounds.fLeft;
                pt.fY = localBounds.fTop;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                    uv_verts++;

                (*uv_verts).fPos = SkPoint::Make(bounds.fLeft, bounds.fBottom);
                (*uv_verts).fOffset = SkPoint::Make(-1, 1);
                (*uv_verts).fOuterRadius = outerRadius;
                (*uv_verts).fInnerRadius = innerRadius;
                (*uv_verts).fColor = color;
                pt.fX = localBounds.fLeft;
                pt.fY = localBounds.fBottom;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;

                uv_verts++;
                (*uv_verts).fPos = SkPoint::Make(bounds.fRight,  bounds.fTop);
                (*uv_verts).fOffset = SkPoint::Make(1, -1);
                (*uv_verts).fOuterRadius = outerRadius;
                (*uv_verts).fInnerRadius = innerRadius;
                (*uv_verts).fColor = color;
                pt.fX = localBounds.fRight;
                pt.fY = localBounds.fTop;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;

                (*uv_verts).fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
                (*uv_verts).fOffset = SkPoint::Make(1, 1);
                (*uv_verts).fOuterRadius = outerRadius;
                (*uv_verts).fInnerRadius = innerRadius;
                (*uv_verts).fColor = color;
                pt.fX = localBounds.fRight;
                pt.fY = localBounds.fBottom;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;

            } else {
                SkScalar innerRadius = geom.fInnerRadius;
                SkScalar outerRadius = geom.fOuterRadius;

                const SkRect& bounds = geom.fDevBounds;

                innerRadius = innerRadius / outerRadius;
                (*verts).fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
                (*verts).fOffset = SkPoint::Make(-1, -1);
                (*verts).fOuterRadius = outerRadius;
                (*verts).fInnerRadius = innerRadius;
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
                (*verts).fOffset = SkPoint::Make(-1, 1);
                (*verts).fOuterRadius = outerRadius;
                (*verts).fInnerRadius = innerRadius;
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
                (*verts).fOffset = SkPoint::Make(1, -1);
                (*verts).fOuterRadius = outerRadius;
                (*verts).fInnerRadius = innerRadius;
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
                (*verts).fOffset = SkPoint::Make(1, 1);
                (*verts).fOuterRadius = outerRadius;
                (*verts).fInnerRadius = innerRadius;
                (*verts).fColor = geom.fColor;
                verts++;
            }
        }
        helper.recordDraw(target);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    CircleBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        this->setBounds(geometry.fDevBounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        CircleBatch* that = t->cast<CircleBatch>();
        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        if (this->stroke() != that->stroke()) {
            return false;
        }

        // We are intended to batch ovals with different colors.
#if 0
        if (this->color() != that->color()) {
            return false;
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }
#endif
        if (this->usesLocalCoords() && this->stroke()) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    const SkMatrix& localMatrix() const { return fGeoData[0].fLocalMatrix; }
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

    typedef GrVertexBatch INHERITED;
};

static GrDrawBatch* create_circle_batch(GrColor color,
                                        const SkMatrix& viewMatrix,
                                        bool useCoverageAA,
                                        const SkRect& circle,
                                        const SkStrokeRec& stroke,
                                        bool canOptimizeforBitmapShader = false,
                                        GrPipelineBuilder *pipelineBuilder = NULL) {

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
    if (pipelineBuilder && canOptimizeforBitmapShader) {
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

    // The radii are outset for two reasons. First, it allows the shader to simply perform simpler
    // computation because the computed alpha is zero, rather than 50%, at the radius.
    // Second, the outer radius is used to compute the verts of the bounding box that is rendered
    // and the outset ensures the box will cover all partially covered by the circle.
    outerRadius += SK_ScalarHalf;
    innerRadius -= SK_ScalarHalf;
    localOuterRadius += SK_ScalarHalf;

    CircleBatch::Geometry geometry;
    geometry.fViewMatrix = viewMatrix;
    geometry.fLocalMatrix = localMatrix;
    geometry.fColor = color;
    geometry.fInnerRadius = innerRadius;
    geometry.fOuterRadius = outerRadius;
    geometry.fUsesLocalCoord = useLocalCoord;
    geometry.fLocalOuterRadius = localOuterRadius;
    geometry.fStroke = isStrokeOnly && innerRadius > 0;
    geometry.fDevBounds = SkRect::MakeLTRB(center.fX - outerRadius, center.fY - outerRadius,
                                           center.fX + outerRadius, center.fY + outerRadius);
    SkRect localBounds = SkRect::MakeLTRB(
        circle.centerX() - localOuterRadius,
        circle.centerY() - localOuterRadius,
        circle.centerX() + localOuterRadius,
        circle.centerY() + localOuterRadius
    );

    geometry.fLocalBounds = localBounds;

    return CircleBatch::Create(geometry);
}

void GrOvalRenderer::DrawCircle(GrDrawTarget* target,
                                const GrPipelineBuilder& pipelineBuilder,
                                GrColor color,
                                const SkMatrix& viewMatrix,
                                bool useCoverageAA,
                                const SkRect& circle,
                                const SkStrokeRec& stroke) {
    GrContext* context = pipelineBuilder.getRenderTarget()->getContext();
    gOvalIndexBuffer = GrOvalRenderer::ovalIndexBuffer(context->getGpu());
    if (NULL == gOvalIndexBuffer) {
        SkDebugf("Failed to create index buffer for oval!\n");
        return;
    }
    GrPipelineBuilder *temp = (GrPipelineBuilder*) &pipelineBuilder;
    SkAutoTUnref<GrDrawBatch> batch(create_circle_batch(color, viewMatrix, useCoverageAA, circle,
                                                        stroke, temp->canOptimizeForBitmapShader(), temp));
    target->drawBatch(pipelineBuilder, batch);
}

///////////////////////////////////////////////////////////////////////////////

class EllipseBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkRect fDevBounds;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        GrColor fColor;
        bool fStroke;
    };

    static GrDrawBatch* Create(const Geometry& geometry) { return new EllipseBatch(geometry); }

    const char* name() const override { return "EllipseBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:
    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsCoverage()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    void onPrepareDraws(Target* target) override {
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        // Setup geometry processor
        SkAutoTUnref<GrGeometryProcessor> gp(EllipseEdgeEffect::Create(this->color(),
                                                                       this->stroke(),
                                                                       invert,
                                                                       false));

        target->initDraw(gp, this->pipeline());

        int instanceCount = fGeoData.count();
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(EllipseVertex));
        InstancedHelper helper;
        EllipseVertex* verts = reinterpret_cast<EllipseVertex*>(helper.init(target,
                kTriangles_GrPrimitiveType, vertexStride, gOvalIndexBuffer, 4, 6, instanceCount));

        if (!verts) {
            return;
        }

        for (int i = 0; i < instanceCount; i++) {
            Geometry& geom = fGeoData[i];

            SkScalar xRadius = geom.fXRadius;
            SkScalar yRadius = geom.fYRadius;

            // Compute the reciprocals of the radii here to save time in the shader
            SkScalar xRadRecip = SkScalarInvert(xRadius);
            SkScalar yRadRecip = SkScalarInvert(yRadius);
            SkScalar xInnerRadRecip = SkScalarInvert(geom.fInnerXRadius);
            SkScalar yInnerRadRecip = SkScalarInvert(geom.fInnerYRadius);

            const SkRect& bounds = geom.fDevBounds;
            (*verts).fPos = SkPoint::Make(bounds.fLeft,  bounds.fTop);
            (*verts).fOffset = SkPoint::Make(-xRadius, -yRadius);
            (*verts).fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
            (*verts).fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
            (*verts).fColor = geom.fColor;
            verts++;

           (*verts).fPos = SkPoint::Make(bounds.fLeft,  bounds.fBottom);
           (*verts).fOffset = SkPoint::Make(-xRadius, yRadius);
           (*verts).fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
           (*verts).fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
           (*verts).fColor = geom.fColor;
           verts++;

           (*verts).fPos = SkPoint::Make(bounds.fRight, bounds.fTop);
           (*verts).fOffset = SkPoint::Make(xRadius, -yRadius);
           (*verts).fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
           (*verts).fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
           (*verts).fColor = geom.fColor;
           verts++;

           (*verts).fPos = SkPoint::Make(bounds.fRight, bounds.fBottom);
           (*verts).fOffset = SkPoint::Make(xRadius, yRadius);
           (*verts).fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
           (*verts).fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
           (*verts).fColor = geom.fColor;
           verts++;
        }
        helper.recordDraw(target);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    EllipseBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        this->setBounds(geometry.fDevBounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        EllipseBatch* that = t->cast<EllipseBatch>();

        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        // TODO use vertex color to avoid breaking batches
        if (this->stroke() != that->stroke()) {
            return false;
        }

        // We are intended to batch ovals with different colors.
#if 0
        if (this->color() != that->color()) {
            return false;
        }
#endif

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        if (this->usesLocalCoords() && this->stroke()) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
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

    typedef GrVertexBatch INHERITED;
};

static GrDrawBatch* create_ellipse_batch(GrColor color,
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
            return nullptr;
        }

        // we don't handle it if curvature of the stroke is less than curvature of the ellipse
        if (scaledStroke.fX*(yRadius*yRadius) < (scaledStroke.fY*scaledStroke.fY)*xRadius ||
            scaledStroke.fY*(xRadius*xRadius) < (scaledStroke.fX*scaledStroke.fX)*yRadius) {
            return nullptr;
        }

        // this is legit only if scale & translation (which should be the case at the moment)
        if (isStrokeOnly) {
            innerXRadius = xRadius - scaledStroke.fX;
            innerYRadius = yRadius - scaledStroke.fY;
        }

        xRadius += scaledStroke.fX;
        yRadius += scaledStroke.fY;
    }

    // We've extended the outer x radius out half a pixel to antialias.
    // This will also expand the rect so all the pixels will be captured.
    // TODO: Consider if we should use sqrt(2)/2 instead
    xRadius += SK_ScalarHalf;
    yRadius += SK_ScalarHalf;

    EllipseBatch::Geometry geometry;
    geometry.fViewMatrix = viewMatrix;
    geometry.fColor = color;
    geometry.fXRadius = xRadius;
    geometry.fYRadius = yRadius;
    geometry.fInnerXRadius = innerXRadius;
    geometry.fInnerYRadius = innerYRadius;
    geometry.fStroke = isStrokeOnly && innerXRadius > 0 && innerYRadius > 0;
    geometry.fDevBounds = SkRect::MakeLTRB(center.fX - xRadius, center.fY - yRadius,
                                           center.fX + xRadius, center.fY + yRadius);

    return EllipseBatch::Create(geometry);
}

bool GrOvalRenderer::DrawEllipse(GrDrawTarget* target,
                                 const GrPipelineBuilder& pipelineBuilder,
                                 GrColor color,
                                 const SkMatrix& viewMatrix,
                                 bool useCoverageAA,
                                 const SkRect& ellipse,
                                 const SkStrokeRec& stroke) {
    GrContext* context = pipelineBuilder.getRenderTarget()->getContext();
    gOvalIndexBuffer = GrOvalRenderer::ovalIndexBuffer(context->getGpu());
    if (NULL == gOvalIndexBuffer) {
        SkDebugf("Failed to create index buffer for oval!\n");
        return false;
    }
    SkAutoTUnref<GrDrawBatch> batch(create_ellipse_batch(color, viewMatrix, useCoverageAA, ellipse,
                                                         stroke));
    if (!batch) {
        return false;
    }

    target->drawBatch(pipelineBuilder, batch);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

class DIEllipseBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkMatrix fVm;
        SkMatrix fLocalMatrix;
        SkRect fBounds;
        SkRect fLocalBounds;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fXLocalRadius;
        SkScalar fYLocalRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        SkScalar fGeoDx;
        SkScalar fGeoDy;
        GrColor fColor;
        SkPoint fCenter;
        DIEllipseEdgeEffect::Mode fMode;
    };

    static GrDrawBatch* Create(const Geometry& geometry, const SkRect& bounds) {
        return new DIEllipseBatch(geometry, bounds);
    }

    const char* name() const override { return "DIEllipseBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:

    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fMode = fGeoData[0].fMode;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    void onPrepareDraws(Target* target) override {
        // Setup geometry processor
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            return;
        }

        GrGeometryProcessor* gp = NULL;
        if (this->usesLocalCoords()) {
            gp = DIEllipseEdgeEffect::Create(this->color(),
                                             SkMatrix::I(),
                                             this->localMatrix(),
                                             this->mode(),
                                             this->usesLocalCoords());
        } else {
            gp = DIEllipseEdgeEffect::Create(this->color(),
                                             SkMatrix::I(),
                                             invert,
                                             this->mode(),
                                             this->usesLocalCoords());
        }

        target->initDraw(gp, this->pipeline());

        int instanceCount = fGeoData.count();
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(DIEllipseVertex));

        InstancedHelper helper;
        DIEllipseVertex *verts = NULL;
        DIEllipseUVVertex *uv_verts = NULL;

        if (this->usesLocalCoords()) {
            uv_verts = reinterpret_cast<DIEllipseUVVertex*>(helper.init(target,
                    kTriangles_GrPrimitiveType, vertexStride, gOvalIndexBuffer, 4, 6, instanceCount));
            if (!uv_verts)
                return;
        } else {
            verts = reinterpret_cast<DIEllipseVertex*>(helper.init(target,
                    kTriangles_GrPrimitiveType, vertexStride, gOvalIndexBuffer, 4, 6, instanceCount));
            if (!verts)
                return;
        }

        for (int i = 0; i < instanceCount; i++) {
            Geometry& geom = fGeoData[i];
            if (this->usesLocalCoords()) {
                SkScalar xRadius = geom.fXRadius;
                SkScalar yRadius = geom.fYRadius;

                const SkRect& bounds = geom.fBounds;
                const SkRect& localBounds = geom.fLocalBounds;
                SkPoint pt;
                const SkMatrix& localMatrix = geom.fLocalMatrix;
                const SkMatrix &vm = geom.fVm;
                SkRect mappedBounds;
                SkPoint points[8];
                SkPoint mappedPoints[8];
                vm.mapRect(&mappedBounds, bounds);

                // This adjusts the "radius" to include the half-pixel border
                SkScalar offsetDx = SkScalarDiv(geom.fGeoDx, xRadius);
                SkScalar offsetDy = SkScalarDiv(geom.fGeoDy, yRadius);
                SkScalar innerRatioX = SkScalarDiv(xRadius, geom.fInnerXRadius);
                SkScalar innerRatioY = SkScalarDiv(yRadius, geom.fInnerYRadius);
                points[0] = SkPoint::Make(-1.0f - offsetDx, -1.0f - offsetDy);
                points[1] = SkPoint::Make(-innerRatioX - offsetDx, -innerRatioY - offsetDy);
                points[2] = SkPoint::Make(1.0f + offsetDx, -1.0f - offsetDy);
                points[3] = SkPoint::Make(innerRatioX + offsetDx, -innerRatioY - offsetDy);
                points[4] = SkPoint::Make(-1.0f - offsetDx, 1.0f + offsetDy);
                points[5] = SkPoint::Make(-innerRatioX - offsetDx, innerRatioY + offsetDy);
                points[6] = SkPoint::Make(1.0f + offsetDx, 1.0f + offsetDy);
                points[7] = SkPoint::Make(innerRatioX + offsetDx, innerRatioY + offsetDy);

                SkScalar leftPt = geom.fCenter.fX - xRadius - geom.fGeoDx;
                SkScalar rightPt = geom.fCenter.fX + xRadius + geom.fGeoDx;
                SkScalar topPt = geom.fCenter.fY - yRadius - geom.fGeoDy;
                SkScalar bottomPt = geom.fCenter.fY + yRadius + geom.fGeoDy;

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

                (*uv_verts).fPos = mappedBoundPts[0];
                (*uv_verts).fOuterOffset = points[0];
                (*uv_verts).fInnerOffset = points[1];
                (*uv_verts).fColor = geom.fColor;
                pt.fX = localBounds.fLeft;
                pt.fY = localBounds.fTop;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;

                (*uv_verts).fPos = mappedBoundPts[1];
                (*uv_verts).fOuterOffset = points[4];
                (*uv_verts).fInnerOffset = points[5];
                (*uv_verts).fColor = geom.fColor;
                pt.fX = localBounds.fLeft;
                pt.fY = localBounds.fBottom;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;

                (*uv_verts).fPos = mappedBoundPts[3];
                (*uv_verts).fOuterOffset = points[2];
                (*uv_verts).fInnerOffset = points[3];
                (*uv_verts).fColor = geom.fColor;
                pt.fX = localBounds.fRight;
                pt.fY = localBounds.fTop;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;

                (*uv_verts).fPos = mappedBoundPts[2];
                (*uv_verts).fOuterOffset = points[6];
                (*uv_verts).fInnerOffset = points[7];
                (*uv_verts).fColor = geom.fColor;
                pt.fX = localBounds.fRight;
                pt.fY = localBounds.fBottom;
                localMatrix.mapPoints(&pt, 1);
                (*uv_verts).fLocalPos = pt;
                uv_verts++;
            } else {
                SkScalar xRadius = geom.fXRadius;
                SkScalar yRadius = geom.fYRadius;

                const SkRect& bounds = geom.fBounds;

                const SkMatrix &vm = geom.fVm;
                SkRect mappedBounds;
                SkPoint points[8];
                SkPoint mappedPoints[8];
                vm.mapRect(&mappedBounds, bounds);

                // This adjusts the "radius" to include the half-pixel border
                SkScalar offsetDx = SkScalarDiv(geom.fGeoDx, xRadius);
                SkScalar offsetDy = SkScalarDiv(geom.fGeoDy, yRadius);
                SkScalar innerRatioX = SkScalarDiv(xRadius, geom.fInnerXRadius);
                SkScalar innerRatioY = SkScalarDiv(yRadius, geom.fInnerYRadius);
                points[0] = SkPoint::Make(-1.0f - offsetDx, -1.0f - offsetDy);
                points[1] = SkPoint::Make(-innerRatioX - offsetDx, -innerRatioY - offsetDy);
                points[2] = SkPoint::Make(1.0f + offsetDx, -1.0f - offsetDy);
                points[3] = SkPoint::Make(innerRatioX + offsetDx, -innerRatioY - offsetDy);
                points[4] = SkPoint::Make(-1.0f - offsetDx, 1.0f + offsetDy);
                points[5] = SkPoint::Make(-innerRatioX - offsetDx, innerRatioY + offsetDy);
                points[6] = SkPoint::Make(1.0f + offsetDx, 1.0f + offsetDy);
                points[7] = SkPoint::Make(innerRatioX + offsetDx, innerRatioY + offsetDy);

                SkScalar leftPt = geom.fCenter.fX - xRadius - geom.fGeoDx;
                SkScalar rightPt = geom.fCenter.fX + xRadius + geom.fGeoDx;
                SkScalar topPt = geom.fCenter.fY - yRadius - geom.fGeoDy;
                SkScalar bottomPt = geom.fCenter.fY + yRadius + geom.fGeoDy;

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

                (*verts).fPos = mappedBoundPts[0];
                (*verts).fOuterOffset = points[0];
                (*verts).fInnerOffset = points[1];
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = mappedBoundPts[1];
                (*verts).fOuterOffset = points[4];
                (*verts).fInnerOffset = points[5];
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = mappedBoundPts[3];
                (*verts).fOuterOffset = points[2];
                (*verts).fInnerOffset = points[3];
                (*verts).fColor = geom.fColor;
                verts++;

                (*verts).fPos = mappedBoundPts[2];
                (*verts).fOuterOffset = points[6];
                (*verts).fInnerOffset = points[7];
                (*verts).fColor = geom.fColor;
                verts++;
            }
        }
        helper.recordDraw(target);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    DIEllipseBatch(const Geometry& geometry, const SkRect& bounds) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        this->setBounds(bounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        DIEllipseBatch* that = t->cast<DIEllipseBatch>();
        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        // We are intended to batch ovals with different colors.
#if 0
        if (this->color() != that->color()) {
            return false;
        }
#endif

        if (this->mode() != that->mode()) {
            return false;
        }

        // TODO rewrite to allow positioning on CPU
        if (!this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    const SkMatrix& localMatrix() const { return fGeoData[0].fLocalMatrix; }
    DIEllipseEdgeEffect::Mode mode() const { return fBatch.fMode; }

    struct BatchTracker {
        GrColor fColor;
        DIEllipseEdgeEffect::Mode fMode;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;

    typedef GrVertexBatch INHERITED;
};

static GrDrawBatch* create_diellipse_batch(GrColor color,
                                           const SkMatrix& viewMatrix,
                                           bool useCoverageAA,
                                           const SkRect& ellipse,
                                           const SkStrokeRec& stroke,
                                           bool canOptimizeforBitmapShader = false,
                                           GrPipelineBuilder *pipelineBuilder = NULL) {


    SkMatrix localMatrix;

    const SkMatrix &vm = viewMatrix;
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
            return nullptr;
        }

        // we don't handle it if curvature of the stroke is less than curvature of the ellipse
        if (strokeWidth*(yRadius*yRadius) < (strokeWidth*strokeWidth)*xRadius ||
            strokeWidth*(xRadius*xRadius) < (strokeWidth*strokeWidth)*yRadius) {
            return nullptr;
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
        localMatrix = lm;
    }

    if (DIEllipseEdgeEffect::kStroke == mode) {
        mode = (innerXRadius > 0 && innerYRadius > 0) ? DIEllipseEdgeEffect::kStroke :
                                                        DIEllipseEdgeEffect::kFill;
    }

    // This expands the outer rect so that after CTM we end up with a half-pixel border

    SkScalar a = vm[SkMatrix::kMScaleX];
    SkScalar b = vm[SkMatrix::kMSkewX];
    SkScalar c = vm[SkMatrix::kMSkewY];
    SkScalar d = vm[SkMatrix::kMScaleY];
    SkScalar geoDx = SK_ScalarHalf / SkScalarSqrt(a*a + c*c);
    SkScalar geoDy = SK_ScalarHalf / SkScalarSqrt(b*b + d*d);

    xLocalRadius += SK_ScalarHalf;
    yLocalRadius += SK_ScalarHalf;

    SkRect localBounds = SkRect::MakeLTRB(
        localCenter.fX - xLocalRadius,
        localCenter.fY - yLocalRadius,
        localCenter.fX + xLocalRadius,
        localCenter.fY + yLocalRadius
    );

    DIEllipseBatch::Geometry geometry;
    geometry.fViewMatrix = SkMatrix::I();
    geometry.fLocalMatrix = localMatrix;
    geometry.fVm = vm;
    geometry.fColor = color;
    geometry.fXRadius = xRadius;
    geometry.fYRadius = yRadius;
    geometry.fXLocalRadius = xLocalRadius;
    geometry.fYLocalRadius = yLocalRadius;
    geometry.fInnerXRadius = innerXRadius;
    geometry.fInnerYRadius = innerYRadius;
    geometry.fGeoDx = geoDx;
    geometry.fGeoDy = geoDy;
    geometry.fMode = mode;
    geometry.fCenter = center;
    geometry.fBounds = SkRect::MakeLTRB(center.fX - xRadius - geoDx, center.fY - yRadius - geoDy,
                                        center.fX + xRadius + geoDx, center.fY + yRadius + geoDy);
    geometry.fLocalBounds = localBounds;

    SkRect devBounds = geometry.fBounds;
    viewMatrix.mapRect(&devBounds);
    return DIEllipseBatch::Create(geometry, devBounds);
}

bool GrOvalRenderer::DrawDIEllipse(GrDrawTarget* target,
                                   const GrPipelineBuilder& pipelineBuilder,
                                   GrColor color,
                                   const SkMatrix& viewMatrix,
                                   bool useCoverageAA,
                                   const SkRect& ellipse,
                                   const SkStrokeRec& stroke) {
    GrContext* context = pipelineBuilder.getRenderTarget()->getContext();
    gOvalIndexBuffer = GrOvalRenderer::ovalIndexBuffer(context->getGpu());
    if (NULL == gOvalIndexBuffer) {
        SkDebugf("Failed to create index buffer for oval!\n");
        return false;
    }
    GrPipelineBuilder *temp = (GrPipelineBuilder*) &pipelineBuilder;
    SkAutoTUnref<GrDrawBatch> batch(create_diellipse_batch(color, viewMatrix, useCoverageAA,
                                                           ellipse, stroke,temp->canOptimizeForBitmapShader(), temp));
    if (!batch) {
        return false;
    }
    target->drawBatch(pipelineBuilder, batch);
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

GR_DECLARE_STATIC_UNIQUE_KEY(gStrokeRRectOnlyIndexBufferKey);
GR_DECLARE_STATIC_UNIQUE_KEY(gRRectOnlyIndexBufferKey);
static const GrIndexBuffer* ref_rrect_index_buffer(bool strokeOnly,
                                                   GrResourceProvider* resourceProvider) {
    GR_DEFINE_STATIC_UNIQUE_KEY(gStrokeRRectOnlyIndexBufferKey);
    GR_DEFINE_STATIC_UNIQUE_KEY(gRRectOnlyIndexBufferKey);
    if (strokeOnly) {
        return resourceProvider->findOrCreateInstancedIndexBuffer(
            gRRectIndices, kIndicesPerStrokeRRect, kNumRRectsInIndexBuffer, kVertsPerRRect,
            gStrokeRRectOnlyIndexBufferKey);
    } else {
        return resourceProvider->findOrCreateInstancedIndexBuffer(
            gRRectIndices, kIndicesPerRRect, kNumRRectsInIndexBuffer, kVertsPerRRect,
            gRRectOnlyIndexBufferKey);

    }
}

bool GrOvalRenderer::DrawDRRect(GrDrawTarget* target,
                                const GrPipelineBuilder& pipelineBuilder,
                                GrColor color,
                                const SkMatrix& viewMatrix,
                                bool useAA,
                                const SkRRect& origOuter,
                                const SkRRect& origInner) {
    bool applyAA = useAA && !pipelineBuilder.getRenderTarget()->isUnifiedMultisampled();
    GrPipelineBuilder::AutoRestoreFragmentProcessorState arfps;
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
        if (nullptr == fp) {
            return false;
        }
        arfps.set(&pipelineBuilder);
        arfps.addCoverageFragmentProcessor(fp)->unref();
    }

    SkStrokeRec fillRec(SkStrokeRec::kFill_InitStyle);
    if (DrawRRect(target, pipelineBuilder, color, viewMatrix, useAA, origOuter, fillRec)) {
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
    if (nullptr == effect) {
        return false;
    }
    if (!arfps.isSet()) {
        arfps.set(&pipelineBuilder);
    }

    SkMatrix invert;
    if (!viewMatrix.invert(&invert)) {
        return false;
    }

    arfps.addCoverageFragmentProcessor(effect)->unref();
    SkRect bounds = outer->getBounds();
    if (applyAA) {
        bounds.outset(SK_ScalarHalf, SK_ScalarHalf);
    }
    target->drawNonAARect(pipelineBuilder, color, SkMatrix::I(), bounds, invert);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class RRectCircleRendererBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkMatrix fLocalMatrix;
        SkRect fDevBounds;
        SkRect fLocalBounds;
        SkScalar fInnerRadius;
        SkScalar fOuterRadius;
        SkScalar fLocalOuterRadius;
        GrColor fColor;
        bool fStroke;
    };

    static GrDrawBatch* Create(const Geometry& geometry) {
        return new RRectCircleRendererBatch(geometry);
    }

    const char* name() const override { return "RRectCircleBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:
    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    void onPrepareDraws(Target* target) override {
        // reset to device coordinates
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return;
        }

        // Setup geometry processor
        GrGeometryProcessor *gp = NULL;
        if(this->usesLocalCoords() && !this->stroke()) {
            gp = CircleEdgeEffect::Create (this->color(),
                                           this->stroke(),
                                           this->localMatrix(),
                                           this->usesLocalCoords());
        } else {
            gp = CircleEdgeEffect::Create (this->color(),
                                           this->stroke(),
                                           invert,
                                           this->usesLocalCoords() && !this->stroke());
        }

        target->initDraw(gp, this->pipeline());

        int instanceCount = fGeoData.count();
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(CircleVertex));

        SkAutoTUnref<const GrIndexBuffer> indexBuffer(
            ref_rrect_index_buffer(this->stroke(), target->resourceProvider()));

        InstancedHelper helper;
        CircleVertex* verts = NULL;
        CircleUVVertex* uv_verts = NULL;
        if(this->usesLocalCoords() && !this->stroke()) {
		    if (this->stroke()) {
		        uv_verts  = reinterpret_cast<CircleUVVertex*>(helper.init(target,
                            kTriangles_GrPrimitiveType, vertexStride, gRectStrokeIndexBuffer, 16,
                            SK_ARRAY_COUNT(gRRectStrokeIndices), instanceCount));
		    } else {
		        uv_verts  = reinterpret_cast<CircleUVVertex*>(helper.init(target,
                            kTriangles_GrPrimitiveType, vertexStride, gRectFillIndexBuffer, 16,
                            SK_ARRAY_COUNT(gRRectIndices), instanceCount));
		    }
            if (!uv_verts) {
                SkDebugf("Could not allocate vertices\n");
                return;
            }
        } else{
            if (this->stroke()) {
                verts  = reinterpret_cast<CircleVertex*>(helper.init(target,
                            kTriangles_GrPrimitiveType, vertexStride, gRectStrokeIndexBuffer, 16,
                            SK_ARRAY_COUNT(gRRectStrokeIndices), instanceCount));
            } else {
             verts  = reinterpret_cast<CircleVertex*>(helper.init(target,
                            kTriangles_GrPrimitiveType, vertexStride, gRectFillIndexBuffer, 16,
                            SK_ARRAY_COUNT(gRRectIndices), instanceCount));
		    }
            if (!verts) {
                SkDebugf("Could not allocate vertices\n");
                return;
            }
        }

        for (int i = 0; i < instanceCount; i++) {

            Geometry& args = fGeoData[i];
            if(this->usesLocalCoords() && !this->stroke()) {
                const SkRect& localBounds = args.fLocalBounds;
                const SkMatrix& localMatrix = args.fLocalMatrix;
                SkScalar localOuterRadius = args.fLocalOuterRadius;
                SkScalar yLocalCoords[4] = {
                    localBounds.fTop,
                    localBounds.fTop + localOuterRadius,
                    localBounds.fBottom - localOuterRadius,
                    localBounds.fBottom
                };
                SkScalar outerRadius = args.fOuterRadius;

                const SkRect& bounds = args.fDevBounds;

                SkScalar yCoords[4] = {
                    bounds.fTop,
                    bounds.fTop + outerRadius,
                    bounds.fBottom - outerRadius,
                    bounds.fBottom
                };

                SkScalar yOuterRadii[4] = {-1, 0, 0, 1 };
                SkPoint localPt;
                SkPoint mappedPt;
                // The inner radius in the vertex data must be specified in normalized space.
                SkScalar innerRadius = args.fInnerRadius / args.fOuterRadius;
                for (int i = 0; i < 4; ++i) {
                    uv_verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(-1, yOuterRadii[i]);
                    uv_verts->fOuterRadius = outerRadius;
                    uv_verts->fInnerRadius = innerRadius;
                    uv_verts->fColor = args.fColor;

                    localPt.fX = localBounds.fLeft;
                    localPt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &localPt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fLeft + outerRadius, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                    uv_verts->fOuterRadius = outerRadius;
                    uv_verts->fInnerRadius = innerRadius;
                    uv_verts->fColor = args.fColor;

                    localPt.fX = localBounds.fLeft + localOuterRadius;
                    localPt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &localPt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fRight - outerRadius, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                    uv_verts->fOuterRadius = outerRadius;
                    uv_verts->fInnerRadius = innerRadius;
                    uv_verts->fColor = args.fColor;

                    localPt.fX = localBounds.fRight - localOuterRadius;
                    localPt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &localPt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(1, yOuterRadii[i]);
                    uv_verts->fOuterRadius = outerRadius;
                    uv_verts->fInnerRadius = innerRadius;
                    uv_verts->fColor = args.fColor;

                    localPt.fX = localBounds.fRight;
                    localPt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &localPt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;
                }
            } else {
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
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fLeft + outerRadius, yCoords[i]);
                    verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                    verts->fOuterRadius = outerRadius;
                    verts->fInnerRadius = innerRadius;
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fRight - outerRadius, yCoords[i]);
                    verts->fOffset = SkPoint::Make(0, yOuterRadii[i]);
                    verts->fOuterRadius = outerRadius;
                    verts->fInnerRadius = innerRadius;
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                    verts->fOffset = SkPoint::Make(1, yOuterRadii[i]);
                    verts->fOuterRadius = outerRadius;
                    verts->fInnerRadius = innerRadius;
                    verts->fColor = args.fColor;
                    verts++;
                }
            }
        }
        helper.recordDraw(target);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    RRectCircleRendererBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        this->setBounds(geometry.fDevBounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        RRectCircleRendererBatch* that = t->cast<RRectCircleRendererBatch>();
        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        // We are intended to batch ovals with different colors.
#if 0
        if (this->color() != that->color()) {
            return false;
        }
#endif

        if (this->stroke() != that->stroke()) {
            return false;
        }

#if 0
        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }
#endif

        if (this->usesLocalCoords() && this->stroke()) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    const SkMatrix& localMatrix() const { return fGeoData[0].fLocalMatrix; }
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

    typedef GrVertexBatch INHERITED;
};

class RRectEllipseRendererBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkMatrix fLocalMatrix;
        SkRect fDevBounds;
        SkRect fLocalBounds;
        SkScalar fXRadius;
        SkScalar fYRadius;
        SkScalar fXLocalRadius;
        SkScalar fYLocalRadius;
        SkScalar fInnerXRadius;
        SkScalar fInnerYRadius;
        GrColor fColor;
        bool fStroke;
    };

    static GrDrawBatch* Create(const Geometry& geometry) {
        return new RRectEllipseRendererBatch(geometry);
    }

    const char* name() const override { return "RRectEllipseRendererBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:
    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fStroke = fGeoData[0].fStroke;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    void onPrepareDraws(Target* target) override {
        // reset to device coordinates
        SkMatrix invert;
        if (!this->viewMatrix().invert(&invert)) {
            SkDebugf("Failed to invert\n");
            return;
        }

        // Setup geometry processor
        GrGeometryProcessor *gp = NULL;
        if (this->usesLocalCoords() && !this->stroke()) {
            gp = (EllipseEdgeEffect::Create(this->color(),
                                            this->stroke(),
                                            this->localMatrix(),
                                            this->usesLocalCoords()));
        } else {
           gp = (EllipseEdgeEffect::Create(this->color(),
                                           this->stroke(),
                                           invert,
                                           this->usesLocalCoords() && !this->stroke()));
        }

        target->initDraw(gp, this->pipeline());

        int instanceCount = fGeoData.count();
        size_t vertexStride = gp->getVertexStride();
        SkASSERT(vertexStride == sizeof(EllipseVertex));

        SkAutoTUnref<const GrIndexBuffer> indexBuffer(
            ref_rrect_index_buffer(this->stroke(), target->resourceProvider()));

        InstancedHelper helper;
        EllipseVertex* verts = NULL;
        EllipseUVVertex* uv_verts = NULL;
        if (this->usesLocalCoords() && !this->stroke()) {
            if (this->stroke()) {
                uv_verts = reinterpret_cast<EllipseUVVertex*>(helper.init(target,
                        kTriangles_GrPrimitiveType, vertexStride, gRectStrokeIndexBuffer, 16,
                        SK_ARRAY_COUNT(gRRectStrokeIndices), instanceCount));
            } else {
                uv_verts = reinterpret_cast<EllipseUVVertex*>(helper.init(target,
                        kTriangles_GrPrimitiveType, vertexStride, gRectFillIndexBuffer, 16,
                        SK_ARRAY_COUNT(gRRectIndices), instanceCount));
            }
            if (!uv_verts) {
                SkDebugf("Could not allocate vertices\n");
                return;
            }
        } else {
            if (this->stroke()) {
                verts = reinterpret_cast<EllipseVertex*>(helper.init(target,
                        kTriangles_GrPrimitiveType, vertexStride, gRectStrokeIndexBuffer, 16,
                        SK_ARRAY_COUNT(gRRectStrokeIndices), instanceCount));
            } else {
                verts = reinterpret_cast<EllipseVertex*>(helper.init(target,
                        kTriangles_GrPrimitiveType, vertexStride, gRectFillIndexBuffer, 16,
                        SK_ARRAY_COUNT(gRRectIndices), instanceCount));
            }
            if (!verts) {
                SkDebugf("Could not allocate vertices\n");
                return;
            }
        }

        for (int i = 0; i < instanceCount; i++) {
            Geometry& args = fGeoData[i];
            if (this->usesLocalCoords() && !this->stroke()) {
                // Compute the reciprocals of the radii here to save time in the shader
                SkScalar xRadRecip = SkScalarInvert(args.fXRadius);
                SkScalar yRadRecip = SkScalarInvert(args.fYRadius);
                SkScalar xInnerRadRecip = SkScalarInvert(args.fInnerXRadius);
                SkScalar yInnerRadRecip = SkScalarInvert(args.fInnerYRadius);
                SkScalar xLocalRadius = args.fXLocalRadius;
                SkScalar yLocalRadius = args.fYLocalRadius;
                const SkMatrix& localMatrix = args.fLocalMatrix;

                SkPoint pt;
                SkPoint mappedPt;
                SkScalar xLocalOuterRadius = xLocalRadius + SK_ScalarHalf;
                SkScalar yLocalOuterRadius = yLocalRadius + SK_ScalarHalf;

                // Extend the radii out half a pixel to antialias.
                SkScalar xOuterRadius = args.fXRadius + SK_ScalarHalf;
                SkScalar yOuterRadius = args.fYRadius + SK_ScalarHalf;

                const SkRect& bounds = args.fDevBounds;
                const SkRect& localBounds = args.fLocalBounds;

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

                for (int i = 0; i < 4; ++i) {
                    uv_verts->fPos = SkPoint::Make(bounds.fLeft, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                    uv_verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    uv_verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    uv_verts->fColor = args.fColor;

                    pt.fX = localBounds.fLeft;
                    pt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &pt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fLeft + xOuterRadius, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                    uv_verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    uv_verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);

                    pt.fX = localBounds.fLeft + xLocalOuterRadius;
                    pt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &pt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts->fColor = args.fColor;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fRight - xOuterRadius, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                    uv_verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    uv_verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    uv_verts->fColor = args.fColor;

                    pt.fX = localBounds.fRight - xLocalOuterRadius;
                    pt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &pt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;

                    uv_verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                    uv_verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                    uv_verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    uv_verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    uv_verts->fColor = args.fColor;

                    pt.fX = localBounds.fRight;
                    pt.fY = yLocalCoords[i];
                    localMatrix.mapPoints(&mappedPt, &pt, 1);
                    uv_verts->fLocalPos = mappedPt;
                    uv_verts++;
                }
            } else {
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
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fLeft + xOuterRadius, yCoords[i]);
                    verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                    verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fRight - xOuterRadius, yCoords[i]);
                    verts->fOffset = SkPoint::Make(SK_ScalarNearlyZero, yOuterOffsets[i]);
                    verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    verts->fColor = args.fColor;
                    verts++;

                    verts->fPos = SkPoint::Make(bounds.fRight, yCoords[i]);
                    verts->fOffset = SkPoint::Make(xOuterRadius, yOuterOffsets[i]);
                    verts->fOuterRadii = SkPoint::Make(xRadRecip, yRadRecip);
                    verts->fInnerRadii = SkPoint::Make(xInnerRadRecip, yInnerRadRecip);
                    verts->fColor = args.fColor;
                    verts++;
                }
            }
        }
        helper.recordDraw(target);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    RRectEllipseRendererBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        this->setBounds(geometry.fDevBounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        RRectEllipseRendererBatch* that = t->cast<RRectEllipseRendererBatch>();

        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        // We are intended to batch ovals with different colors.
#if 0
        if (this->color() != that->color()) {
            return false;
        }
#endif

        if (this->stroke() != that->stroke()) {
            return false;
        }

#if 0
        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }
#endif

        if (this->usesLocalCoords() && this->stroke()) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    const SkMatrix& localMatrix() const { return fGeoData[0].fLocalMatrix; }
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

    typedef GrVertexBatch INHERITED;
};

static GrDrawBatch* create_rrect_batch(GrColor color,
                                       const SkMatrix& viewMatrix,
                                       const SkRRect& rrect,
                                       const SkStrokeRec& stroke,
                                       bool canOptimizeforBitmapShader = false,
                                       GrPipelineBuilder *pipelineBuilder = NULL) {
    SkASSERT(viewMatrix.rectStaysRect());
    SkASSERT(rrect.isSimple());
    SkASSERT(!rrect.isOval());

    // RRect batchs only handle simple, but not too simple, rrects
    // do any matrix crunching before we reset the draw state for device coords
    const SkRect& rrectBounds = rrect.getBounds();
    SkRect bounds;
    viewMatrix.mapRect(&bounds, rrectBounds);

    SkRect localBounds = rrectBounds;
    SkMatrix localMatrix;
    bool useLocalCoord = false;

    SkVector radii = rrect.getSimpleRadii();
    SkScalar xRadius = SkScalarAbs(viewMatrix[SkMatrix::kMScaleX]*radii.fX +
                                   viewMatrix[SkMatrix::kMSkewY]*radii.fY);
    SkScalar yRadius = SkScalarAbs(viewMatrix[SkMatrix::kMSkewX]*radii.fX +
                                   viewMatrix[SkMatrix::kMScaleY]*radii.fY);

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
            scaledStroke.fX = SkScalarAbs(strokeWidth*(viewMatrix[SkMatrix::kMScaleX] +
                                                       viewMatrix[SkMatrix::kMSkewY]));
            scaledStroke.fY = SkScalarAbs(strokeWidth*(viewMatrix[SkMatrix::kMSkewX] +
                                                       viewMatrix[SkMatrix::kMScaleY]));
        }

        // if half of strokewidth is greater than radius, we don't handle that right now
        if (SK_ScalarHalf*scaledStroke.fX > xRadius || SK_ScalarHalf*scaledStroke.fY > yRadius) {
            return nullptr;
        }
    }

    // The way the effect interpolates the offset-to-ellipse/circle-center attribute only works on
    // the interior of the rrect if the radii are >= 0.5. Otherwise, the inner rect of the nine-
    // patch will have fractional coverage. This only matters when the interior is actually filled.
    // We could consider falling back to rect rendering here, since a tiny radius is
    // indistinguishable from a square corner.
    if (!isStrokeOnly && (SK_ScalarHalf > xRadius || SK_ScalarHalf > yRadius)) {
        return nullptr;
    }

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

        RRectCircleRendererBatch::Geometry geometry;
        geometry.fLocalOuterRadius = localOuterRadius;
        geometry.fViewMatrix = viewMatrix;
        geometry.fLocalMatrix = localMatrix;
        geometry.fColor = color;
        geometry.fInnerRadius = innerRadius;
        geometry.fOuterRadius = outerRadius;
        geometry.fStroke = isStrokeOnly;
        geometry.fDevBounds = bounds;
        geometry.fLocalBounds = localBounds;

        return RRectCircleRendererBatch::Create(geometry);
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
                return nullptr;
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
                return nullptr;
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

        RRectEllipseRendererBatch::Geometry geometry;
        geometry.fViewMatrix = viewMatrix;
        geometry.fLocalMatrix = localMatrix;
        geometry.fColor = color;
        geometry.fXRadius = xRadius;
        geometry.fYRadius = yRadius;
        geometry.fXLocalRadius = xLocalRadius;
        geometry.fYLocalRadius = yLocalRadius;
        geometry.fInnerXRadius = innerXRadius;
        geometry.fInnerYRadius = innerYRadius;
        geometry.fStroke = isStrokeOnly;
        geometry.fDevBounds = bounds;
        geometry.fLocalBounds = localBounds;

        return RRectEllipseRendererBatch::Create(geometry);
    }
}

static inline void fill_indices(uint16_t *indices, const uint16_t *src,
                                const int srcSize, const int indicesCount, const int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < srcSize; j++)
            indices[i * srcSize + j] = src[j] + i * indicesCount;
    }
}

GrIndexBuffer* GrOvalRenderer::rectFillIndexBuffer(GrGpu* gpu) {
    if (NULL == gRectFillIndexBuffer) {
        static const int SIZE = sizeof(gRRectIndices) * MAX_RRECTS;
        gRectFillIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != gRectFillIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)gRectFillIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gRRectIndices,
                             sizeof(gRRectIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                gRectFillIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gRRectIndices),
                             sizeof(gRRectIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                if (!gRectFillIndexBuffer->updateData(indices, SIZE)) {
                    gRectFillIndexBuffer->unref();
                    gRectFillIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return gRectFillIndexBuffer;
}

GrIndexBuffer* GrOvalRenderer::rectStrokeIndexBuffer(GrGpu* gpu) {
    if (NULL == gRectStrokeIndexBuffer) {
        static const int SIZE = sizeof(gRRectStrokeIndices) * MAX_RRECTS;
        gRectStrokeIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != gRectStrokeIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)gRectStrokeIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gRRectStrokeIndices,
                             sizeof(gRRectStrokeIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                gRectStrokeIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gRRectStrokeIndices),
                             sizeof(gRRectStrokeIndices)/sizeof(uint16_t),
                             16, MAX_RRECTS);
                if (!gRectStrokeIndexBuffer->updateData(indices, SIZE)) {
                    gRectStrokeIndexBuffer->unref();
                    gRectStrokeIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return gRectStrokeIndexBuffer;
}

static const uint16_t gOvalIndices[] = {
    // corners
    0, 1, 2, 1, 2, 3
};

GrIndexBuffer* GrOvalRenderer::ovalIndexBuffer(GrGpu *gpu) {
    if (NULL == gOvalIndexBuffer) {
        static const int SIZE = sizeof(gOvalIndices) * MAX_OVALS;
        gOvalIndexBuffer = gpu->createIndexBuffer(SIZE, false);
        if (NULL != gOvalIndexBuffer) {
            // FIXME use lock()/unlock() when port to later revision
            uint16_t *indices = (uint16_t *)gOvalIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, gOvalIndices,
                             sizeof(gOvalIndices)/sizeof(uint16_t),
                             4, MAX_OVALS);
                gOvalIndexBuffer->unmap();
            } else {
                indices = (uint16_t *)sk_malloc_throw(SIZE);
                fill_indices(indices, static_cast<const uint16_t *>(gOvalIndices),
                             sizeof(gOvalIndices) / sizeof(uint16_t),
                             4, MAX_OVALS);
                if (!gOvalIndexBuffer->updateData(indices, SIZE)) {
                    gOvalIndexBuffer->unref();
                    gOvalIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return gOvalIndexBuffer;
}

bool GrOvalRenderer::DrawRRect(GrDrawTarget* target,
                               const GrPipelineBuilder& pipelineBuilder,
                               GrColor color,
                               const SkMatrix& viewMatrix,
                               bool useAA,
                               const SkRRect& rrect,
                               const SkStrokeRec& stroke) {
    const SkMatrix vm = viewMatrix;
    if (rrect.isOval()) {
        return DrawOval(target, pipelineBuilder, color, viewMatrix, useAA, rrect.getBounds(),
                        stroke);
    }

    bool useCoverageAA = useAA;

    // only anti-aliased rrects for now
    if (!useCoverageAA) {
        return false;
    }

    if (!vm.rectStaysRect() || !rrect.isSimple()) {
        return false;
    }

    SkStrokeRec::Style style = stroke.getStyle();

    bool isStrokeOnly = SkStrokeRec::kStroke_Style == style ||
    SkStrokeRec::kHairline_Style == style;
    bool hasStroke = isStrokeOnly || SkStrokeRec::kStrokeAndFill_Style == style;

    if (!hasStroke)
    {
        GrContext* context = pipelineBuilder.getRenderTarget()->getContext();
        gRectFillIndexBuffer = GrOvalRenderer::rectFillIndexBuffer(context->getGpu());
        if (NULL == gRectFillIndexBuffer) {
            SkDebugf("Failed to create index buffer for oval!\n");
            return false;
        }
    } else {
        GrContext* context = pipelineBuilder.getRenderTarget()->getContext();
        gRectStrokeIndexBuffer = GrOvalRenderer::rectStrokeIndexBuffer(context->getGpu());
        if (NULL == gRectStrokeIndexBuffer) {
            SkDebugf("Failed to create index buffer for oval!\n");
            return false;
        }
    }

    GrPipelineBuilder *temp = (GrPipelineBuilder*) &pipelineBuilder;
    SkAutoTUnref<GrDrawBatch> batch(create_rrect_batch(color, vm, rrect, stroke, temp->canOptimizeForBitmapShader(), temp));
    if (!batch) {
        return false;
    }

    target->drawBatch(pipelineBuilder, batch);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef GR_TEST_UTILS

DRAW_BATCH_TEST_DEFINE(CircleBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrix(random);
    GrColor color = GrRandomColor(random);
    bool useCoverageAA = random->nextBool();
    SkRect circle = GrTest::TestSquare(random);
    return create_circle_batch(color, viewMatrix, useCoverageAA, circle,
                               GrTest::TestStrokeRec(random));
}

DRAW_BATCH_TEST_DEFINE(EllipseBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrixRectStaysRect(random);
    GrColor color = GrRandomColor(random);
    SkRect ellipse = GrTest::TestSquare(random);
    return create_ellipse_batch(color, viewMatrix, true, ellipse,
                                GrTest::TestStrokeRec(random));
}

DRAW_BATCH_TEST_DEFINE(DIEllipseBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrix(random);
    GrColor color = GrRandomColor(random);
    bool useCoverageAA = random->nextBool();
    SkRect ellipse = GrTest::TestSquare(random);
    return create_diellipse_batch(color, viewMatrix, useCoverageAA, ellipse,
                                  GrTest::TestStrokeRec(random));
}

DRAW_BATCH_TEST_DEFINE(RRectBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrixRectStaysRect(random);
    GrColor color = GrRandomColor(random);
    const SkRRect& rrect = GrTest::TestRRectSimple(random);
    return create_rrect_batch(color, viewMatrix, rrect, GrTest::TestStrokeRec(random));
}

#endif
