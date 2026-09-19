package com.example.tagmeow;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

// 自适应换行容器：一行放不下就自动换到下一行

// 用法（子 View 用 wrap_content）：
//   FlowLayout flow = new FlowLayout(this, dp(6), dp(6));
//   flow.addView(chip);
//   container.addView(flow);

public final class FlowLayout extends ViewGroup {

    private final int horizontal_gap;

    private final int vertical_gap;

    public FlowLayout(Context context, int horizontal_gap_px, int vertical_gap_px) {
        super(context);

        this.horizontal_gap = horizontal_gap_px;
        this.vertical_gap = vertical_gap_px;
    }

    @Override
    protected void onMeasure(int width_measure_spec, int height_measure_spec) {
        int width_limit = MeasureSpec.getSize(width_measure_spec) - getPaddingLeft() - getPaddingRight();

        int line_width = 0;
        int line_height = 0;
        int total_height = 0;
        int widest_line = 0;

        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);

            if (child.getVisibility() == GONE) {
                continue;
            }

            measureChild(child, width_measure_spec, height_measure_spec);

            int child_width = child.getMeasuredWidth();
            int child_height = child.getMeasuredHeight();

            // 这一行放不下了：结算上一行
            if (line_width > 0 && line_width + horizontal_gap + child_width > width_limit) {
                total_height += line_height + vertical_gap;
                widest_line = Math.max(widest_line, line_width);
                line_width = 0;
                line_height = 0;
            }

            line_width += (line_width == 0 ? 0 : horizontal_gap) + child_width;
            line_height = Math.max(line_height, child_height);
        }

        total_height += line_height;
        widest_line = Math.max(widest_line, line_width);

        setMeasuredDimension(
                resolveSize(widest_line + getPaddingLeft() + getPaddingRight(), width_measure_spec),
                resolveSize(total_height + getPaddingTop() + getPaddingBottom(), height_measure_spec));
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int width_limit = right - left - getPaddingLeft() - getPaddingRight();

        int x = getPaddingLeft();
        int y = getPaddingTop();
        int line_height = 0;

        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);

            if (child.getVisibility() == GONE) {
                continue;
            }

            int child_width = child.getMeasuredWidth();
            int child_height = child.getMeasuredHeight();

            if (x > getPaddingLeft() && x + child_width > getPaddingLeft() + width_limit) {
                x = getPaddingLeft();
                y += line_height + vertical_gap;
                line_height = 0;
            }

            child.layout(x, y, x + child_width, y + child_height);

            x += child_width + horizontal_gap;
            line_height = Math.max(line_height, child_height);
        }
    }
}