| 方案 | 原理 | 难度与耗时 | 适用场景 |
| --- | --- | --- | --- |
| 方案 1：开启 NavMesh 自动落差链接 (Auto Drop Link) | 在场景的 `RecastNavMesh-Default` 属性中，开启 `Generate OffMesh Links`，并设置 `Max Drop Down Height = 400`。引擎烘焙时会自动沿着所有符合高度的边缘生成“单向跳下”链接。 | ⭐（1分钟，0代码，改个数值即可） | 场景边缘规则，小怪只要自然跑过去摔下来就行。 |
| 方案 2：放置 NavLinkProxy（行业标准做法） | 在高台边缘放置引擎自带的 `NavLinkProxy` Actor，它有两个点（起点在高台边缘，终点在地面）。AI 寻路就会把这两个点连起来，走到起点后直接走向终点摔下去。 | ⭐⭐（5分钟，摆放 Actor） | 精确控制哪些指定地点允许跳下。 |
| 方案 3：Smart NavLink + 扑击跳跃技能 (GAS) | 使用 `Smart NavLink`，AI 走到边缘时触发自定义事件，激活一个 `GA_bot_LeapDown`（播放一个从天而降的空中飞扑动画并 `LaunchCharacter`）。 | ⭐⭐⭐（30分钟，需要动作） | 精英怪、Boss 或需要帅气跳跃攻击动画的场合。 |