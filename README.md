## 简介

CSI-NN2 是 T-HEAD 提供的一组针对无剑 SoC 平台的神经网络库 API。抽象了各种常用的网络层的接口，并且提供一系列已优化的二进制库。

CSI-NN2 的特性：

- C 代码版本的参考实现。
- 提供玄铁系列 CPU 的汇编优化实现。
- 支持对称量化和非对称量化。
- 支持8位定点，16位定点和16位浮点等数据类型。
- 兼容 NCHW 和 NHWC 格式。
- 搭配 [HHB](https://occ.t-head.cn/development/series/index?spm=a2cl5.14300690.0.0.4aca475a4yHCxV&id=3865005559921381376&type=kind) 实现代码自动调用。
- 覆盖 CPU，NPU 架构。
- 附加一些辅助接口，参考使用。

CSI-NN2 提供了完成的接口声明和接口的参考实现，各个设备提供商可以依此针对性的完成各个接口的优化工作。

## 文档说明

- [中文文档](https://www.yuque.com/za4k4z/isgz8o)

## 致谢

CSI-NN2 参考、借鉴了下列项目：
- [Caffe](https://github.com/BVLC/caffe)
- [Tensorflow](https://github.com/tensorflow/tensorflow)
- [ncnn](https://github.com/Tencent/ncnn)
- [MNN](https://github.com/alibaba/MNN)
- [Tengine](https://github.com/OAID/Tengine)
- [CMSIS_5](https://github.com/ARM-software/CMSIS_5)
