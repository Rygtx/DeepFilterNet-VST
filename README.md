# DeepFilterNet VST

基于 `JUCE 8.0.12 + FST + Corrosion + 上游 DeepFilterNet/libDF` 的降噪插件工程，可同时构建 `VST2` 和 `VST3`。
当前实现通过本仓库内的 Rust bridge 静态链接上游 runtime，模型权重在编译期内嵌进静态库，Rust 部分由 CMake 通过 Corrosion 直接驱动构建。

## 仓库内容

- `DeepFilterNetVst/`：插件源代码、JUCE 集成与 GUI。
- `extern/Corrosion/`：Corrosion 子模块，用于把 Rust crate 直接导入 CMake。
- `extern/DeepFilterNet/`：上游 `Rikorose/DeepFilterNet` 子模块，提供 `libDF` 源码与默认模型。
- `rust/deepfilter_runtime_bridge/`：对上游 `deep_filter/libDF` 的静态桥接层。
- `extern/JUCE/`：JUCE 子模块。
- `extern/FST/`：FST 子模块，仅用于 JUCE 的 VST2 兼容头来源。

## 特性

- 双格式输出：可同时生成 `VST2` 和 `VST3`。
- 接近上游 stereo 路径：双声道不会先混成 mono，而是按多通道帧一起送入上游 `DfTract` runtime 处理。
- 编译期内嵌 runtime 与模型：bridge 通过本地 path 依赖引用 `extern/DeepFilterNet/libDF`，上游默认模型会被编进 Rust 静态库，插件只链接本地生成的 `deepfilter_runtime_bridge.lib`。
- JUCE 原生 GUI：支持 `Denoise Strength` 与 `Post Filter` 参数。
- 自动重采样：宿主不是 `48 kHz` 时，插件内部会自动重采样到 `48 kHz` 后处理，再转回宿主采样率。

## 构建

前置要求：

- Windows
- CMake 3.22+
- Visual Studio Build Tools / MSVC x64
- Rust 工具链（`cargo` / `rustc`，目标建议为 `x86_64-pc-windows-msvc`）
- Git（用于拉取子模块）

首次构建时，Cargo 会按 `rust/deepfilter_runtime_bridge/Cargo.lock` 固定版本解析依赖；上游 `DeepFilterNet` 源码来自仓库内的 `extern/DeepFilterNet` 子模块。

初始化子模块：

```bash
git submodule update --init --recursive
```

使用 Visual Studio 生成器：

```bash
cmake -S DeepFilterNetVst -B build/juce-vst -G "Visual Studio 17 2022" -A x64
cmake --build build/juce-vst --config Release
```

或在开发者命令行里使用 NMake：

```bash
cmake -S DeepFilterNetVst -B build/juce-vst-msvc -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/juce-vst-msvc
```

输出文件：

- `build/juce-vst/DeepFilterNetVst_artefacts/Release/VST/DeepFilterNet.dll`
- `build/juce-vst/DeepFilterNetVst_artefacts/Release/VST3/DeepFilterNet.vst3`
- `build/juce-vst-msvc/DeepFilterNetVst_artefacts/Release/VST/DeepFilterNet.dll`
- `build/juce-vst-msvc/DeepFilterNetVst_artefacts/Release/VST3/DeepFilterNet.vst3`

## GitHub CI

- 已添加普通 CI 工作流：`.github/workflows/ci.yml`
- 向任意分支 `push` 时，GitHub Actions 都会自动执行 Windows 构建检查
- 只有向 `main` 分支 `push` 时才会发布 GitHub 正式发行版，且 release 的 tag 和名称都直接使用源码中的版本号
- 发布时会上传：
  - `DeepFilterNet-v版本号-windows.zip`
  - `DeepFilterNet-v版本号-windows.sha256`
- 如果同名版本 tag 已经指向其他提交，工作流会失败，避免覆盖正式版
- 下载位置为仓库的 `Releases` 页面

示例：

```bash
git push origin main
git push origin feature/my-change
```

## 运行方式

- 默认模型的处理采样率为 `48 kHz`，宿主不是 `48 kHz` 时会自动做输入/输出重采样。
- 支持 `mono` 和 `stereo` 总线布局；其中 `stereo` 会按真正双声道路径进入上游 runtime，而不是先混成单声道再复制回去。
- `VST2` 目标依赖 `FST` 兼容头；`VST3` 目标使用 JUCE 自带的 VST3 SDK 内容。

## 许可证

- 除第三方组件外，本仓库原创代码按 `AGPL-3.0-only` 提供，见 `LICENSE`。
- 第三方依赖仍适用其各自上游许可证。
