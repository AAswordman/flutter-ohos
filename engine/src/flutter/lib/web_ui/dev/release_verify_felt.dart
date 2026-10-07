// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'felt.dart' as felt;

/// Runs release verification with the web engine's required working directory.
Future<void> main(List<String> arguments) async {
  Directory.current = File.fromUri(Platform.script).parent.parent.path;
  await felt.main(arguments);
}
