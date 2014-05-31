
/*
 * Copyright 2014 Igalia S.L.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SkBenchmark.h"
#include "SkCanvas.h"
#include "SkCommandLineFlags.h"
#include "SkPaint.h"
#include "SkRandom.h"
#include "SkString.h"

class AAOvalBench : public SkBenchmark {
public:
    enum {
        W = 640,
        H = 480,
        N = 1000,
    };
    struct {
        double x;
        double y;
        double radius;
        double rotation;
        double scaleX;
        double scaleY;
    } fOvals[N];
    SkColor fColors[N];

    AAOvalBench() {}

protected:
    virtual const char* onGetName() { return "aaovals"; }

    virtual void randomColors(SkRandom rand) {
        for (int i = 0; i < N; i++) {
            int r = rand.nextU() * 255;
            int g = rand.nextU() * 255;
            int b = rand.nextU() * 255;
            int a = rand.nextU() * 255;
            SkColor color(a << 24 | r << 16 | g << 8 | b);
            fColors[i] = color;
        }
    }

    virtual void onPreDraw() {
        SkRandom rand;

        this->randomColors(rand);

        for (int i = 0; i < N; i++) {
            fOvals[i].x = rand.nextU() * W;
            fOvals[i].y = rand.nextU() * H;
            fOvals[i].radius = rand.nextU() * H;
            fOvals[i].rotation = rand.nextU() * 180;
            fOvals[i].scaleX = rand.nextU() * 2;
            fOvals[i].scaleY = rand.nextU() * 2;
        }
    }

    virtual void onDraw(const int loops, SkCanvas* canvas) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kFill_Style);

        for (int i = 0; i < loops; i++) {
            paint.setColor(fColors[i % N]);

            canvas->save();
            canvas->translate(fOvals[i % N].x, fOvals[i % N].x);
            canvas->rotate(fOvals[i % N].rotation);
            canvas->scale(fOvals[i % N].scaleX, fOvals[i % N].scaleY);

            canvas->drawCircle(0, 0, fOvals[i % N].radius, paint);
            canvas->restore();
        }
    }
private:
    typedef SkBenchmark INHERITED;
};

class AARRectBench : public AAOvalBench {
public:
    struct {
        double x;
        double y;
        SkRRect rrect;
        double rotation;
        double scaleX;
        double scaleY;
    } fOvals[N];

    AARRectBench() {}

protected:
    virtual const char* onGetName() { return "aarrects"; }

    virtual void onPreDraw() {
        SkRandom rand;

        this->randomColors(rand);

        for (int i = 0; i < N; i++) {
            fOvals[i].x = rand.nextU() * W;
            fOvals[i].y = rand.nextU() * H;

            double width = rand.nextU() * H;
            double height = rand.nextU() * H;
            double topR = rand.nextU() * H;
            double leftR = rand.nextU() * H;
            SkRect rect = SkRect::MakeXYWH(-width/2, height/2, width, height);

            fOvals[i].rrect.setRectXY(rect, leftR, topR);

            fOvals[i].rotation = rand.nextU() * 180;
            fOvals[i].scaleX = rand.nextU() * 2;
            fOvals[i].scaleY = rand.nextU() * 2;
        }
    }

    virtual void onDraw(const int loops, SkCanvas* canvas) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kFill_Style);

        for (int i = 0; i < loops; i++) {
            paint.setColor(fColors[i % N]);

            canvas->save();
            canvas->translate(fOvals[i % N].x, fOvals[i % N].x);
            canvas->rotate(fOvals[i % N].rotation);
            canvas->scale(fOvals[i % N].scaleX, fOvals[i % N].scaleY);

            canvas->drawRRect(fOvals[i % N].rrect, paint);
            canvas->restore();
        }
    }
};

DEF_BENCH( return SkNEW_ARGS(AAOvalBench, ()); )
DEF_BENCH( return SkNEW_ARGS(AARRectBench, ()); )
