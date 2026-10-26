// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

/// Exercises synchronous unregistration through the real selection registrar.
void main() {
  for (final event in <SelectionEvent>[
    const ClearSelectionEvent(),
    const SelectAllSelectionEvent(),
  ]) {
    for (final removesSelf in <bool>[true, false]) {
      testWidgets(
        '${event.type.name} handles a child unregistering ${removesSelf ? 'itself' : 'a later child'}',
        (WidgetTester tester) async {
          final delegate = _SelectionDelegate();
          addTearDown(() async {
            await tester.pumpWidget(const SizedBox.shrink());
            delegate.dispose();
          });
          final keys = List<GlobalKey>.generate(3, (int index) => GlobalKey());
          await tester.pumpWidget(
            Directionality(
              textDirection: TextDirection.ltr,
              child: SelectionContainer(
                delegate: delegate,
                child: Column(
                  children: <Widget>[
                    for (final GlobalKey key in keys)
                      SizedBox(height: 40.0, child: _SelectionSpy(key: key)),
                  ],
                ),
              ),
            ),
          );
          await tester.pump();
          final children = <_RenderSelectionSpy>[
            for (final GlobalKey key in keys)
              tester.renderObject<_RenderSelectionSpy>(find.byKey(key)),
          ];
          for (final child in children) {
            child.events.clear();
          }
          final _RenderSelectionSpy removed = children[removesSelf ? 0 : 1];
          children.first.onSelectionEvent = removed.hideContent;

          delegate.dispatchSelectionEvent(event);

          expect(delegate.selectables, isNot(contains(removed)));
          expect(children.first.events, <SelectionEvent>[event]);
          expect(children[1].events, removesSelf ? <SelectionEvent>[event] : isEmpty);
          expect(children.last.events, <SelectionEvent>[event]);
          if (event is SelectAllSelectionEvent) {
            expect(delegate.selectionStartIndex, 0);
            expect(delegate.selectionEndIndex, 1);
          } else {
            expect(delegate.selectionStartIndex, -1);
            expect(delegate.selectionEndIndex, -1);
          }
          expect(tester.takeException(), isNull);
        },
      );
    }
  }

  testWidgets('selectAll through a scrollable tolerates synchronous unregistration', (
    WidgetTester tester,
  ) async {
    final regionKey = GlobalKey<SelectableRegionState>();
    final GlobalKey childKey = GlobalKey();
    await tester.pumpWidget(
      MaterialApp(
        home: SelectableRegion(
          key: regionKey,
          selectionControls: materialTextSelectionControls,
          child: SingleChildScrollView(
            child: SizedBox(height: 80.0, child: _SelectionSpy(key: childKey)),
          ),
        ),
      ),
    );
    await tester.pump();
    final _RenderSelectionSpy child = tester.renderObject<_RenderSelectionSpy>(
      find.byKey(childKey),
    );
    child.events.clear();
    child.onSelectionEvent = child.hideContent;

    regionKey.currentState!.selectAll();
    await tester.pump();

    expect(child.events.whereType<ClearSelectionEvent>(), hasLength(1));
    expect(child.value.hasContent, isFalse);
    expect(tester.takeException(), isNull);
  });
}

/// Exposes the resulting indices without bypassing selection event dispatch.
class _SelectionDelegate extends StaticSelectionContainerDelegate {
  /// Reports the current start after synchronous removals have been applied.
  int get selectionStartIndex => currentSelectionStartIndex;

  /// Reports the current end after synchronous removals have been applied.
  int get selectionEndIndex => currentSelectionEndIndex;
}

/// Creates a selectable whose content can disappear during event dispatch.
class _SelectionSpy extends LeafRenderObjectWidget {
  /// Preserves render object identity while tests access the registered child.
  const _SelectionSpy({super.key});

  /// Registers the render object with its enclosing selection container.
  @override
  RenderObject createRenderObject(BuildContext context) {
    return _RenderSelectionSpy(SelectionContainer.maybeOf(context));
  }
}

/// Records events and models a child synchronously losing selectable content.
class _RenderSelectionSpy extends RenderProxyBox with Selectable, SelectionRegistrant {
  /// Registers with the real registrar used by the selection container.
  _RenderSelectionSpy(SelectionRegistrar? registrar) {
    this.registrar = registrar;
  }

  final Set<VoidCallback> _listeners = <VoidCallback>{};
  final List<SelectionEvent> events = <SelectionEvent>[];
  VoidCallback? onSelectionEvent;
  bool _hasContent = true;
  bool _isSelected = false;

  /// Supplies bounds used to order children within the selection container.
  @override
  List<Rect> get boundingBoxes => <Rect>[paintBounds];

  /// Fills the finite bounds supplied by the test widget.
  @override
  Size computeDryLayout(BoxConstraints constraints) => constraints.biggest;

  /// Keeps the actual size consistent with intrinsic layout checks.
  @override
  void performLayout() => size = computeDryLayout(constraints);

  /// Records listeners maintained by the real selection registrar.
  @override
  void addListener(VoidCallback listener) => _listeners.add(listener);

  /// Removes listeners during synchronous unregistration.
  @override
  void removeListener(VoidCallback listener) => _listeners.remove(listener);

  /// Publishes content disappearance so SelectionRegistrant unregisters it.
  void hideContent() {
    _hasContent = false;
    for (final listener in List<VoidCallback>.of(_listeners)) {
      listener();
    }
  }

  /// Records each event before triggering the configured content change.
  @override
  SelectionResult dispatchSelectionEvent(SelectionEvent event) {
    events.add(event);
    _isSelected = event is SelectAllSelectionEvent;
    onSelectionEvent?.call();
    return SelectionResult.end;
  }

  /// Returns the stable payload exposed by this test selectable.
  @override
  SelectedContent? getSelectedContent() =>
      _hasContent && _isSelected ? const SelectedContent(plainText: 'content') : null;

  /// Reports the selected range when the test payload has been selected.
  @override
  SelectedContentRange? getSelection() =>
      _hasContent && _isSelected ? const SelectedContentRange(startOffset: 0, endOffset: 7) : null;

  /// Reports the length of the stable test payload.
  @override
  int get contentLength => 7;

  /// Makes registration track whether the child still has selectable content.
  @override
  SelectionGeometry get value => _hasContent && _isSelected
      ? const SelectionGeometry(
          hasContent: true,
          status: SelectionStatus.uncollapsed,
          startSelectionPoint: SelectionPoint(
            localPosition: Offset.zero,
            lineHeight: 10.0,
            handleType: TextSelectionHandleType.left,
          ),
          endSelectionPoint: SelectionPoint(
            localPosition: Offset(40.0, 0.0),
            lineHeight: 10.0,
            handleType: TextSelectionHandleType.right,
          ),
        )
      : SelectionGeometry(hasContent: _hasContent, status: SelectionStatus.none);

  /// Accepts handle updates without rendering selection handles.
  @override
  void pushHandleLayers(LayerLink? startHandle, LayerLink? endHandle) {}
}
