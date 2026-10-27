// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('unlaid-out containers wait for layout without requesting frames', (
    WidgetTester tester,
  ) async {
    final delegate = _SelectionDelegate();
    SelectedContent? selected;
    final lower = OverlayEntry(
      maintainState: true,
      builder: (BuildContext context) => SelectionContainer(
        delegate: delegate,
        child: const Column(children: <Widget>[Text('first'), Text('second')]),
      ),
    );
    final upper = OverlayEntry(
      opaque: true,
      builder: (BuildContext context) => const Text('cover'),
    );
    var upperRemoved = false;
    addTearDown(() async {
      lower.remove();
      if (!upperRemoved) {
        upper.remove();
      }
      await tester.pumpWidget(const SizedBox.shrink());
      lower.dispose();
      upper.dispose();
      delegate.dispose();
    });

    await tester.pumpWidget(
      MaterialApp(
        home: SelectionArea(
          onSelectionChanged: (SelectedContent? content) {
            selected = content;
          },
          child: Overlay(initialEntries: <OverlayEntry>[lower, upper]),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(delegate.hasSize, isFalse);
    expect(delegate.selectables, isEmpty);
    expect(tester.binding.hasScheduledFrame, isFalse);

    // Waiting for layout must not start an endless chain of microtasks or frames.
    for (var frame = 0; frame < 3; frame += 1) {
      await tester.pump();
      expect(delegate.selectables, isEmpty);
      expect(tester.binding.hasScheduledFrame, isFalse);
    }

    upper.remove();
    upperRemoved = true;
    await tester.pumpAndSettle();
    expect(delegate.hasSize, isTrue);
    expect(delegate.selectables.length, 2);
    final SelectableRegionState region = tester.state(find.byType(SelectableRegion));
    region.selectAll(SelectionChangedCause.keyboard);
    await tester.pump();
    expect(selected?.plainText, 'first\nsecond');
    expect(tester.takeException(), isNull);
  });

  testWidgets('a hidden SelectionArea with WidgetSpans becomes selectable after layout', (
    WidgetTester tester,
  ) async {
    SelectedContent? selected;
    final lower = OverlayEntry(
      maintainState: true,
      builder: (BuildContext context) => SelectionArea(
        onSelectionChanged: (SelectedContent? content) {
          selected = content;
        },
        child: const Text.rich(
          TextSpan(
            children: <InlineSpan>[
              TextSpan(text: 'before '),
              WidgetSpan(child: Text('inside')),
              TextSpan(text: ' after'),
            ],
          ),
        ),
      ),
    );
    final upper = OverlayEntry(
      opaque: true,
      builder: (BuildContext context) => const Text('cover'),
    );
    var upperRemoved = false;
    addTearDown(() async {
      lower.remove();
      if (!upperRemoved) {
        upper.remove();
      }
      await tester.pumpWidget(const SizedBox.shrink());
      lower.dispose();
      upper.dispose();
    });
    await tester.pumpWidget(
      MaterialApp(home: Overlay(initialEntries: <OverlayEntry>[lower, upper])),
    );
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(tester.binding.hasScheduledFrame, isFalse);

    upper.remove();
    upperRemoved = true;
    await tester.pumpAndSettle();
    final SelectableRegionState region = tester.state(find.byType(SelectableRegion));
    region.selectAll(SelectionChangedCause.keyboard);
    await tester.pump();
    expect(selected?.plainText, 'before inside after');
    expect(tester.takeException(), isNull);
  });

  testWidgets('removing a pending selectable before layout does not restore it later', (
    WidgetTester tester,
  ) async {
    final delegate = _SelectionDelegate();
    late StateSetter rebuild;
    var showFirst = true;
    final lower = OverlayEntry(
      maintainState: true,
      builder: (BuildContext context) => SelectionContainer(
        delegate: delegate,
        child: StatefulBuilder(
          builder: (BuildContext context, StateSetter setState) {
            rebuild = setState;
            return Column(
              children: <Widget>[
                if (showFirst) const Text('removed', key: ValueKey<String>('removed')),
                const Text('remaining', key: ValueKey<String>('remaining')),
              ],
            );
          },
        ),
      ),
    );
    final upper = OverlayEntry(
      opaque: true,
      builder: (BuildContext context) => const Text('cover'),
    );
    var upperRemoved = false;
    addTearDown(() async {
      lower.remove();
      if (!upperRemoved) {
        upper.remove();
      }
      await tester.pumpWidget(const SizedBox.shrink());
      lower.dispose();
      upper.dispose();
      delegate.dispose();
    });
    await tester.pumpWidget(
      MaterialApp(
        home: SelectionArea(child: Overlay(initialEntries: <OverlayEntry>[lower, upper])),
      ),
    );
    await tester.pumpAndSettle();
    rebuild(() {
      showFirst = false;
    });
    await tester.pump();
    expect(delegate.selectables, isEmpty);

    upper.remove();
    upperRemoved = true;
    await tester.pumpAndSettle();
    expect(delegate.selectables.length, 1);
    delegate.dispatchSelectionEvent(const SelectAllSelectionEvent());
    expect(delegate.getSelectedContent()?.plainText, 'remaining');
    expect(tester.takeException(), isNull);
  });

  testWidgets('disposing a container cancels updates waiting for its first layout', (
    WidgetTester tester,
  ) async {
    final lower = OverlayEntry(
      maintainState: true,
      builder: (BuildContext context) =>
          const SelectionArea(child: Column(children: <Widget>[Text('first'), Text('second')])),
    );
    final upper = OverlayEntry(
      opaque: true,
      builder: (BuildContext context) => const Text('cover'),
    );
    await tester.pumpWidget(
      MaterialApp(home: Overlay(initialEntries: <OverlayEntry>[lower, upper])),
    );
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    lower.remove();
    upper.remove();
    await tester.pumpWidget(const SizedBox.shrink());
    lower.dispose();
    upper.dispose();
    await tester.pump();
    expect(tester.takeException(), isNull);
    expect(tester.binding.hasScheduledFrame, isFalse);
  });
}

class _SelectionDelegate extends StaticSelectionContainerDelegate {
  @override
  SelectedContent? getSelectedContent() {
    final contents = <String>[
      for (final Selectable selectable in selectables)
        if (selectable.getSelectedContent() case final SelectedContent content) content.plainText,
    ];
    return contents.isEmpty ? null : SelectedContent(plainText: contents.join('\n'));
  }
}
