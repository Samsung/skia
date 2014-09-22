
/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "SkShapeStrokerPriv.h"
#include "SkGeometry.h"
#include "SkPath.h"

static bool is_clockwise(const SkVector& before, const SkVector& after)
{
    return SkScalarMul(before.fX, after.fY) - SkScalarMul(before.fY, after.fX) > 0;
}

static void ButtCapper(SkPath* path, const SkPoint& pivot,
                       const SkVector& normal,
                       SkScalar radius,
                       const SkPoint& start, const SkPoint& stop)
{
    path->close();
}

static void RoundCapper(SkPath* path, const SkPoint& pivot,
                        const SkVector& normal,
                        SkScalar radius,
                        const SkPoint& start, const SkPoint& stop)
{
    SkVector            before = normal;
    SkVector            after = -normal;
    SkRotationDirection dir = kCW_SkRotationDirection;

    SkPoint     pts[kSkBuildQuadArcStorage];
    SkMatrix    matrix;
    matrix.setScale(radius, radius);
    matrix.postTranslate(pivot.fX, pivot.fY);
    bool swap = !is_clockwise(before, after);
    int count;

    if (swap) {
        dir = kCCW_SkRotationDirection;
        count = SkBuildQuadArc(after, before, dir, &matrix, pts);
    }
    else
        count = SkBuildQuadArc(before, after, dir, &matrix, pts);

    SkASSERT((count & 1) == 1);

    if (count > 1)
    {
        path->moveTo(pivot.fX, pivot.fY);
        if (swap)
            path->lineTo(stop.fX, stop.fY);
        else
            path->lineTo(start.fX, start.fY);
        for (int i = 1; i < count; i += 2)
            path->quadTo(pts[i].fX, pts[i].fY, pts[i+1].fX, pts[i+1].fY);
        path->close();
    }
}

static void SquareCapper(SkPath* path, const SkPoint& pivot,
                         const SkVector& normal,
                         SkScalar radius,
                         const SkPoint &start, const SkPoint& stop)
{
    SkVector parallel;
    SkVector scaledNormal;
    normal.scale (radius, &scaledNormal);
    scaledNormal.rotateCW(&parallel);

    path->moveTo(start.fX, start.fY);
    path->lineTo(pivot.fX + scaledNormal.fX + parallel.fX, pivot.fY + scaledNormal.fY + parallel.fY);
    path->lineTo(pivot.fX - scaledNormal.fX + parallel.fX, pivot.fY - scaledNormal.fY + parallel.fY);
    path->lineTo(stop.fX, stop.fY);
    path->close();
}

/////////////////////////////////////////////////////////////////////////////

enum AngleType {
    kNearly180_AngleType,
    kSharp_AngleType,
    kShallow_AngleType,
    kNearlyLine_AngleType
};

static AngleType Dot2AngleType(SkScalar dot)
{
// need more precise fixed normalization
//  SkASSERT(SkScalarAbs(dot) <= SK_Scalar1 + SK_ScalarNearlyZero);

    if (dot >= 0)   // shallow or line
        return SkScalarNearlyZero(SK_Scalar1 - dot) ? kNearlyLine_AngleType : kShallow_AngleType;
    else            // sharp or 180
        return SkScalarNearlyZero(SK_Scalar1 + dot) ? kNearly180_AngleType : kSharp_AngleType;
}

static void BevelJoiner(SkPath* path, const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar invMiterLimit,
                        const SkPoint& start, const SkPoint& stop)
{
    SkVector    after;
    SkVector    before = beforeUnitNormal;
    afterUnitNormal.scale(radius, &after);
    bool swap = !is_clockwise(before, after);

    path->close();
    path->moveTo(pivot.fX, pivot.fY);
    if (swap) {
        path->lineTo(stop.fX, stop.fY);
        path->lineTo(pivot.fX - after.fX, pivot.fY - after.fY);
    }
    else {
        path->lineTo(start.fX, start.fY);
        path->lineTo(pivot.fX + after.fX, pivot.fY + after.fY);
    }
    path->close();
}

static void RoundJoiner(SkPath* path, const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar invMiterLimit,
                        const SkPoint& start, const SkPoint& stop)
{
    SkScalar    dotProd = SkPoint::DotProduct(beforeUnitNormal, afterUnitNormal);
    AngleType   angleType = Dot2AngleType(dotProd);

    path->close();

    if (angleType == kNearlyLine_AngleType)
        return;

    SkVector            before = beforeUnitNormal;
    SkVector            after = afterUnitNormal;
    SkRotationDirection dir = kCW_SkRotationDirection;

    SkPoint     pts[kSkBuildQuadArcStorage];
    SkMatrix    matrix;
    matrix.setScale(radius, radius);
    matrix.postTranslate(pivot.fX, pivot.fY);
    bool swap = !is_clockwise(before, after);
    int count;

    if (swap) {
        dir = kCCW_SkRotationDirection;
        count = SkBuildQuadArc(after, before, dir, &matrix, pts);
    }
    else
        count = SkBuildQuadArc(before, after, dir, &matrix, pts);

    SkASSERT((count & 1) == 1);

    if (count > 1)
    {
        path->moveTo(pivot.fX, pivot.fY);
        if (swap)
            path->lineTo(stop.fX, stop.fY);
        else
            path->lineTo(start.fX, start.fY);
        for (int i = 1; i < count; i += 2)
            path->quadTo(pts[i].fX, pts[i].fY, pts[i+1].fX, pts[i+1].fY);
        path->close();
    }
}

#define kOneOverSqrt2   (0.707106781f)

static void MiterJoiner(SkPath* path, const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar invMiterLimit,
                        const SkPoint &start, const SkPoint &stop)
{
    // negate the dot since we're using normals instead of tangents
    SkScalar    dotProd = SkPoint::DotProduct(beforeUnitNormal, afterUnitNormal);
    AngleType   angleType = Dot2AngleType(dotProd);
    SkVector    before = beforeUnitNormal;
    SkVector    after = afterUnitNormal;
    SkVector    mid;
    SkScalar    sinHalfAngle;
    bool        ccw;

    path->close();

    if (angleType == kNearlyLine_AngleType)
        return;

    bool swap = !is_clockwise(before, after);

    path->moveTo(pivot.fX, pivot.fY);
    if (swap)
        path->lineTo(stop.fX, stop.fY);
    else
        path->lineTo(start.fX, start.fY);

    if (angleType == kNearly180_AngleType)
        goto DO_BLUNT;

    /*  Before we enter the world of square-roots and divides,
        check if we're trying to join an upright right angle
        (common case for stroking rectangles). If so, special case
        that (for speed an accuracy).
        Note: we only need to check one normal if dot==0
    */
    if (0 == dotProd && invMiterLimit <= kOneOverSqrt2) {
        mid.set(SkScalarMul(before.fX + after.fX, radius),
                SkScalarMul(before.fY + after.fY, radius));
        goto DO_MITER;
    }

    /*  midLength = radius / sinHalfAngle
        if (midLength > miterLimit * radius) abort
        if (radius / sinHalf > miterLimit * radius) abort
        if (1 / sinHalf > miterLimit) abort
        if (1 / miterLimit > sinHalf) abort
        My dotProd is opposite sign, since it is built from normals and not tangents
        hence 1 + dot instead of 1 - dot in the formula
    */
    sinHalfAngle = SkScalarSqrt(SkScalarHalf(SK_Scalar1 + dotProd));
    if (sinHalfAngle < invMiterLimit)
        goto DO_BLUNT;

    // choose the most accurate way to form the initial mid-vector
    if (angleType == kSharp_AngleType)
        mid.set(after.fY - before.fY, before.fX - after.fX);
    else
        mid.set(before.fX + after.fX, before.fY + after.fY);

    mid.setLength(SkScalarDiv(radius, sinHalfAngle));
DO_MITER:
   path->lineTo(pivot.fX + mid.fX, pivot.fY + mid.fY);

DO_BLUNT:
    if (swap)
        path->lineTo(pivot.fX - radius * after.fX, pivot.fY - radius * after.fY);
    else
        path->lineTo(pivot.fX + radius * after.fX, pivot.fY + radius * after.fY);

    path->close();
}

/////////////////////////////////////////////////////////////////////////////

SkShapeStrokerPriv::CapProc SkShapeStrokerPriv::CapFactory(SkPaint::Cap cap)
{
    static const SkShapeStrokerPriv::CapProc gCappers[] = {
        ButtCapper, RoundCapper, SquareCapper
    };

    SkASSERT((unsigned)cap < SkPaint::kCapCount);
    return gCappers[cap];
}

SkShapeStrokerPriv::JoinProc SkShapeStrokerPriv::JoinFactory(SkPaint::Join join)
{
    static const SkShapeStrokerPriv::JoinProc gJoiners[] = {
        MiterJoiner, RoundJoiner, BevelJoiner
    };

    SkASSERT((unsigned)join < SkPaint::kJoinCount);
    return gJoiners[join];
}
