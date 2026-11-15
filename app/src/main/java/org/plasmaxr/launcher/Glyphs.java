package org.plasmaxr.launcher;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;

/**
 * Tiny vector glyph set for the shell UI, drawn as white-on-transparent masks so the GPU can tint
 * them. All geometry is authored in a unit square and scaled to the requested size.
 */
final class Glyphs {
    private Glyphs() {}

    static void draw(String name, Canvas c, int size) {
        float s = size;
        Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        fill.setColor(0xFFFFFFFF);
        fill.setStyle(Paint.Style.FILL);
        Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        stroke.setColor(0xFFFFFFFF);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        stroke.setStrokeJoin(Paint.Join.ROUND);
        stroke.setStrokeWidth(s * 0.085f);

        switch (name) {
            case "search":
                c.drawCircle(s * 0.43f, s * 0.43f, s * 0.27f, stroke);
                c.drawLine(s * 0.63f, s * 0.63f, s * 0.85f, s * 0.85f, stroke);
                break;
            case "star": {
                Path p = new Path();
                for (int i = 0; i < 10; i++) {
                    double a = -Math.PI / 2 + i * Math.PI / 5;
                    float r = (i % 2 == 0 ? 0.42f : 0.18f) * s;
                    float x = s * 0.5f + (float) Math.cos(a) * r;
                    float y = s * 0.54f + (float) Math.sin(a) * r;
                    if (i == 0) p.moveTo(x, y); else p.lineTo(x, y);
                }
                p.close();
                c.drawPath(p, fill);
                break;
            }
            case "apps": {
                float cell = s * 0.22f, gap = s * 0.11f, start = (s - (cell * 3 + gap * 2)) / 2;
                for (int r = 0; r < 3; r++) {
                    for (int q = 0; q < 3; q++) {
                        float x = start + q * (cell + gap), y = start + r * (cell + gap);
                        c.drawRoundRect(new RectF(x, y, x + cell, y + cell), cell * 0.28f, cell * 0.28f, fill);
                    }
                }
                break;
            }
            case "power": {
                RectF oval = new RectF(s * 0.2f, s * 0.22f, s * 0.8f, s * 0.82f);
                c.drawArc(oval, -60, 300, false, stroke);
                c.drawLine(s * 0.5f, s * 0.12f, s * 0.5f, s * 0.5f, stroke);
                break;
            }
            case "wifi": {
                float cx = s * 0.5f, cy = s * 0.8f;
                for (int i = 1; i <= 3; i++) {
                    float r = s * 0.2f * i;
                    c.drawArc(new RectF(cx - r, cy - r, cx + r, cy + r), -135, 90, false, stroke);
                }
                c.drawCircle(cx, cy, s * 0.06f, fill);
                break;
            }
            case "wifi_off": {
                float cx = s * 0.5f, cy = s * 0.8f;
                for (int i = 1; i <= 3; i++) {
                    float r = s * 0.2f * i;
                    c.drawArc(new RectF(cx - r, cy - r, cx + r, cy + r), -135, 90, false, stroke);
                }
                c.drawCircle(cx, cy, s * 0.06f, fill);
                c.drawLine(s * 0.12f, s * 0.12f, s * 0.88f, s * 0.88f, stroke);
                break;
            }
            case "settings": {
                Path p = new Path();
                float cx = s * 0.5f, cy = s * 0.5f;
                int teeth = 8;
                for (int i = 0; i < teeth * 2; i++) {
                    double a = i * Math.PI / teeth - Math.PI / (teeth * 2);
                    float r = (i % 2 == 0 ? 0.42f : 0.31f) * s;
                    double a2 = a + Math.PI / (teeth * 2) * 0.0;
                    float x = cx + (float) Math.cos(a2) * r, y = cy + (float) Math.sin(a2) * r;
                    if (i == 0) p.moveTo(x, y); else p.lineTo(x, y);
                }
                p.close();
                Path hole = new Path();
                hole.addCircle(cx, cy, s * 0.14f, Path.Direction.CCW);
                p.op(hole, Path.Op.DIFFERENCE);
                c.drawPath(p, fill);
                break;
            }
            case "hand": {
                float w = s * 0.11f;
                float[] xs = {0.28f, 0.42f, 0.56f, 0.70f};
                float[] tops = {0.2f, 0.1f, 0.14f, 0.26f};
                for (int i = 0; i < 4; i++) {
                    c.drawRoundRect(new RectF(s * xs[i] - w / 2, s * tops[i], s * xs[i] + w / 2, s * 0.62f),
                            w / 2, w / 2, fill);
                }
                c.drawRoundRect(new RectF(s * 0.2f, s * 0.46f, s * 0.78f, s * 0.9f), s * 0.2f, s * 0.2f, fill);
                Path th = new Path();
                th.moveTo(s * 0.22f, s * 0.62f);
                th.lineTo(s * 0.08f, s * 0.46f);
                Paint tp = new Paint(stroke);
                tp.setStrokeWidth(w);
                c.drawPath(th, tp);
                break;
            }
            case "eye": {
                Path p = new Path();
                p.moveTo(s * 0.06f, s * 0.5f);
                p.quadTo(s * 0.5f, s * 0.08f, s * 0.94f, s * 0.5f);
                p.quadTo(s * 0.5f, s * 0.92f, s * 0.06f, s * 0.5f);
                c.drawPath(p, stroke);
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.13f, fill);
                break;
            }
            case "recenter": {
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.26f, stroke);
                c.drawLine(s * 0.5f, s * 0.08f, s * 0.5f, s * 0.24f, stroke);
                c.drawLine(s * 0.5f, s * 0.76f, s * 0.5f, s * 0.92f, stroke);
                c.drawLine(s * 0.08f, s * 0.5f, s * 0.24f, s * 0.5f, stroke);
                c.drawLine(s * 0.76f, s * 0.5f, s * 0.92f, s * 0.5f, stroke);
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.05f, fill);
                break;
            }
            case "close":
                c.drawLine(s * 0.24f, s * 0.24f, s * 0.76f, s * 0.76f, stroke);
                c.drawLine(s * 0.76f, s * 0.24f, s * 0.24f, s * 0.76f, stroke);
                break;
            case "chevron_right": {
                Path p = new Path();
                p.moveTo(s * 0.36f, s * 0.2f);
                p.lineTo(s * 0.66f, s * 0.5f);
                p.lineTo(s * 0.36f, s * 0.8f);
                c.drawPath(p, stroke);
                break;
            }
            case "chevron_left": {
                Path p = new Path();
                p.moveTo(s * 0.64f, s * 0.2f);
                p.lineTo(s * 0.34f, s * 0.5f);
                p.lineTo(s * 0.64f, s * 0.8f);
                c.drawPath(p, stroke);
                break;
            }
            case "chevron_up": {
                Path p = new Path();
                p.moveTo(s * 0.2f, s * 0.64f);
                p.lineTo(s * 0.5f, s * 0.34f);
                p.lineTo(s * 0.8f, s * 0.64f);
                c.drawPath(p, stroke);
                break;
            }
            case "check": {
                Path p = new Path();
                p.moveTo(s * 0.2f, s * 0.52f);
                p.lineTo(s * 0.42f, s * 0.74f);
                p.lineTo(s * 0.8f, s * 0.28f);
                c.drawPath(p, stroke);
                break;
            }
            case "folder": {
                Path p = new Path();
                p.moveTo(s * 0.08f, s * 0.24f);
                p.lineTo(s * 0.38f, s * 0.24f);
                p.lineTo(s * 0.46f, s * 0.34f);
                p.lineTo(s * 0.92f, s * 0.34f);
                p.lineTo(s * 0.92f, s * 0.8f);
                p.lineTo(s * 0.08f, s * 0.8f);
                p.close();
                c.drawPath(p, fill);
                break;
            }
            case "user": {
                c.drawCircle(s * 0.5f, s * 0.34f, s * 0.19f, fill);
                c.drawArc(new RectF(s * 0.16f, s * 0.58f, s * 0.84f, s * 1.3f), 180, 180, true, fill);
                break;
            }
            case "restart": {
                RectF oval = new RectF(s * 0.2f, s * 0.2f, s * 0.8f, s * 0.8f);
                c.drawArc(oval, -40, 290, false, stroke);
                Path ar = new Path();
                ar.moveTo(s * 0.64f, s * 0.12f);
                ar.lineTo(s * 0.82f, s * 0.24f);
                ar.lineTo(s * 0.62f, s * 0.34f);
                c.drawPath(ar, stroke);
                break;
            }
            case "exit": {
                Path p = new Path();
                p.moveTo(s * 0.42f, s * 0.14f);
                p.lineTo(s * 0.16f, s * 0.14f);
                p.lineTo(s * 0.16f, s * 0.86f);
                p.lineTo(s * 0.42f, s * 0.86f);
                c.drawPath(p, stroke);
                c.drawLine(s * 0.36f, s * 0.5f, s * 0.86f, s * 0.5f, stroke);
                Path h = new Path();
                h.moveTo(s * 0.68f, s * 0.32f);
                h.lineTo(s * 0.86f, s * 0.5f);
                h.lineTo(s * 0.68f, s * 0.68f);
                c.drawPath(h, stroke);
                break;
            }
            case "moon": {
                Path p = new Path();
                p.addCircle(s * 0.5f, s * 0.5f, s * 0.36f, Path.Direction.CW);
                Path cut = new Path();
                cut.addCircle(s * 0.65f, s * 0.4f, s * 0.3f, Path.Direction.CW);
                p.op(cut, Path.Op.DIFFERENCE);
                c.drawPath(p, fill);
                break;
            }
            case "plasma": {
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.44f, stroke);
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.24f, fill);
                break;
            }
            case "minimize":
                c.drawLine(s * 0.22f, s * 0.68f, s * 0.78f, s * 0.68f, stroke);
                break;
            case "maximize":
                c.drawRoundRect(new RectF(s * 0.2f, s * 0.2f, s * 0.8f, s * 0.8f), s * 0.08f, s * 0.08f, stroke);
                break;
            case "restore": {
                c.drawRoundRect(new RectF(s * 0.18f, s * 0.34f, s * 0.66f, s * 0.82f), s * 0.07f, s * 0.07f, stroke);
                Path p = new Path();
                p.moveTo(s * 0.34f, s * 0.2f);
                p.lineTo(s * 0.82f, s * 0.2f);
                p.lineTo(s * 0.82f, s * 0.66f);
                c.drawPath(p, stroke);
                break;
            }
            case "float": {
                // A window with an arrow leaving it: pop out of the desktop into space.
                c.drawRoundRect(new RectF(s * 0.16f, s * 0.34f, s * 0.66f, s * 0.84f), s * 0.07f, s * 0.07f, stroke);
                c.drawLine(s * 0.46f, s * 0.54f, s * 0.84f, s * 0.16f, stroke);
                Path h = new Path();
                h.moveTo(s * 0.58f, s * 0.16f);
                h.lineTo(s * 0.84f, s * 0.16f);
                h.lineTo(s * 0.84f, s * 0.42f);
                c.drawPath(h, stroke);
                break;
            }
            case "dock": {
                c.drawRoundRect(new RectF(s * 0.18f, s * 0.18f, s * 0.82f, s * 0.82f), s * 0.07f, s * 0.07f, stroke);
                c.drawRect(new RectF(s * 0.26f, s * 0.58f, s * 0.74f, s * 0.74f), fill);
                break;
            }
            case "terminal": {
                // A window with a prompt: ">_"
                c.drawRoundRect(new RectF(s * 0.1f, s * 0.18f, s * 0.9f, s * 0.82f), s * 0.1f, s * 0.1f, stroke);
                Path p = new Path();
                p.moveTo(s * 0.26f, s * 0.38f);
                p.lineTo(s * 0.42f, s * 0.5f);
                p.lineTo(s * 0.26f, s * 0.62f);
                c.drawPath(p, stroke);
                c.drawLine(s * 0.52f, s * 0.64f, s * 0.72f, s * 0.64f, stroke);
                break;
            }
            case "dot":
                c.drawCircle(s * 0.5f, s * 0.5f, s * 0.2f, fill);
                break;
            default:
                c.drawRoundRect(new RectF(s * 0.15f, s * 0.15f, s * 0.85f, s * 0.85f), s * 0.15f, s * 0.15f, stroke);
                break;
        }
    }
}
