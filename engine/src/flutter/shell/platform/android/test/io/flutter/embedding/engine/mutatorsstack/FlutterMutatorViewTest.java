// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.embedding.engine.mutatorsstack;

import static android.view.View.OnFocusChangeListener;
import static junit.framework.TestCase.*;
import static org.mockito.Mockito.*;

import android.content.Context;
import android.graphics.Matrix;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.accessibility.AccessibilityEvent;
import android.widget.FrameLayout;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import io.flutter.embedding.android.AndroidTouchProcessor;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;

@RunWith(AndroidJUnit4.class)
public class FlutterMutatorViewTest {
  private final Context ctx = ApplicationProvider.getApplicationContext();

  /** Scrolling preserves native layout while updating bounds used by touch dispatch. */
  @Test
  public void scrollingDoesNotRequestNativeLayout() {
    final FlutterMutatorView view = new FlutterMutatorView(ctx);
    final FlutterMutatorsStack stack = new FlutterMutatorsStack();
    view.readyToDisplay(stack, 10, 20, 100, 200);
    view.measure(
        View.MeasureSpec.makeMeasureSpec(100, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(200, View.MeasureSpec.EXACTLY));
    view.layout(10, 20, 110, 220);
    final FrameLayout.LayoutParams original = (FrameLayout.LayoutParams) view.getLayoutParams();
    assertFalse(view.isLayoutRequested());

    view.readyToDisplay(stack, 30, -40, 100, 200);

    assertSame(original, view.getLayoutParams());
    assertFalse(view.isLayoutRequested());
    assertEquals(30, view.getLeft());
    assertEquals(-40, view.getTop());
    assertEquals(130, view.getRight());
    assertEquals(160, view.getBottom());
    assertEquals(30, original.leftMargin);
    assertEquals(-40, original.topMargin);

    // A later ancestor layout must retain the position committed during scrolling.
    final FrameLayout parent = new FrameLayout(ctx);
    parent.addView(view);
    parent.measure(
        View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.EXACTLY));
    parent.layout(0, 0, 500, 500);
    assertEquals(30, view.getLeft());
    assertEquals(-40, view.getTop());

    view.readyToDisplay(stack, 30, -40, 150, 250);
    assertTrue(view.isLayoutRequested());
    assertEquals(150, view.getLayoutParams().width);
    assertEquals(250, view.getLayoutParams().height);
  }

  @Test
  public void canDragViews() {
    final AndroidTouchProcessor touchProcessor = mock(AndroidTouchProcessor.class);
    final FlutterMutatorView view = new FlutterMutatorView(ctx, 1.0f, touchProcessor);
    final FlutterMutatorsStack mutatorStack = mock(FlutterMutatorsStack.class);

    assertTrue(view.onInterceptTouchEvent(mock(MotionEvent.class)));

    {
      view.readyToDisplay(mutatorStack, /*left=*/ 1, /*top=*/ 2, /*width=*/ 0, /*height=*/ 0);
      view.layout(1, 2, 1, 2);
      view.onTouchEvent(MotionEvent.obtain(0, 0, MotionEvent.ACTION_DOWN, 0.0f, 0.0f, 0));
      final ArgumentCaptor<Matrix> matrixCaptor = ArgumentCaptor.forClass(Matrix.class);
      verify(touchProcessor).onTouchEvent(any(), matrixCaptor.capture());

      final Matrix screenMatrix = new Matrix();
      screenMatrix.postTranslate(1, 2);
      assertEquals(matrixCaptor.getValue(), screenMatrix);
    }

    reset(touchProcessor);

    {
      view.readyToDisplay(mutatorStack, /*left=*/ 3, /*top=*/ 4, /*width=*/ 0, /*height=*/ 0);
      view.layout(3, 4, 3, 4);
      view.onTouchEvent(MotionEvent.obtain(0, 0, MotionEvent.ACTION_MOVE, 0.0f, 0.0f, 0));
      final ArgumentCaptor<Matrix> matrixCaptor = ArgumentCaptor.forClass(Matrix.class);
      verify(touchProcessor).onTouchEvent(any(), matrixCaptor.capture());

      final Matrix screenMatrix = new Matrix();
      screenMatrix.postTranslate(3, 4);
      assertEquals(matrixCaptor.getValue(), screenMatrix);
    }

    reset(touchProcessor);

    {
      view.readyToDisplay(mutatorStack, /*left=*/ 5, /*top=*/ 6, /*width=*/ 0, /*height=*/ 0);
      view.layout(5, 6, 5, 6);
      view.onTouchEvent(MotionEvent.obtain(0, 0, MotionEvent.ACTION_MOVE, 0.0f, 0.0f, 0));
      final ArgumentCaptor<Matrix> matrixCaptor = ArgumentCaptor.forClass(Matrix.class);
      verify(touchProcessor).onTouchEvent(any(), matrixCaptor.capture());

      final Matrix screenMatrix = new Matrix();
      screenMatrix.postTranslate(5, 6);
      assertEquals(matrixCaptor.getValue(), screenMatrix);
    }

    reset(touchProcessor);

    {
      view.readyToDisplay(mutatorStack, /*left=*/ 7, /*top=*/ 8, /*width=*/ 0, /*height=*/ 0);
      view.layout(7, 8, 7, 8);
      view.onTouchEvent(MotionEvent.obtain(0, 0, MotionEvent.ACTION_DOWN, 0.0f, 0.0f, 0));
      final ArgumentCaptor<Matrix> matrixCaptor = ArgumentCaptor.forClass(Matrix.class);
      verify(touchProcessor).onTouchEvent(any(), matrixCaptor.capture());

      final Matrix screenMatrix = new Matrix();
      screenMatrix.postTranslate(7, 8);
      assertEquals(matrixCaptor.getValue(), screenMatrix);
    }
  }

  @Test
  public void focusChangeListener_hasFocus() {
    final ViewTreeObserver viewTreeObserver = mock(ViewTreeObserver.class);
    when(viewTreeObserver.isAlive()).thenReturn(true);

    final FlutterMutatorView view =
        new FlutterMutatorView(ctx) {
          @Override
          public ViewTreeObserver getViewTreeObserver() {
            return viewTreeObserver;
          }

          @Override
          public boolean hasFocus() {
            return true;
          }
        };

    final OnFocusChangeListener focusListener = mock(OnFocusChangeListener.class);
    view.setOnDescendantFocusChangeListener(focusListener);

    final ArgumentCaptor<ViewTreeObserver.OnGlobalFocusChangeListener> focusListenerCaptor =
        ArgumentCaptor.forClass(ViewTreeObserver.OnGlobalFocusChangeListener.class);
    verify(viewTreeObserver).addOnGlobalFocusChangeListener(focusListenerCaptor.capture());

    focusListenerCaptor.getValue().onGlobalFocusChanged(null, null);
    verify(focusListener).onFocusChange(view, true);
  }

  @Test
  public void focusChangeListener_doesNotHaveFocus() {
    final ViewTreeObserver viewTreeObserver = mock(ViewTreeObserver.class);
    when(viewTreeObserver.isAlive()).thenReturn(true);

    final FlutterMutatorView view =
        new FlutterMutatorView(ctx) {
          @Override
          public ViewTreeObserver getViewTreeObserver() {
            return viewTreeObserver;
          }

          @Override
          public boolean hasFocus() {
            return false;
          }
        };

    final OnFocusChangeListener focusListener = mock(OnFocusChangeListener.class);
    view.setOnDescendantFocusChangeListener(focusListener);

    final ArgumentCaptor<ViewTreeObserver.OnGlobalFocusChangeListener> focusListenerCaptor =
        ArgumentCaptor.forClass(ViewTreeObserver.OnGlobalFocusChangeListener.class);
    verify(viewTreeObserver).addOnGlobalFocusChangeListener(focusListenerCaptor.capture());

    focusListenerCaptor.getValue().onGlobalFocusChanged(null, null);
    verify(focusListener).onFocusChange(view, false);
  }

  @Test
  public void focusChangeListener_viewTreeObserverIsAliveFalseDoesNotThrow() {
    final FlutterMutatorView view =
        new FlutterMutatorView(ctx) {
          @Override
          public ViewTreeObserver getViewTreeObserver() {
            final ViewTreeObserver viewTreeObserver = mock(ViewTreeObserver.class);
            when(viewTreeObserver.isAlive()).thenReturn(false);
            return viewTreeObserver;
          }
        };
    view.setOnDescendantFocusChangeListener(mock(OnFocusChangeListener.class));
  }

  @Test
  public void setOnDescendantFocusChangeListener_keepsSingleListener() {
    final ViewTreeObserver viewTreeObserver = mock(ViewTreeObserver.class);
    when(viewTreeObserver.isAlive()).thenReturn(true);

    final FlutterMutatorView view =
        new FlutterMutatorView(ctx) {
          @Override
          public ViewTreeObserver getViewTreeObserver() {
            return viewTreeObserver;
          }
        };

    assertNull(view.activeFocusListener);

    view.setOnDescendantFocusChangeListener(mock(OnFocusChangeListener.class));
    assertNotNull(view.activeFocusListener);

    final ViewTreeObserver.OnGlobalFocusChangeListener activeFocusListener =
        view.activeFocusListener;

    view.setOnDescendantFocusChangeListener(mock(OnFocusChangeListener.class));
    assertNotNull(view.activeFocusListener);

    verify(viewTreeObserver, times(1)).removeOnGlobalFocusChangeListener(activeFocusListener);
  }

  @Test
  public void unsetOnDescendantFocusChangeListener_removesActiveListener() {
    final ViewTreeObserver viewTreeObserver = mock(ViewTreeObserver.class);
    when(viewTreeObserver.isAlive()).thenReturn(true);

    final FlutterMutatorView view =
        new FlutterMutatorView(ctx) {
          @Override
          public ViewTreeObserver getViewTreeObserver() {
            return viewTreeObserver;
          }
        };

    assertNull(view.activeFocusListener);

    view.setOnDescendantFocusChangeListener(mock(OnFocusChangeListener.class));
    assertNotNull(view.activeFocusListener);

    final ViewTreeObserver.OnGlobalFocusChangeListener activeFocusListener =
        view.activeFocusListener;

    view.unsetOnDescendantFocusChangeListener();
    assertNull(view.activeFocusListener);

    view.unsetOnDescendantFocusChangeListener();
    verify(viewTreeObserver, times(1)).removeOnGlobalFocusChangeListener(activeFocusListener);
  }

  @Test
  @Config(
      shadows = {
        ShadowViewGroup.class,
      })
  public void ignoreAccessibilityEvents() {
    final FlutterMutatorView wrapperView = new FlutterMutatorView(ctx);

    final View embeddedView = mock(View.class);
    wrapperView.addView(embeddedView);

    when(embeddedView.getImportantForAccessibility())
        .thenReturn(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
    final boolean eventSent =
        wrapperView.requestSendAccessibilityEvent(embeddedView, mock(AccessibilityEvent.class));
    assertFalse(eventSent);
  }

  @Implements(ViewGroup.class)
  public static class ShadowViewGroup extends org.robolectric.shadows.ShadowViewGroup {
    @Implementation
    protected boolean requestSendAccessibilityEvent(View child, AccessibilityEvent event) {
      return true;
    }
  }

  @Implements(FrameLayout.class)
  public static class ShadowFrameLayout
      extends io.flutter.plugin.platform.PlatformViewWrapperTest.ShadowViewGroup {}
}
