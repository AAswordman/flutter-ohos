# PlatformView 占位布局回调的生命周期修复

## 原因与范围

2026-11-23 的 Android 诊断日志在 21:13:21.620 记录：插件 WebView 解绑后，
`_PlatformViewPlaceholderBox.performLayout` 注册的 post-frame 回调仍然执行
`localToGlobal()`，在 `RenderObject.getTransformTo` 的 `owner!.rootNode!` 处抛出空指针。

当前固定 SDK `3.41.10-ohos-0.0.2-beta.operit.10`，源码提交
`5f7e9dc0c5d71e2a1b8532789a728a71fc4dae09`，包含这个缺口。
post-frame 回调不能注销，而占位 RenderObject 可以在布局之后、回调之前被移除。
此时它不再拥有有效的全局坐标空间。

修复在 Flutter 共享的 PlatformView 实现中检查 `attached`，让已经失效的布局通知结束，
有效视图继续正常报告尺寸与位置。没有捕获并吞掉异常，也没有跨卡片共享 WebView 控制器。
本次修复未修改应用构建脚本、workflow 或 `.fvmrc`。

这个修复解决日志中已确认的生命周期异常。调度器会单独捕获 post-frame 异常，
所以不能把该异常解释成整帧 layout/paint 作废；整个聊天区持续空白仍需真机复测确认。

## 自维护 SDK 源码

直接修改项目选中的自维护 SDK 仓库：
`C:/Users/12809/fvm/versions/3.41.10-ohos-0.0.2-beta.operit.10`，
远程为 `https://github.com/AAswordman/flutter-ohos.git`。

- 实现：`packages/flutter/lib/src/widgets/platform_view.dart`。
- 回归测试：`packages/flutter/test/widgets/platform_view_placeholder_lifecycle_test.dart`。

修复与测试均归 SDK 仓库管理，不通过应用构建阶段打补丁。
修复提交同步到 SDK 主分支 `oh-3.41.9-release`，尚未发布新的 SDK 版本；
重装原来的固定版本不会包含尚未发布的修复。

## 发布与应用接入

应用通过 FVM 固定 SDK tag，因此要让 CI 与其他开发机使用修复，需要发布包含该提交的新 tag，
再将应用 `.fvmrc` 指向新版本。现有 `.operit.10` tag 保持原提交。

本次修改仅涉及 Flutter Framework Dart 源码，不涉及 `engine/src`、Dart VM 或引擎 ABI。
新 SDK 版本可以复用当前各平台引擎产物，保留 `bin/internal/engine.version` 和现有产物下载 pin，
无需重新编译 Android、Windows、iOS 等原生引擎。更新 SDK 后重新构建并安装应用，
Framework 修复才会进入用户设备上的应用。

## 回归验证

在 SDK 仓库的 `packages/flutter` 下执行，Windows 使用 `flutter.bat`：

```text
../../bin/flutter test --no-pub test/widgets/platform_view_placeholder_lifecycle_test.dart test/widgets/platform_view_test.dart --name "PlatformViewLink|platform view"
```

新增测试使用真实 SDK 的 `PlatformViewLink` 和占位 RenderObject，仅用测试控制器替代原生插件：

- 在布局后、占位回调前卸载视图，确认控制器已销毁，旧回调不会创建原生视图或抛异常；
  随后创建的视图仍能正常显示。
- 有效视图收到正确尺寸 `200 × 100` 和全局位置 `(30, 40)`，正常创建流程不变。
- 连续执行 30 轮卸载与替换，每轮检查旧控制器销毁、替换视图创建和可见内容。

未修补 SDK：卸载用例在 `getTransformTo` 触发 `attached` 断言，正常创建用例通过。
修补后：上述 3 项测试通过。Debug 测试启用了断言，所以在访问空 `owner` 之前
就报告了同一路径的生命周期错误；诊断包中的运行时日志则在空值断言处失败。

SDK 环境中的新增回归与相关原有测试合计 19 项通过，覆盖普通创建、空尺寸、
纹理模式、Hybrid Composition、销毁、销毁后通知、widget 树替换、viewType 切换与焦点。
同时按 SDK 仓库要求清理了同一源码文件中原有的冗余非空断言、缺失文档和类型标注 lint；
非空参数约束仍由 Dart 类型系统保证。

未构建或安装 APK，尚未进行用户设备上的快速会话切换与插件卡片滚动验证。
