
/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkShapeStrokerPriv_DEFINED
#define SkShapeStrokerPriv_DEFINED

#include "SkShapeStroke.h"

#define CWX(x, y)   (-y)
#define CWY(x, y)   (x)
#define CCWX(x, y)  (y)
#define CCWY(x, y)  (-x)

#define CUBIC_ARC_FACTOR    ((SK_ScalarSqrt2 - SK_Scalar1) * 4 / 3)

class SkShapeStrokerPriv {
public:
    typedef void (*CapProc)(SkPath* path,
                            const SkPoint& pivot,
                            const SkVector& normal,
                            const SkPoint& start,
                            const SkPoint& stop);

    typedef void (*JoinProc)(SkPath* path,
                             const SkVector& beforeUnitNormal,
                             const SkPoint& pivot,
                             const SkVector& afterUnitNormal,
                             SkScalar radius, SkScalar invMiterLimit,
                             const SkPoint &start);

    static CapProc  CapFactory(SkPaint::Cap);
    static JoinProc JoinFactory(SkPaint::Join);
};

#endif
