## 版本概述
本版本为Flutter OpenHarmony平台0.0.1版本（canary1），基于Flutter 3.41.9版本适配。本版本支持和完善OpenHarmony平台侧能力，提供平台化Channel、外接纹理、云端SDK等特性，并优化性能。

## Bug修复
- Fix OHOS platform view active touch cancellation
- 解决分栏功能跳转返回时的几个异常。1.使用go_router库时跳转主页有动画残留（闪烁）2.详情页非模态弹窗返回时会自动回到上一页 3.如果栈中没有主页，则虽然sdk会拦截主页移除，但是此时侧滑返回无效 4.popuntil特定场景下会死循环
- LTPO Performance Optimization
- 切换分栏实现方案，支持router路由方式下的分栏
- Frame Buffer PTS Optimization for Delayed Frame Presentation
- [OHOS] Fix PixelMap ReadPixels temp buffer cleanup
- fix：修复性能雷达滑动丢帧上报字段值问题

## 版本配套
- 引擎构建最低要求 API：**OpenHarmony API 23**
- 应用构建推荐适配 API：**OpenHarmony API 23**
- 应用构建最低适配 API：**OpenHarmony API 20**
- 应用最低运行 API：**OpenHarmony API 12**
- Flutter SDK：**3.41.9-ohos-0.0.2**（由于flutter版本解析规则，为了避免版本比较解析失败，将显示为3.41.10-ohos-0.0.2）

## Changelog
- [3.41.9-ohos-0.0.2](../CHANGELOG_OHOS.md)

## 赋能文档
- [文档链接](https://gitcode.com/openharmony-tpc/flutter_samples/tree/master/ohos/docs)
