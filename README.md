# DeepFilterNet VST

基于 `JUCE 8.0.12 + FST` 的 DeepFilterNet 降噪插件工程，可同时构建 `VST2` 和 `VST3`。

## 仓库内容

- `DeepFilterNetVst/`：插件源代码、GUI、运行时资源嵌入逻辑。
- `DeepFilterNetVst/EmbeddedAssets/`：会被打包进插件的 `df.dll` 与模型文件。
- `extern/JUCE/`：JUCE 子模块。
- `extern/FST/`：FST 子模块，仅用于 JUCE 的 VST2 兼容头来源。

## 特性

- 双格式输出：可同时生成 `VST2` 和 `VST3`。
- 运行时内嵌：`df.dll` 与 `DeepFilterNet3_onnx.tar.gz` 会被打包进插件二进制。
- JUCE 原生 GUI：支持 `Denoise Strength` 与 `Post Filter` 参数。
- 自动重采样：宿主不是 `48 kHz` 时，插件内部会自动重采样到 `48 kHz` 后处理，再转回宿主采样率。

## 构建

前置要求：

- Windows
- CMake 3.22+
- Visual Studio Build Tools / MSVC x64
- Git（用于拉取子模块）

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

## 运行方式

- 插件首次加载时会把嵌入的 `df.dll` 和模型释放到系统临时目录缓存。
- 处理链路固定运行在 `48 kHz`。
- 当前音频处理逻辑为单声道降噪，输出会复制到所有输出通道。
- `VST2` 目标依赖 `FST` 兼容头；`VST3` 目标使用 JUCE 自带的 VST3 SDK 内容。

## 许可证

- 除第三方组件外，本仓库原创代码按 `AGPL-3.0-only` 提供，见 `LICENSE`。
- 第三方依赖仍适用其各自上游许可证。
