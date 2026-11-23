// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

/// Exercises the real SDK placeholder without loading a native WebView plugin.
void main() {
  testWidgets('removed platform view ignores its pending layout callback', (tester) async {
    late _TestPlatformViewController removedController;
    _removeBeforeLayoutCallback(tester);

    await tester.pumpWidget(
      _platformView(onController: (controller) => removedController = controller),
    );

    expect(tester.takeException(), isNull);
    expect(removedController.disposed, isTrue);
    expect(removedController.creations, isEmpty);

    late _TestPlatformViewController replacementController;
    await tester.pumpWidget(
      _platformView(onController: (controller) => replacementController = controller),
    );
    await tester.pump();

    expect(tester.takeException(), isNull);
    expect(replacementController.creations, [const Size(200, 100)]);
    expect(find.text('platform view ready'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
    expect(replacementController.disposed, isTrue);
  });

  testWidgets('attached platform view receives its size and global position', (tester) async {
    late _TestPlatformViewController controller;
    await tester.pumpWidget(_platformView(onController: (value) => controller = value));
    await tester.pump();

    expect(tester.takeException(), isNull);
    expect(controller.creations, [const Size(200, 100)]);
    expect(controller.positions, [const Offset(30, 40)]);
    expect(find.text('platform view ready'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
    expect(controller.disposed, isTrue);
  });

  testWidgets('rapid platform view removal preserves subsequent rendering', (tester) async {
    for (var cycle = 0; cycle < 30; cycle++) {
      late _TestPlatformViewController removed;
      _removeBeforeLayoutCallback(tester);
      await tester.pumpWidget(_platformView(onController: (controller) => removed = controller));
      expect(tester.takeException(), isNull, reason: 'removal cycle $cycle');
      expect(removed.disposed, isTrue);
      expect(removed.creations, isEmpty);

      late _TestPlatformViewController replacement;
      await tester.pumpWidget(
        _platformView(onController: (controller) => replacement = controller),
      );
      await tester.pump();
      expect(tester.takeException(), isNull, reason: 'replacement cycle $cycle');
      expect(replacement.creations, [const Size(200, 100)]);
      expect(replacement.positions, [const Offset(30, 40)]);
      expect(find.text('platform view ready'), findsOneWidget);
      await tester.pumpWidget(const SizedBox.shrink());
      expect(replacement.disposed, isTrue);
    }
  });
}

/// Unmounts the real placeholder after layout and before its queued callback.
void _removeBeforeLayoutCallback(WidgetTester tester) {
  tester.binding.addPostFrameCallback((_) {
    final RenderObject placeholder = tester.renderObject(find.byType(PlatformViewLink));
    expect(placeholder.attached, isTrue);
    tester.binding.attachRootWidget(tester.binding.wrapWithDefaultView(const SizedBox.shrink()));
    tester.binding.buildOwner!.buildScope(tester.binding.rootElement!);
    tester.binding.buildOwner!.finalizeTree();
    expect(placeholder.attached, isFalse);
  });
}

/// Creates a size-dependent platform view with a visible replacement surface.
Widget _platformView({required void Function(_TestPlatformViewController) onController}) =>
    Directionality(
      textDirection: TextDirection.ltr,
      child: Align(
        alignment: Alignment.topLeft,
        child: Padding(
          padding: const EdgeInsets.only(left: 30, top: 40),
          child: SizedBox(
            width: 200,
            height: 100,
            child: PlatformViewLink(
              viewType: 'operit-lifecycle-test',
              onCreatePlatformView: (params) {
                final controller = _TestPlatformViewController(
                  params.id,
                  params.onPlatformViewCreated,
                );
                onController(controller);
                return controller;
              },
              surfaceFactory: (context, controller) => const Text('platform view ready'),
            ),
          ),
        ),
      ),
    );

class _TestPlatformViewController extends PlatformViewController {
  /// Records framework creation calls and signals surface readiness.
  _TestPlatformViewController(this.viewId, this.onCreated);

  @override
  final int viewId;
  final PlatformViewCreatedCallback onCreated;
  final creations = <Size?>[];
  final positions = <Offset?>[];
  bool disposed = false;

  /// Requires the placeholder's initial layout before creating the view.
  @override
  bool get awaitingCreation => !disposed && creations.isEmpty;

  /// Completes creation only while this controller is live.
  @override
  Future<void> create({Size? size, Offset? position}) async {
    expect(disposed, isFalse);
    creations.add(size);
    positions.add(position);
    onCreated(viewId);
  }

  /// Records disposal before any pending placeholder callback is processed.
  @override
  Future<void> dispose() async {
    disposed = true;
  }

  /// Accepts focus clearing without a native channel.
  @override
  Future<void> clearFocus() async {}

  /// Accepts pointer events without a native channel.
  @override
  Future<void> dispatchPointerEvent(PointerEvent event) async {}
}
