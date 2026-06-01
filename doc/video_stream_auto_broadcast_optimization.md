# 足球自动转播系统的视频流方案优化

本文结合 `src/video_stream` 现有文件，将“摄像头输入管理”优化为“视频文件动态裁剪转播处理器”，用于足球比赛视频的自动转播输出。

## 1. 模块架构图

```mermaid
flowchart LR
    A["输入视频 / 摄像头"] --> B["VideoStreamManager"]
    B --> C["ROI 回调<br/>检测模块每帧输出中心点"]
    C --> D["FootballAutoBroadcastProcessor"]
    D --> E["滑动窗口平滑"]
    E --> F["动态裁剪 / 边界保护"]
    F --> G["尺寸适配 / 视频写出"]
    G --> H["自动转播输出视频"]

    I["detection 模块"] --> C
    H --> J["editor 模块"]
    J --> K["全场高光 / 个人高光"]
```

### 设计要点

- `video_stream` 负责读取视频、处理 ROI、平滑裁剪和输出新视频。
- `detection` 每帧提供 `(center_x, center_y)` 作为关注点。
- `editor` 复用输出视频和事件信息，生成全场高光与个人高光。
- 采用滑动窗口平滑，减少镜头抖动和来回跳动。
- 采用边界复制填充，避免裁剪中心贴边时画面直接断裂。

## 2. 关键函数签名

文件位置：

- [src/video_stream/video_stream.h](</D:\LUO\football-auto-broadcast\football-auto-broadcast\src\video_stream\video_stream.h>)
- [src/video_stream/video_stream.cpp](</D:\LUO\football-auto-broadcast\football-auto-broadcast\src\video_stream\video_stream.cpp>)

核心接口：

```cpp
class FootballAutoBroadcastProcessor {
public:
    using RoiCallback = std::function<std::optional<cv::Point2f>(
        int frameIndex,
        double timestamp,
        const cv::Mat& frame
    )>;

    struct Config {
        cv::Size outputSize = cv::Size(1280, 720);
        int smoothingWindow = 5;
        double zoomFactor = 1.35;
        bool useReplicateBorder = true;
    };

    bool processFile(
        const std::string& inputPath,
        const std::string& outputPath,
        const RoiCallback& roiCallback,
        const Config& config = Config()
    );

    static cv::Point2f defaultCenter(const cv::Mat& frame);
};
```

推荐回调形式：

```cpp
std::optional<cv::Point2f> roiCallback(
    int frameIndex,
    double timestamp,
    const cv::Mat& frame
);
```

## 3. 核心代码实现

当前实现的优化逻辑：

1. 打开输入视频，读取 FPS 和首帧尺寸。
2. 每帧调用 ROI 回调，获取目标中心点。
3. 对中心点做滑动窗口平均，得到平滑后的转播焦点。
4. 依据 `zoomFactor` 计算裁剪窗口，并保持输出比例稳定。
5. 裁剪窗口越界时使用 `BORDER_REPLICATE` 进行边界补齐。
6. 将裁剪结果统一缩放到输出尺寸并写入新视频。

实现上的三个优化点：

- `roiCallback` 为空或失效时，自动回退到上一帧中心或画面中心。
- `smoothingWindow` 可调，窗口越大越稳，越小越跟手。
- `zoomFactor` 可调，适合“全场视角”和“跟球视角”之间切换。

关键代码已经加入 `src/video_stream/video_stream.cpp`，不破坏原有的：

- `VideoStreamManager`
- `DualVideoStreamManager`

因此现有摄像头采集流程仍可继续使用，新处理器可单独用于视频文件转播。

## 4. 测试用例

### 用例 1：基本裁剪输出

- 输入：一段 1080p 足球比赛视频。
- ROI：每帧返回球或主要进攻区域中心。
- 期望：输出视频可正常播放，画面持续跟随关注点。

### 用例 2：ROI 抖动测试

- 输入：ROI 中心点在相邻帧间轻微抖动。
- 参数：`smoothingWindow = 5`。
- 期望：输出画面移动更平滑，不出现明显左右来回跳。

### 用例 3：边缘目标测试

- 输入：关注点靠近画面边缘。
- 参数：`useReplicateBorder = true`。
- 期望：裁剪仍能输出完整画面，不出现黑边或裁剪破口。

### 用例 4：ROI 缺失测试

- 输入：某些帧检测失败，回调返回 `std::nullopt`。
- 期望：系统自动沿用上一帧平滑中心，不中断输出。

### 用例 5：不同分辨率测试

- 输入：720p、1080p、4K 视频各一段。
- 期望：输出尺寸统一，帧率保持与输入一致或回退到默认 FPS。

### 可写进答辩的评价点

- 视频输出连续性
- 镜头平滑度
- 关键球员 / 足球入镜率
- 边缘裁剪鲁棒性
- 自动转播延迟

