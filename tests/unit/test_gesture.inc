// ─────────────────────────────────────────────────────────────────────────────
// test_gesture.inc — GESTURE 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────


// 喂入一串轨迹点, 返回识别出的方向编码 (无效手势返回空串)。
static std::string recognize(std::initializer_list<TrackPoint> pts) {
    GestureRecognizer r;  // 默认配置: minSegmentDistance=14, samplingInterval=2
    r.reset();
    for (const auto& p : pts) r.addPoint(p.x, p.y);
    auto res = r.finalize();
    return res ? res->code : std::string{};
}

// -----------------------------------------------------------------------------
// 1. 手势识别器测试套件
// -----------------------------------------------------------------------------
TEST(GestureRecognizerTest, DirectionEncodingAndSmoothing) {
    // 屏幕坐标 Y 轴向下: 向上 = y 减小
    EXPECT_EQ(recognize({{0, 0}, {100, 0}}), "R");    // →
    EXPECT_EQ(recognize({{100, 0}, {0, 0}}), "L");    // ←
    EXPECT_EQ(recognize({{0, 100}, {0, 0}}), "U");    // ↑
    EXPECT_EQ(recognize({{0, 0}, {0, 100}}), "D");    // ↓

    // 对角线 (单段)
    EXPECT_EQ(recognize({{0, 100}, {100, 0}}), "UR"); // ↗
    EXPECT_EQ(recognize({{0, 0}, {100, 100}}), "DR"); // ↘

    // 多段 (L 形及连续拐弯，基于拐点检测)
    EXPECT_EQ(recognize({{0, 100}, {100, 100}, {100, 0}}), "R-U"); // → 然后 ↑
    EXPECT_EQ(recognize({{100, 0}, {0, 0}, {0, 100}}), "L-D");     // ← 然后 ↓
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {50, 50}}), "D-R");      // ↓ 然后 →
    EXPECT_EQ(recognize({{0, 0}, {50, 0}, {50, 50}}), "R-D");      // → 然后 ↓
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {-50, 50}}), "D-L");     // ↓ 然后 ←
    EXPECT_EQ(recognize({{0, 0}, {0, -50}, {50, -50}}), "U-R");    // ↑ 然后 →
    EXPECT_EQ(recognize({{0, 0}, {0, -50}, {-50, -50}}), "U-L");   // ↑ 然后 ←
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {50, 50}, {50, 100}}), "D-R-D"); // ↓ → ↓

    // 方向序列与箭头转换辅助函数
    std::vector<Direction> dirs = {Direction::Left, Direction::Down};
    EXPECT_EQ(directionsToCode(dirs), "L-D");
    EXPECT_EQ(directionsToArrowString(dirs), "←↓");

    std::vector<Direction> singleDir = {Direction::Up};
    EXPECT_EQ(directionsToCode(singleDir), "U");
    EXPECT_EQ(directionsToArrowString(singleDir), "↑");

    // 防抖: 微位移小于最小段距离 (如 < 14px) → 视为普通点击/无效手势
    EXPECT_EQ(recognize({{0, 0}, {8, 0}}), "");

    // 智能转弯圆角平滑消抖 (Corner Fillet Folding)
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Right})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::DownRight, Direction::Down})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownLeft, Direction::Left})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpRight, Direction::Right})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpLeft, Direction::Left})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::DownLeft, Direction::Down})), "L-D");
    
    // 孤立回弹微抖动消除 (Rebound Jitter Elimination)
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Down})), "D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::UpRight, Direction::Right})), "R");

    // 真正对角手势保留（整笔就是一条斜线）
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight})), "DR");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpLeft})), "UL");

    // 未走完的转角圆角：对角段补成直角第二段
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight, Direction::Right})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownLeft})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownLeft, Direction::Left})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpRight})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpRight, Direction::Right})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpLeft})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpLeft, Direction::Left})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::DownRight})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::DownLeft})), "L-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::UpRight})), "R-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::UpLeft})), "L-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::DownRight})), "U-DR");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Right, Direction::DownRight})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Left, Direction::DownLeft})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Up, Direction::Right, Direction::UpRight})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Up, Direction::Left, Direction::UpLeft})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Right, Direction::Down, Direction::DownRight})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Right, Direction::Up, Direction::UpRight})), "R-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Left, Direction::Down, Direction::DownLeft})), "L-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Left, Direction::Up, Direction::UpLeft})), "L-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Right, Direction::Down})), "D-R-D");

    EXPECT_EQ(tokenToDirection("XX"), Direction::None);
    EXPECT_EQ(tokenToDirection(""), Direction::None);
    EXPECT_TRUE(codeToDirections("").empty());
    EXPECT_EQ(directionsToCode(codeToDirections("D-R")), "D-R");
    EXPECT_EQ(directionsToCode(codeToDirections("DR")), "DR");
    EXPECT_EQ(codeToDirections("D-XX-R").size(), 2u);

    EXPECT_EQ(*expandSingleDiagonalCode("DR"), "D-R");
    EXPECT_EQ(*expandSingleDiagonalCode("DL"), "D-L");
    EXPECT_EQ(*expandSingleDiagonalCode("UR"), "U-R");
    EXPECT_EQ(*expandSingleDiagonalCode("UL"), "U-L");
    EXPECT_FALSE(expandSingleDiagonalCode("D-R").has_value());
    EXPECT_FALSE(expandSingleDiagonalCode("L").has_value());

    EXPECT_EQ(refineCodeWithPath("", {{0, 0}, {10, 10}}, 14), "");
    EXPECT_EQ(refineCodeWithPath("D-R", {{0, 0}, {0, 80}, {80, 80}}, 14), "D-R");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {8, 400}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {120, 400}}, 14), "D");
    // 整笔持续向左倾斜仍是「下」，没有真实转角就不能升级成「下-左」。
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {-15, 30}, {-30, 60}, {-45, 90}, {-60, 120}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("DR", {{0, 0}, {40, 40}, {80, 80}}, 14), "DR");
    {
        std::vector<TrackPoint> lDownRight;
        for (int y = 0; y <= 80; y += 8) lDownRight.push_back({0, y});
        for (int x = 8; x <= 80; x += 8) lDownRight.push_back({x, 80});
        EXPECT_EQ(refineCodeWithPath("D", lDownRight, 14), "D-R");
        std::vector<TrackPoint> lDownLeft;
        for (int y = 0; y <= 80; y += 8) lDownLeft.push_back({80, y});
        for (int x = 72; x >= 0; x -= 8) lDownLeft.push_back({x, 80});
        EXPECT_EQ(refineCodeWithPath("D", lDownLeft, 14), "D-L");
        std::vector<TrackPoint> lUpRight;
        for (int y = 80; y >= 0; y -= 8) lUpRight.push_back({0, y});
        for (int x = 8; x <= 80; x += 8) lUpRight.push_back({x, 0});
        EXPECT_EQ(refineCodeWithPath("U", lUpRight, 14), "U-R");
        std::vector<TrackPoint> lRightDown;
        for (int x = 0; x <= 80; x += 8) lRightDown.push_back({x, 0});
        for (int y = 8; y <= 80; y += 8) lRightDown.push_back({80, y});
        EXPECT_EQ(refineCodeWithPath("R", lRightDown, 14), "R-D");
        std::vector<TrackPoint> slantDown;
        for (int y = 0; y <= 200; y += 10) slantDown.push_back({y / 20, y});
        EXPECT_EQ(refineCodeWithPath("D", slantDown, 14), "D");
    }

    auto recognizePath = [](const std::vector<TrackPoint>& pts) {
        GestureRecognizer r;
        r.reset();
        for (const auto& p : pts) r.addPoint(p.x, p.y);
        auto res = r.finalize();
        return res ? res->code : std::string{};
    };
    auto driftTurn = [](int ax, int ay, int bx, int by) {
        std::vector<TrackPoint> pts;
        for (int i = 0; i <= 24; ++i) pts.push_back({ax * i, ay * i});
        const auto last = pts.back();
        for (int i = 1; i <= 24; ++i) {
            pts.push_back({last.x + bx * i, last.y + by * i});
        }
        return pts;
    };
    EXPECT_EQ(recognizePath(driftTurn(0, 4, 4, 1)), "D-R");
    EXPECT_EQ(recognizePath(driftTurn(0, 4, -4, 1)), "D-L");
    EXPECT_EQ(recognizePath(driftTurn(0, -4, 4, -1)), "U-R");
    EXPECT_EQ(recognizePath(driftTurn(0, -4, -4, -1)), "U-L");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, 1, 4)), "R-D");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, 1, -4)), "R-U");
    EXPECT_EQ(recognizePath(driftTurn(-4, 0, -1, 4)), "L-D");
    EXPECT_EQ(recognizePath(driftTurn(-4, 0, -1, -4)), "L-U");
    EXPECT_EQ(recognizePath(driftTurn(0, 4, 0, -4)), "D-U");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, -4, 0)), "R-L");

    {
        std::vector<TrackPoint> sloppyDown;
        sloppyDown.push_back({0, 0});
        sloppyDown.push_back({10, 10});
        sloppyDown.push_back({16, 16});
        for (int y = 20; y <= 120; y += 4) sloppyDown.push_back({16, y});
        EXPECT_EQ(recognizePath(sloppyDown), "D");
    }

    // 自然「下再右」：横扫时手腕仍会略微下沉，不能把整笔锁死成 D
    {
        std::vector<TrackPoint> naturalDownRight;
        for (int y = 0; y <= 80; y += 4) naturalDownRight.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) naturalDownRight.push_back({x, 80 + x / 8});
        GestureRecognizer natural;
        natural.reset();
        for (const auto& p : naturalDownRight) natural.addPoint(p.x, p.y);
        auto naturalRes = natural.finalize();
        ASSERT_TRUE(naturalRes.has_value());
        EXPECT_EQ(naturalRes->code, "D-R");
    }

    {
        std::vector<TrackPoint> slowDownRight;
        for (int y = 0; y <= 100; y += 2) slowDownRight.push_back({0, y});
        for (int x = 2; x <= 100; x += 2) slowDownRight.push_back({x, 100});
        EXPECT_EQ(recognizePath(slowDownRight), "D-R");
        std::vector<TrackPoint> slowDownLeft;
        for (int y = 0; y <= 100; y += 2) slowDownLeft.push_back({100, y});
        for (int x = 98; x >= 0; x -= 2) slowDownLeft.push_back({x, 100});
        EXPECT_EQ(recognizePath(slowDownLeft), "D-L");
        std::vector<TrackPoint> slowUpRight;
        for (int y = 100; y >= 0; y -= 2) slowUpRight.push_back({0, y});
        for (int x = 2; x <= 100; x += 2) slowUpRight.push_back({x, 0});
        EXPECT_EQ(recognizePath(slowUpRight), "U-R");
        std::vector<TrackPoint> slowUpLeft;
        for (int y = 100; y >= 0; y -= 2) slowUpLeft.push_back({100, y});
        for (int x = 98; x >= 0; x -= 2) slowUpLeft.push_back({x, 0});
        EXPECT_EQ(recognizePath(slowUpLeft), "U-L");
        std::vector<TrackPoint> slowRightDown;
        for (int x = 0; x <= 100; x += 2) slowRightDown.push_back({x, 0});
        for (int y = 2; y <= 100; y += 2) slowRightDown.push_back({100, y});
        EXPECT_EQ(recognizePath(slowRightDown), "R-D");
        std::vector<TrackPoint> slowRightUp;
        for (int x = 0; x <= 100; x += 2) slowRightUp.push_back({x, 100});
        for (int y = 98; y >= 0; y -= 2) slowRightUp.push_back({100, y});
        EXPECT_EQ(recognizePath(slowRightUp), "R-U");
        std::vector<TrackPoint> slowLeftDown;
        for (int x = 100; x >= 0; x -= 2) slowLeftDown.push_back({x, 0});
        for (int y = 2; y <= 100; y += 2) slowLeftDown.push_back({0, y});
        EXPECT_EQ(recognizePath(slowLeftDown), "L-D");
        std::vector<TrackPoint> slowLeftUp;
        for (int x = 100; x >= 0; x -= 2) slowLeftUp.push_back({x, 100});
        for (int y = 98; y >= 0; y -= 2) slowLeftUp.push_back({0, y});
        EXPECT_EQ(recognizePath(slowLeftUp), "L-U");
    }

    {
        std::vector<TrackPoint> slantDown;
        for (int y = 0; y <= 240; y += 4) slantDown.push_back({y / 20, y});
        EXPECT_EQ(recognizePath(slantDown), "D");
        std::vector<TrackPoint> slantDownLeft;
        for (int y = 0; y <= 240; y += 4) slantDownLeft.push_back({-y * 13 / 25, y});
        EXPECT_EQ(recognizePath(slantDownLeft), "D");
        std::vector<TrackPoint> slantRight;
        for (int x = 0; x <= 240; x += 4) slantRight.push_back({x, x / 20});
        EXPECT_EQ(recognizePath(slantRight), "R");

        // 明确的 45° 对角线与真正组合手势不能被基准方向容错吞掉。
        std::vector<TrackPoint> diagonalDownLeft;
        for (int y = 0; y <= 120; y += 4) diagonalDownLeft.push_back({-y, y});
        EXPECT_EQ(recognizePath(diagonalDownLeft), "DL");
    }

    {
        std::vector<TrackPoint> drHook;
        for (int y = 0; y <= 80; y += 4) drHook.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) drHook.push_back({x, 80});
        for (int i = 1; i <= 6; ++i) drHook.push_back({80 + i * 2, 80 + i * 2});
        EXPECT_EQ(recognizePath(drHook), "D-R");
        std::vector<TrackPoint> urHook;
        for (int y = 80; y >= 0; y -= 4) urHook.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) urHook.push_back({x, 0});
        for (int i = 1; i <= 6; ++i) urHook.push_back({80 + i * 2, -i * 2});
        EXPECT_EQ(recognizePath(urHook), "U-R");
        std::vector<TrackPoint> rdHook;
        for (int x = 0; x <= 80; x += 4) rdHook.push_back({x, 0});
        for (int y = 4; y <= 80; y += 4) rdHook.push_back({80, y});
        for (int i = 1; i <= 6; ++i) rdHook.push_back({80 + i * 2, 80 + i * 2});
        EXPECT_EQ(recognizePath(rdHook), "R-D");
    }

    {
        std::vector<TrackPoint> downRightDown;
        for (int y = 0; y <= 60; y += 4) downRightDown.push_back({0, y});
        for (int x = 4; x <= 60; x += 4) downRightDown.push_back({x, 60});
        for (int y = 64; y <= 120; y += 4) downRightDown.push_back({60, y});
        EXPECT_EQ(recognizePath(downRightDown), "D-R-D");
        std::vector<TrackPoint> leftUpRight;
        for (int x = 120; x >= 60; x -= 4) leftUpRight.push_back({x, 120});
        for (int y = 116; y >= 60; y -= 4) leftUpRight.push_back({60, y});
        for (int x = 64; x <= 120; x += 4) leftUpRight.push_back({x, 60});
        EXPECT_EQ(recognizePath(leftUpRight), "L-U-R");
    }

    // 乱晃反悔与原地打圈自动取消判定
    GestureRecognizer scribbleRecognizer;
    scribbleRecognizer.reset();
    std::vector<TrackPoint> scribblePoints = {
        {0, 0}, {50, 0}, {0, 0}, {50, 0}, {0, 0}, {50, 0}, {0, 0}
    };
    for (const auto& p : scribblePoints) {
        scribbleRecognizer.addPoint(p.x, p.y);
    }
    EXPECT_TRUE(scribbleRecognizer.isScribbleCanceled());
    EXPECT_FALSE(scribbleRecognizer.finalize().has_value());

    // HEX 颜色解析测试
    auto parsed = easy::core::parseHexColor("#FF0000");
    EXPECT_EQ(parsed.r, 1.0f);
    EXPECT_EQ(parsed.g, 0.0f);
    EXPECT_EQ(parsed.b, 0.0f);
    auto invalidHex = easy::core::parseHexColor("invalid");
    EXPECT_GT(invalidHex.r, 0.0f);

    // 自定义最小识别距离 (minSegmentDistance) 测试
    {
        RecognizerConfig customCfg;
        customCfg.minSegmentDistance = 30;
        GestureRecognizer customRecognizer(customCfg);
        customRecognizer.reset();
        customRecognizer.addPoint(0, 0);
        customRecognizer.addPoint(20, 0);
        auto res20 = customRecognizer.finalize();
        EXPECT_FALSE(res20.has_value()); // 20px < 30px 阈值，应判定为无效手势

        customRecognizer.reset();
        customRecognizer.addPoint(0, 0);
        customRecognizer.addPoint(50, 0);
        auto res50 = customRecognizer.finalize();
        ASSERT_TRUE(res50.has_value());
        EXPECT_EQ(res50->code, "R"); // 50px >= 30px 阈值，应识别为向右
    }
}

TEST(GestureInputPolicyTest, TriggerDownHonorsMode) {
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::RightOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::RightOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::RightOnly));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::MiddleOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::MiddleOnly));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::X1Only));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::X1Only));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::X2Only));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::X2Only));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::Both));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::All));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::LeftDown, TriggerMode::All));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::Move, TriggerMode::All));
}

TEST(GestureInputPolicyTest, TriggerUpPairsWithActualButton) {
    EXPECT_EQ(triggerUpFor(MouseEventType::RightDown), MouseEventType::RightUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::MiddleDown), MouseEventType::MiddleUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::X1Down), MouseEventType::X1Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::X2Down), MouseEventType::X2Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::LeftDown), MouseEventType::LeftUp);
}

TEST(GestureInputPolicyTest, LeftClickAlwaysCancelsTracking) {
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftUp, MouseEventType::MiddleDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::LeftDown, MouseEventType::LeftDown));
}

TEST(GestureInputPolicyTest, OppositeMouseButtonCancelsTracking) {
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::MiddleDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::RightDown, MouseEventType::MiddleDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::X1Down, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::X2Down, MouseEventType::X1Down));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::RightDown, MouseEventType::RightDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::X1Down, MouseEventType::X1Down));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::Move, MouseEventType::RightDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::RightUp, MouseEventType::RightDown));
}

TEST(GestureInputPolicyTest, EasyToolsUiAndOverlayClassNames) {
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_SettingsWindow"));
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_GestureOverlay"));
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_TrayWindow"));
    EXPECT_FALSE(isEasyToolsUiClassName(L"EasyTools"));
    EXPECT_FALSE(isEasyToolsUiClassName(L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(isEasyToolsUiClassName(L""));
    EXPECT_TRUE(isGestureOverlayClassName(L"EasyTools_GestureOverlay"));
    EXPECT_FALSE(isGestureOverlayClassName(L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(isGestureOverlayClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isGesturePassThroughClassName(L"EasyTools_GestureOverlay"));
    EXPECT_TRUE(isGesturePassThroughClassName(L"EasyTools_ToastOverlay"));
    EXPECT_FALSE(isGesturePassThroughClassName(L"EasyTools_SettingsWindow"));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(false, false, true));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(true, true, true));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(true, false, false));
    EXPECT_FALSE(gestureHitTestShouldSkipCandidate(true, false, true));
    EXPECT_TRUE(gestureHitTestAcceptsWindow(true, false, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, true, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, false, true, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(false, false, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, false, false, false));
    EXPECT_EQ(parseGestureTargetMode("foreground"), GestureTargetMode::Foreground);
    EXPECT_EQ(parseGestureTargetMode("underPointer"), GestureTargetMode::UnderPointer);
    EXPECT_EQ(parseGestureTargetMode("nope"), GestureTargetMode::UnderPointer);
    EXPECT_STREQ(gestureTargetModeKey(GestureTargetMode::Foreground), "foreground");
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, true, true, true), 0);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, false, true, true), 1);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, false, false, true), -1);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::Foreground, true, true, true), 2);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::Foreground, true, true, false), -1);
    EXPECT_TRUE(keyStrokeShouldPostClose(MOD_ALT, VK_F4));
    EXPECT_FALSE(keyStrokeShouldPostClose(MOD_ALT | MOD_CONTROL, VK_F4));
    EXPECT_FALSE(keyStrokeShouldPostClose(MOD_CONTROL, 'W'));
    EXPECT_FALSE(keyStrokeShouldPostClose(0, VK_F4));
    EXPECT_TRUE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL, 'W'));
    EXPECT_TRUE(keyStrokeShouldDismissEasyToolsUi(MOD_ALT, VK_F4));
    EXPECT_FALSE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL | MOD_SHIFT, 'W'));
    EXPECT_FALSE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL, 'T'));
    EXPECT_TRUE(keyStrokeIsCtrlW(MOD_CONTROL, 'W'));
    EXPECT_FALSE(keyStrokeIsCtrlW(MOD_CONTROL | MOD_SHIFT, 'W'));
    EXPECT_TRUE(classNameStartsWith(L"Chrome_WidgetWin_1", L"Chrome_WidgetWin"));
    EXPECT_FALSE(classNameStartsWith(L"OrpheusBrowserHost", L"Chrome_WidgetWin"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"Chrome_WidgetWin_0"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"MozillaWindowClass"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"CabinetWClass"));
    EXPECT_FALSE(isTabbedBrowserClassName(L"OrpheusBrowserHost"));
    EXPECT_FALSE(isTabbedBrowserClassName(L"Qt51514QWindowIcon"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"OrpheusBrowserHost"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"CefBrowserWindow"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"Qt51514QWindowIcon"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"ApplicationFrameWindow"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(isProductivityToolkitClassName(L"UnityWndClass"));
    EXPECT_TRUE(shouldAutoBypassFullscreenGestures(true, false));
    EXPECT_FALSE(shouldAutoBypassFullscreenGestures(true, true));
    EXPECT_FALSE(shouldAutoBypassFullscreenGestures(false, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(true, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(true, true));
    EXPECT_TRUE(overlayPresentShouldForceTopmost(false, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(false, true));
    EXPECT_TRUE(windowUsesCompositorSurface(WS_EX_NOREDIRECTIONBITMAP));
    EXPECT_TRUE(windowUsesCompositorSurface(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW));
    EXPECT_FALSE(windowUsesCompositorSurface(WS_EX_LAYERED | WS_EX_TOPMOST));
    EXPECT_FALSE(windowUsesCompositorSurface(0));
    EXPECT_EQ(classifyProcessIntegrityQuery(false, false, false, false, false),
              ProcessIntegrityRelation::Unknown);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, false, false, false, false),
              ProcessIntegrityRelation::Higher);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, false, false, false, true),
              ProcessIntegrityRelation::Unknown);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, false, false),
              ProcessIntegrityRelation::SameOrLower);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, true, false),
              ProcessIntegrityRelation::Higher);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, true, true),
              ProcessIntegrityRelation::SameOrLower);
    EXPECT_FALSE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::Higher));
    EXPECT_TRUE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::SameOrLower));
    EXPECT_TRUE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::Unknown));
    EXPECT_TRUE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Higher, false));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Higher, true));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::SameOrLower, false));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Unknown, false));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_ALT, VK_F4, L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"OrpheusBrowserHost"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"Qt51514QWindowIcon"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'T', L"OrpheusBrowserHost"));
    EXPECT_EQ(resolveCloseableWindow(nullptr), nullptr);
    EXPECT_FALSE(windowStillAcceptsClose(false, true));
    EXPECT_FALSE(windowStillAcceptsClose(true, false));
    EXPECT_TRUE(windowStillAcceptsClose(true, true));
    EXPECT_FALSE(closeShouldSendKeyFallback(true, false));
    EXPECT_TRUE(closeShouldSendKeyFallback(true, true));
    EXPECT_FALSE(closeShouldSendKeyFallback(false, true));
    EXPECT_GT(kCloseObserveTimeoutMs, 0u);
}

TEST(GestureInputPolicyTest, ShiftRightClickBypassesGesturesOnlyInSearchWindow) {
    EXPECT_TRUE(shouldBypassGestureForNativeSearchMenu(L"EasyTools_SearchWindow", true));
    EXPECT_FALSE(shouldBypassGestureForNativeSearchMenu(L"EasyTools_SearchWindow", false));
    EXPECT_FALSE(shouldBypassGestureForNativeSearchMenu(L"CabinetWClass", true));
    EXPECT_FALSE(shouldBypassGestureForNativeSearchMenu(L"EasyTools_SettingsWindow", true));
}

TEST(GestureInputPolicyTest, ResultToastRequiresRecognitionOrExcessive) {
    EXPECT_TRUE(shouldShowGestureResultToast(true, true, false));
    EXPECT_FALSE(shouldShowGestureResultToast(false, true, false));
    EXPECT_FALSE(shouldShowGestureResultToast(true, false, false));
    EXPECT_TRUE(shouldShowGestureResultToast(false, true, true));
}

TEST(GestureInputPolicyTest, OverlaySurfaceGrowsOnly) {
    int left = 100, top = 100, right = 200, bottom = 200;
    growOverlayRect(left, top, right, bottom, 0, 0, 256, 256);
    EXPECT_EQ(left, 0);
    EXPECT_EQ(top, 0);
    EXPECT_EQ(right, 256);
    EXPECT_EQ(bottom, 256);
    EXPECT_TRUE(overlaySurfaceContains(10, 10, 20, 20, 0, 0, 256, 256));
    EXPECT_FALSE(overlaySurfaceContains(-1, 0, 10, 10, 0, 0, 256, 256));
    EXPECT_FALSE(overlaySurfaceContains(10, 10, 20, 20, 0, 0, 0, 0));
    int emptyL = 8, emptyT = 8, emptyR = 16, emptyB = 16;
    growOverlayRect(emptyL, emptyT, emptyR, emptyB, 0, 0, 0, 0);
    EXPECT_EQ(emptyL, 8);
    EXPECT_EQ(emptyR, 16);
}

TEST(GestureInputPolicyTest, OverlayRejectsOversizedLeftoverSurface) {
    EXPECT_TRUE(overlayCanReuseSurface(256, 256, 256, 256, 2));
    EXPECT_TRUE(overlayCanReuseSurface(256, 256, 360, 360, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 1792, 1792, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 2816, 1024, 2));
    EXPECT_FALSE(overlayCanReuseSurface(0, 256, 256, 256, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 256, 256, 0));
}

TEST(GestureInputPolicyTest, FadeClockWaitsForRequiredToast) {
    EXPECT_FALSE(gestureFrameReadyToFade(false, false, false));
    EXPECT_TRUE(gestureFrameReadyToFade(true, false, false));
    EXPECT_FALSE(gestureFrameReadyToFade(true, true, false));
    EXPECT_TRUE(gestureFrameReadyToFade(true, true, true));
}

TEST(GestureInputPolicyTest, LiveMatchHoldsThroughBriefUnmatchedGap) {
    EXPECT_TRUE(keepLiveGestureMatch(true, false, 0, 120));
    EXPECT_TRUE(keepLiveGestureMatch(false, true, 80, 120));
    EXPECT_FALSE(keepLiveGestureMatch(false, true, 120, 120));
    EXPECT_FALSE(keepLiveGestureMatch(false, false, 0, 120));
}

TEST(GestureInputPolicyTest, TrailOutlineWidthClampsAndWidens) {
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(-2.0f), 0.0f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(2.5f), 2.5f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(99.0f), 8.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(4.0f, 2.5f), 9.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(4.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(0.0f, 2.5f), 0.0f);
}

TEST(GestureInputPolicyTest, FadeClockStartsAfterFirstPresentedFrame) {
    EXPECT_FALSE(gestureFadeShouldFinish(false, 500, 240, 420));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 100, 240, 420));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 240 + 420, 240, 420));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 349, 0, 350));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 350, 0, 350));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 159, 160, 280));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 160 + 279, 160, 280));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 160 + 280, 160, 280));
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 160, 160, 280), 1.0f);
}

TEST(GestureInputPolicyTest, TrailColorFollowsAccentUnlessCustom) {
    const auto cyan = easy::core::getAccentColorRGB("cyan");
    const auto autoRgb = resolveGestureTrailRgb("auto", "#FF0000", cyan);
    EXPECT_FLOAT_EQ(autoRgb.r, cyan.r);
    EXPECT_FLOAT_EQ(autoRgb.g, cyan.g);
    EXPECT_FLOAT_EQ(autoRgb.b, cyan.b);

    const auto customRgb = resolveGestureTrailRgb("custom", "#FF0000", cyan);
    EXPECT_NEAR(customRgb.r, 1.0f, 0.01f);
    EXPECT_NEAR(customRgb.g, 0.0f, 0.01f);
    EXPECT_NEAR(customRgb.b, 0.0f, 0.01f);

    const auto emptyCustom = resolveGestureTrailRgb("custom", "", cyan);
    EXPECT_FLOAT_EQ(emptyCustom.r, cyan.r);
}

TEST(GestureInputPolicyTest, TrailPaletteFollowsThemeSetting) {
    EXPECT_TRUE(gestureTrailUsesLightPalette("light", false));
    EXPECT_FALSE(gestureTrailUsesLightPalette("dark", true));
    EXPECT_TRUE(gestureTrailUsesLightPalette("system", true));
    EXPECT_FALSE(gestureTrailUsesLightPalette("system", false));
}

TEST(GestureInputPolicyTest, FadeAlphaHoldsThenEases) {
    EXPECT_FLOAT_EQ(gestureFadeAlpha(false, 0, 240, 420), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 0, 240, 420), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 240, 240, 420), 1.0f);
    EXPECT_NEAR(gestureFadeAlpha(true, 240 + 210, 240, 420), 0.25f, 0.01f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 240 + 420, 240, 420), 0.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 10, 240, 0), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 241, 240, 0), 0.0f);
    EXPECT_NEAR(gestureFadeAlpha(true, 175, 0, 350), 0.25f, 0.01f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 350, 0, 350), 0.0f);
}

// -----------------------------------------------------------------------------
// 2. 作用域匹配规则测试套件
// -----------------------------------------------------------------------------
TEST(ScopeRuleTest, MatchingAndJsonSerialization) {
    // 进程名精确匹配 (大小写不敏感)
    ScopeRule r1;
    r1.processName = "chrome.exe";
    r1.matchMode = MatchMode::Exact;
    EXPECT_TRUE(r1.matches(nullptr, L"chrome.exe", L"AnyClass"));
    EXPECT_TRUE(r1.matches(nullptr, L"CHROME.EXE", L""));
    EXPECT_FALSE(r1.matches(nullptr, L"firefox.exe", L""));

    // 窗口类名通配符
    ScopeRule r2;
    r2.windowClass = "Chrome_*";
    r2.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(r2.matches(nullptr, L"", L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(r2.matches(nullptr, L"", L"MozillaWindowClass"));

    // 进程名通配符
    ScopeRule r3;
    r3.processName = "*.exe";
    r3.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(r3.matches(nullptr, L"notepad.exe", L""));
    EXPECT_FALSE(r3.matches(nullptr, L"bash", L""));

    // 通配符中的正则元字符必须按普通字符处理
    ScopeRule rSpecial;
    rSpecial.processName = "app[1].exe";
    rSpecial.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(rSpecial.matches(nullptr, L"app[1].exe", L""));
    EXPECT_FALSE(rSpecial.matches(nullptr, L"app1.exe", L""));

    // 禁用的规则永不匹配
    ScopeRule r4;
    r4.processName = "chrome.exe";
    r4.enabled = false;
    EXPECT_FALSE(r4.matches(nullptr, L"chrome.exe", L""));

    // JSON 往返
    ScopeRule r5;
    r5.id = "rule-1";
    r5.name = "Chrome";
    r5.processName = "chrome.exe";
    r5.matchMode = MatchMode::Wildcard;
    r5.effect = RuleEffect::Disable;
    ScopeRule r5b = ScopeRule::fromJson(r5.toJson());
    EXPECT_EQ(r5b.id, "rule-1");
    EXPECT_EQ(r5b.processName, "chrome.exe");
    EXPECT_EQ(r5b.matchMode, MatchMode::Wildcard);
    EXPECT_EQ(r5b.effect, RuleEffect::Disable);

    // 越界枚举输入必须收敛到合法范围
    auto invalid = ScopeRule::fromJson({{"matchMode", 99}, {"effect", -5}});
    EXPECT_EQ(invalid.matchMode, MatchMode::Regex);
    EXPECT_EQ(invalid.effect, RuleEffect::Enable);
}

// -----------------------------------------------------------------------------
// 3. 手势动作与内置命令分发测试套件
// -----------------------------------------------------------------------------
TEST(GestureActionTest, KeyStrokeAndBuiltinCommands) {
    using namespace easy::gesture;

    // 1. KeyStroke 解析与序列化
    auto ks1 = KeyStroke::fromString("Ctrl+Shift+T");
    EXPECT_EQ(ks1.modifiers, (MOD_CONTROL | MOD_SHIFT));
    EXPECT_EQ(ks1.virtualKey, 'T');
    EXPECT_EQ(ks1.toString(), "Ctrl+Shift+T");

    auto ks2 = KeyStroke::fromString("Alt+Left");
    EXPECT_EQ(ks2.modifiers, MOD_ALT);
    EXPECT_EQ(ks2.virtualKey, VK_LEFT);
    EXPECT_EQ(ks2.toString(), "Alt+Left");

    auto ksEmpty = KeyStroke::fromString("");
    EXPECT_EQ(ksEmpty.virtualKey, 0);
    EXPECT_EQ(resolveGestureKeyTarget(nullptr, nullptr, nullptr), nullptr);
    windowFromPointSkippingGestureOverlay(-100000, -100000);
    windowFromPointSkippingGestureOverlay(0, 0);

    WNDCLASSEXW overlayWc{};
    overlayWc.cbSize = sizeof(overlayWc);
    overlayWc.lpfnWndProc = DefWindowProcW;
    overlayWc.hInstance = GetModuleHandleW(nullptr);
    overlayWc.lpszClassName = L"EasyTools_GestureOverlay";
    RegisterClassExW(&overlayWc);
    WNDCLASSEXW settingsWc = overlayWc;
    settingsWc.lpszClassName = L"EasyTools_SettingsWindow";
    RegisterClassExW(&settingsWc);

    HWND overlay = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_GestureOverlay", L"t",
                                   WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    HWND settings = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_SettingsWindow", L"t",
                                    WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    HWND external = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"t",
                                    WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, settings, nullptr), settings);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, settings, external), settings);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, external, settings), external);
    KeyStroke::fromString("").send(nullptr);
    if (overlay) DestroyWindow(overlay);
    if (settings) DestroyWindow(settings);
    if (external && IsWindow(external)) DestroyWindow(external);

    auto pumpPostedClose = [](HWND hwnd) {
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    WNDCLASSEXW acceptWc{};
    acceptWc.cbSize = sizeof(acceptWc);
    acceptWc.lpfnWndProc = DefWindowProcW;
    acceptWc.hInstance = GetModuleHandleW(nullptr);
    acceptWc.lpszClassName = L"EasyTools_TestAcceptClose";
    RegisterClassExW(&acceptWc);
    HWND accept = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_TestAcceptClose", L"accept",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 16, 16, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(accept != nullptr);
    EXPECT_EQ(resolveCloseableWindow(accept), accept);
    pumpPostedClose(accept);
    EXPECT_FALSE(IsWindow(accept));
    EXPECT_FALSE(closeShouldSendKeyFallback(
        true, windowStillAcceptsClose(IsWindow(accept) != FALSE, false)));

    static auto swallowProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_CLOSE || (m == WM_SYSCOMMAND && w == SC_CLOSE)) return 0;
        return DefWindowProcW(h, m, w, l);
    };
    WNDCLASSEXW swallowWc{};
    swallowWc.cbSize = sizeof(swallowWc);
    swallowWc.lpfnWndProc = swallowProc;
    swallowWc.hInstance = GetModuleHandleW(nullptr);
    swallowWc.lpszClassName = L"EasyTools_TestSwallowClose";
    RegisterClassExW(&swallowWc);
    HWND swallow = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_TestSwallowClose", L"swallow",
                                   WS_POPUP | WS_VISIBLE, 0, 0, 16, 16, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(swallow != nullptr);
    pumpPostedClose(swallow);
    EXPECT_TRUE(IsWindow(swallow));
    EXPECT_TRUE(closeShouldSendKeyFallback(
        true, windowStillAcceptsClose(IsWindow(swallow) != FALSE,
                                      IsWindowVisible(swallow) != FALSE)));
    DestroyWindow(swallow);

    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owner",
                                 WS_POPUP, 0, 0, 24, 24, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    HWND owned = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owned",
                                 WS_POPUP, 0, 0, 16, 16, owner, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(owner != nullptr);
    ASSERT_TRUE(owned != nullptr);
    EXPECT_EQ(resolveCloseableWindow(owned), owner);
    if (owned) DestroyWindow(owned);
    if (owner) DestroyWindow(owner);

    EXPECT_TRUE(gestureActionNeedsInputThread(ActionType::SendKeys));
    EXPECT_TRUE(gestureActionNeedsInputThread(ActionType::BuiltinCommand));
    EXPECT_FALSE(gestureActionNeedsInputThread(ActionType::LuaScript));
    EXPECT_FALSE(gestureActionNeedsInputThread(ActionType::RunProgram));

    // 2. GestureAction JSON 往返
    GestureAction a1;
    a1.type = ActionType::SendKeys;
    a1.name = "关闭标签页";
    a1.description = "关闭当前标签";
    a1.keyStroke = KeyStroke::fromString("Ctrl+W");
    auto j1 = a1.toJson();
    auto a1b = GestureAction::fromJson(j1);
    EXPECT_EQ(a1b.name, "关闭标签页");
    EXPECT_EQ(a1b.keyStroke.toString(), "Ctrl+W");

    GestureAction a2;
    a2.type = ActionType::BuiltinCommand;
    a2.name = "下一曲";
    a2.builtinCmd = BuiltinCommand::MediaNext;
    auto j2 = a2.toJson();
    auto a2b = GestureAction::fromJson(j2);
    EXPECT_EQ(a2b.builtinCmd, BuiltinCommand::MediaNext);

    // 3. GestureMapping 字段与 JSON 往返 (包含 id, enabled, instantExecute, silentToast)
    GestureMapping m1;
    m1.id = "custom-m1";
    m1.enabled = true;
    m1.instantExecute = true;
    m1.silentToast = true;
    m1.gestureCode = "D-R";
    m1.action = a1;
    auto jm1 = m1.toJson();
    auto m1b = GestureMapping::fromJson(jm1);
    EXPECT_EQ(m1b.id, "custom-m1");
    EXPECT_TRUE(m1b.enabled);
    EXPECT_TRUE(m1b.instantExecute);
    EXPECT_TRUE(m1b.silentToast);
    EXPECT_EQ(m1b.gestureCode, "D-R");
    EXPECT_EQ(m1b.action.name, "关闭标签页");

    // 4. GestureProfile 三态模型、单项开关与手势调序测试
    GestureProfile prof("test_profile");
    prof.addMapping(m1);

    GestureMapping m2;
    m2.id = "custom-m2";
    m2.gestureCode = "L-U";
    m2.action.name = "剪切";
    prof.addMapping(m2);

    EXPECT_TRUE(prof.findAction("D-R").has_value());
    prof.setMappingEnabled("D-R", false);
    EXPECT_FALSE(prof.findAction("D-R").has_value()); // 禁用后 findAction 返回空
    EXPECT_TRUE(prof.findMapping("D-R").has_value());  // 但 findMapping 仍能查到
    prof.setMappingEnabled("D-R", true);
    EXPECT_TRUE(prof.findAction("D-R").has_value());

    // 调序与移动测试
    EXPECT_TRUE(prof.moveMapping(0, 1));
    EXPECT_EQ(prof.getMappings()[0].gestureCode, "L-U");
    EXPECT_EQ(prof.getMappings()[1].gestureCode, "D-R");
    prof.reorderMappings({"D-R", "L-U"});
    EXPECT_EQ(prof.getMappings()[0].gestureCode, "D-R");
    EXPECT_EQ(prof.getMappings()[1].gestureCode, "L-U");

    // 触发方式三态测试
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Default);
    prof.setTriggerState("right", TriggerModeState::Disabled);
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Disabled);
    prof.setAllTriggerStates(TriggerModeState::Enabled);
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Enabled);
    EXPECT_EQ(prof.getTriggerState("middle"), TriggerModeState::Enabled);

    // Profile JSON 往返测试
    auto profJson = prof.toJson();
    auto profRestored = GestureProfile::fromJson(profJson);
    EXPECT_EQ(profRestored.name(), "test_profile");
    // 5. 默认全局手势与桌面/任务栏预设工厂测试
    auto defaultProf = GestureProfile::createDefaultGlobal();
    EXPECT_EQ(defaultProf.name(), "default");
    EXPECT_GE(defaultProf.getMappings().size(), 20u);
    EXPECT_TRUE(defaultProf.findAction("L").has_value());
    EXPECT_EQ(defaultProf.findAction("L")->name, "后退");
    EXPECT_EQ(defaultProf.findAction("L")->keyStroke.toString(), "Alt+Left");
    EXPECT_TRUE(defaultProf.findAction("R").has_value());
    EXPECT_EQ(defaultProf.findAction("R")->name, "前进");
    EXPECT_EQ(defaultProf.findAction("R")->keyStroke.toString(), "Alt+Right");
    EXPECT_TRUE(defaultProf.findAction("Middle+L").has_value());
    EXPECT_EQ(defaultProf.findAction("Middle+L")->name, "上一曲");
    EXPECT_EQ(defaultProf.findAction("Middle+L")->keyStroke.toString(), "MediaPrev");
    EXPECT_TRUE(defaultProf.findAction("Middle+R").has_value());
    EXPECT_EQ(defaultProf.findAction("Middle+R")->name, "下一曲");
    EXPECT_EQ(defaultProf.findAction("Middle+R")->keyStroke.toString(), "MediaNext");
    EXPECT_TRUE(defaultProf.findAction("R-D").has_value());
    EXPECT_EQ(defaultProf.findAction("R-D")->name, "恢复关闭的标签页");
    EXPECT_EQ(defaultProf.findAction("R-D")->keyStroke.toString(), "Ctrl+Shift+T");
    EXPECT_TRUE(defaultProf.findAction("D-L").has_value());
    EXPECT_EQ(defaultProf.findAction("D-L")->name, "关闭窗口");
    EXPECT_EQ(defaultProf.findAction("D-L")->keyStroke.toString(), "Alt+F4");
    EXPECT_TRUE(defaultProf.findAction("D-R").has_value());
    EXPECT_EQ(defaultProf.findAction("D-R")->name, "关闭标签页");
    EXPECT_EQ(defaultProf.findAction("D-R")->keyStroke.toString(), "Ctrl+W");

    auto desktopProf = GestureProfile::createDesktopProfile();
    EXPECT_EQ(desktopProf.name(), "special_desktop");
    EXPECT_GE(desktopProf.getMappings().size(), 5u);
    EXPECT_TRUE(desktopProf.findAction("U").has_value());

    auto taskbarProf = GestureProfile::createTaskbarProfile();
    EXPECT_EQ(taskbarProf.name(), "special_taskbar");
    EXPECT_GE(taskbarProf.getMappings().size(), 4u);
    EXPECT_TRUE(taskbarProf.findAction("L").has_value());

    // 6. BuiltinCommandDispatcher 应用级回调路由与媒体/虚拟桌面命令
    auto& dispatcher = BuiltinCommandDispatcher::instance();
    std::atomic<bool> screenshotCalled{false};
    dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, [&screenshotCalled]() {
        screenshotCalled = true;
    });

    dispatcher.execute(BuiltinCommand::TakeScreenshot, nullptr);
    EXPECT_TRUE(screenshotCalled.load());

    // 多媒体 / 虚拟桌面 / 锁屏必须先挂 Mock。默认实现是全局 SendInput 或
    // LockWorkStation，部署跑 CTest 时会暂停播放器、静音、切桌面甚至锁屏。
    const std::vector<BuiltinCommand> systemWideCmds = {
        BuiltinCommand::MediaNext,
        BuiltinCommand::MediaPrev,
        BuiltinCommand::MediaPlayPause,
        BuiltinCommand::VolumeUp,
        BuiltinCommand::VolumeDown,
        BuiltinCommand::VolumeMute,
        BuiltinCommand::PrevVirtualDesktop,
        BuiltinCommand::NextVirtualDesktop,
        BuiltinCommand::ShowDesktop,
        BuiltinCommand::SwitchDesktop,
        BuiltinCommand::TaskView,
        BuiltinCommand::LockScreen,
    };
    for (auto cmd : systemWideCmds) {
        std::atomic<bool> called{false};
        dispatcher.registerHandler(cmd, [&called]() { called = true; });
        dispatcher.execute(cmd, nullptr);
        EXPECT_TRUE(called.load());
    }
    dispatcher.clearHandlers();
}

// -----------------------------------------------------------------------------
// 3.1 手势动作扩展键位、别名与外部程序/Lua权限序列化测试套件
// -----------------------------------------------------------------------------
TEST(GestureActionTest, ExtendedKeyStrokeSpecialKeysAndAliases) {
    using namespace easy::gesture;

    // 1. 扩展按键解析与别名覆盖
    auto ksBack = KeyStroke::fromString("Ctrl+Backspace");
    EXPECT_EQ(ksBack.modifiers, MOD_CONTROL);
    EXPECT_EQ(ksBack.virtualKey, VK_BACK);
    EXPECT_EQ(ksBack.toString(), "Ctrl+Backspace");

    auto ksEscAlias = KeyStroke::fromString("Esc");
    EXPECT_EQ(ksEscAlias.virtualKey, VK_ESCAPE);
    EXPECT_EQ(ksEscAlias.toString(), "Escape");

    auto ksHome = KeyStroke::fromString("Home");
    EXPECT_EQ(ksHome.virtualKey, VK_HOME);
    EXPECT_EQ(ksHome.toString(), "Home");

    auto ksEnd = KeyStroke::fromString("End");
    EXPECT_EQ(ksEnd.virtualKey, VK_END);
    EXPECT_EQ(ksEnd.toString(), "End");

    auto ksPgUp = KeyStroke::fromString("PgUp");
    EXPECT_EQ(ksPgUp.virtualKey, VK_PRIOR);
    EXPECT_EQ(ksPgUp.toString(), "PageUp");

    auto ksPgDn = KeyStroke::fromString("PageDown");
    EXPECT_EQ(ksPgDn.virtualKey, VK_NEXT);
    EXPECT_EQ(ksPgDn.toString(), "PageDown");

    auto ksIns = KeyStroke::fromString("Ins");
    EXPECT_EQ(ksIns.virtualKey, VK_INSERT);
    EXPECT_EQ(ksIns.toString(), "Insert");

    auto ksMediaPlay = KeyStroke::fromString("MediaPlay");
    EXPECT_EQ(ksMediaPlay.virtualKey, VK_MEDIA_PLAY_PAUSE);
    EXPECT_EQ(ksMediaPlay.toString(), "MediaPlayPause");

    auto ksVolUp = KeyStroke::fromString("VolumeUp");
    EXPECT_EQ(ksVolUp.virtualKey, VK_VOLUME_UP);
    EXPECT_EQ(ksVolUp.toString(), "VolumeUp");

    // 十六进制虚拟键码
    auto ksHex = KeyStroke::fromString("0x20"); // VK_SPACE
    EXPECT_EQ(ksHex.virtualKey, 0x20);

    // 2. RunProgram 序列化往返
    GestureAction aProg;
    aProg.type = ActionType::RunProgram;
    aProg.name = "打开终端";
    aProg.description = "在当前目录打开 wt.exe";
    aProg.programPath = "wt.exe";
    aProg.programArgs = "-d \"%PATH%\"";
    auto jProg = aProg.toJson();
    auto aProgRestored = GestureAction::fromJson(jProg);
    EXPECT_EQ(aProgRestored.type, ActionType::RunProgram);
    EXPECT_EQ(aProgRestored.name, "打开终端");
    EXPECT_EQ(aProgRestored.programPath, "wt.exe");
    EXPECT_EQ(aProgRestored.programArgs, "-d \"%PATH%\"");

    // 3. LuaScript 与细粒度权限序列化往返
    GestureAction aLua;
    aLua.type = ActionType::LuaScript;
    aLua.name = "翻译选中";
    aLua.luaScript = "easy.clipboard.get()";
    aLua.requestedPermissions = {"clipboard", "http"};
    auto jLua = aLua.toJson();
    auto aLuaRestored = GestureAction::fromJson(jLua);
    EXPECT_EQ(aLuaRestored.type, ActionType::LuaScript);
    EXPECT_EQ(aLuaRestored.name, "翻译选中");
    EXPECT_EQ(aLuaRestored.luaScript, "easy.clipboard.get()");
    EXPECT_EQ(aLuaRestored.requestedPermissions.size(), 2u);
    EXPECT_EQ(aLuaRestored.requestedPermissions[0], "clipboard");
    EXPECT_EQ(aLuaRestored.requestedPermissions[1], "http");

    // 4. BuiltinCommandDispatcher 全枚举分支安全调度覆盖 (注册 Mock Handler 隔离真实系统 API)
    auto& dispatcher = BuiltinCommandDispatcher::instance();
    const std::vector<BuiltinCommand> allCmds = {
        BuiltinCommand::CloseWindow,
        BuiltinCommand::CloseTab,
        BuiltinCommand::MaximizeWindow,
        BuiltinCommand::MinimizeWindow,
        BuiltinCommand::RestoreWindow,
        BuiltinCommand::ShowDesktop,
        BuiltinCommand::SwitchDesktop,
        BuiltinCommand::TaskView,
        BuiltinCommand::LockScreen,
        BuiltinCommand::PauseGestures,
        BuiltinCommand::TakeScreenshot,
        BuiltinCommand::StartRecording,
        BuiltinCommand::RestoreClosedTab,
        BuiltinCommand::ToggleTopmost,
        BuiltinCommand::ToggleWindowTransparency,
        BuiltinCommand::WebSearch,
        BuiltinCommand::ToggleSearch,
        BuiltinCommand::ShowRadialMenu,
        BuiltinCommand::PasteAsPin,
        BuiltinCommand::MediaNext,
        BuiltinCommand::MediaPrev,
        BuiltinCommand::MediaPlayPause,
        BuiltinCommand::VolumeUp,
        BuiltinCommand::VolumeDown,
        BuiltinCommand::VolumeMute,
        BuiltinCommand::PrevVirtualDesktop,
        BuiltinCommand::NextVirtualDesktop
    };

    for (auto cmd : allCmds) {
        std::atomic<bool> called{false};
        dispatcher.registerHandler(cmd, [&called]() { called = true; });
        dispatcher.execute(cmd, nullptr);
        EXPECT_TRUE(called.load());
    }
    dispatcher.clearHandlers();
}

// -----------------------------------------------------------------------------
// 4. 手势配置综合与继承体系测试套件
// -----------------------------------------------------------------------------
TEST(GestureProfileTest, TriStateInheritanceAndOrdering) {
    using namespace easy::gesture;

    GestureProfile profile("test_comp");
    
    // 1. 三态触发模式深度测试
    EXPECT_EQ(profile.getTriggerState("right"), TriggerModeState::Default);
    profile.setTriggerState("right", TriggerModeState::Enabled);
    EXPECT_EQ(profile.getTriggerState("right"), TriggerModeState::Enabled);
    profile.setTriggerState("middle", TriggerModeState::Disabled);
    EXPECT_EQ(profile.getTriggerState("middle"), TriggerModeState::Disabled);
    
    // 全量批处理设置为启用 / 禁用 / 继承
    profile.setAllTriggerStates(TriggerModeState::Enabled);
    for (const auto& [k, v] : profile.getAllTriggerStates()) {
        EXPECT_EQ(v, TriggerModeState::Enabled);
    }
    profile.setAllTriggerStates(TriggerModeState::Default);
    for (const auto& [k, v] : profile.getAllTriggerStates()) {
        EXPECT_EQ(v, TriggerModeState::Default);
    }

    // 2. 手势映射增删查改与属性测试
    GestureMapping m1;
    m1.id = "m_left";
    m1.gestureCode = "L";
    m1.enabled = true;
    m1.instantExecute = true;
    m1.silentToast = false;
    m1.action.type = ActionType::BuiltinCommand;
    m1.action.builtinCmd = BuiltinCommand::CloseTab;
    profile.addMapping(m1);

    GestureMapping m2;
    m2.id = "m_right";
    m2.gestureCode = "R";
    m2.enabled = false;
    m2.instantExecute = false;
    m2.silentToast = true;
    m2.action.type = ActionType::BuiltinCommand;
    m2.action.builtinCmd = BuiltinCommand::RestoreClosedTab;
    profile.addMapping(m2);

    GestureMapping m3;
    m3.id = "m_lu";
    m3.gestureCode = "L-U";
    m3.enabled = true;
    m3.action.type = ActionType::BuiltinCommand;
    m3.action.builtinCmd = BuiltinCommand::MaximizeWindow;
    profile.addMapping(m3);

    EXPECT_EQ(profile.getMappings().size(), 3u);
    EXPECT_TRUE(profile.hasGesture("L"));
    EXPECT_TRUE(profile.hasGesture("R"));
    EXPECT_TRUE(profile.hasGesture("L-U"));
    EXPECT_FALSE(profile.hasGesture("D"));

    // 3. 前缀冲突检测
    auto conflicts = profile.detectConflicts("L");
    EXPECT_FALSE(conflicts.empty());
    bool foundLu = false;
    for (const auto& c : conflicts) {
        if (c == "L-U") foundLu = true;
    }
    EXPECT_TRUE(foundLu);

    // 4. 映射调序测试 (moveMapping)
    EXPECT_TRUE(profile.moveMapping(0, 2));
    EXPECT_EQ(profile.getMappings()[0].gestureCode, "R");
    EXPECT_EQ(profile.getMappings()[2].gestureCode, "L");
    EXPECT_FALSE(profile.moveMapping(0, 999)); // 越界安全
    EXPECT_FALSE(profile.moveMapping(999, 0)); // 越界安全
    EXPECT_FALSE(profile.moveMapping(1, 1));   // 同位置无位移，返回 false

    // 5. 批量重排序 (reorderMappings)
    profile.reorderMappings({"L-U", "L", "R"});
    EXPECT_EQ(profile.getMappings()[0].gestureCode, "L-U");
    EXPECT_EQ(profile.getMappings()[1].gestureCode, "L");
    EXPECT_EQ(profile.getMappings()[2].gestureCode, "R");

    // 6. 单项启用/禁用切换
    profile.setMappingEnabled("L", false);
    auto optDisabled = profile.findAction("L");
    EXPECT_FALSE(optDisabled.has_value());
    profile.setMappingEnabled("L", true);
    auto optEnabled = profile.findAction("L");
    EXPECT_TRUE(optEnabled.has_value());

    // 7. JSON 序列化与反序列化全属性往返
    auto j = profile.toJson();
    auto restored = GestureProfile::fromJson(j);
    EXPECT_EQ(restored.name(), "test_comp");
    EXPECT_EQ(restored.getMappings().size(), 3u);
    EXPECT_EQ(restored.getMappings()[0].gestureCode, "L-U");
    EXPECT_FALSE(restored.getMappings()[0].instantExecute);
    EXPECT_TRUE(restored.getMappings()[1].instantExecute);
    EXPECT_TRUE(restored.getMappings()[2].silentToast);
}

// -----------------------------------------------------------------------------
// 5. 特殊与系统窗口检测测试套件
// -----------------------------------------------------------------------------

TEST(HotCornerEngineTest, DetectionAndConfiguration) {
    using namespace easy::gesture;
    auto& engine = HotCornerEngine::instance();
    engine.setEnabled(false);
    EXPECT_FALSE(engine.isEnabled());
    engine.setEnabled(true);
    EXPECT_TRUE(engine.isEnabled());

    engine.setTriggerDelay(150);
    EXPECT_EQ(engine.triggerDelay(), 150);

    engine.setAutoBypassFullscreen(false);
    EXPECT_FALSE(engine.autoBypassFullscreen());
    engine.setAutoBypassFullscreen(true);
    EXPECT_TRUE(engine.autoBypassFullscreen());

    engine.setCornerAction(HotCorner::TopLeft, "app:taskview");
    EXPECT_EQ(engine.getCornerAction(HotCorner::TopLeft), "app:taskview");
    engine.setCornerAction(HotCorner::BottomRight, "app:desktop");
    EXPECT_EQ(engine.getCornerAction(HotCorner::BottomRight), "app:desktop");
    EXPECT_EQ(engine.getCornerAction(HotCorner::None), "");

    // 虚拟屏幕四角几何检测
    int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vRight = vLeft + vWidth - 1;
    int vBottom = vTop + vHeight - 1;

    EXPECT_EQ(engine.detectCorner({vLeft, vTop}), HotCorner::TopLeft);
    EXPECT_EQ(engine.detectCorner({vRight, vTop}), HotCorner::TopRight);
    EXPECT_EQ(engine.detectCorner({vLeft, vBottom}), HotCorner::BottomLeft);
    EXPECT_EQ(engine.detectCorner({vRight, vBottom}), HotCorner::BottomRight);
    EXPECT_EQ(engine.detectCorner({vLeft + vWidth / 2, vTop + vHeight / 2}), HotCorner::None);

    engine.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    engine.stop();
}

// -----------------------------------------------------------------------------
// 33. 数据统计管理器测试套件
// -----------------------------------------------------------------------------

TEST(GestureInputPolicyTest, OverlayWindowNeverCreatesTaskbarEntry) {
    const LONG_PTR normalized = normalizeGestureOverlayExStyle(
        WS_EX_APPWINDOW | WS_EX_CLIENTEDGE);
    EXPECT_TRUE(gestureOverlayIsTaskbarSafe(normalized));
    EXPECT_EQ(normalized & WS_EX_APPWINDOW, 0);
    EXPECT_NE(normalized & WS_EX_TOOLWINDOW, 0);
    EXPECT_NE(normalized & WS_EX_NOACTIVATE, 0);
    EXPECT_NE(normalized & WS_EX_LAYERED, 0);
    EXPECT_NE(normalized & WS_EX_TRANSPARENT, 0);
    EXPECT_NE(normalized & WS_EX_TOPMOST, 0);
    EXPECT_NE(normalized & WS_EX_CLIENTEDGE, 0);
    EXPECT_FALSE(gestureOverlayIsTaskbarSafe(WS_EX_APPWINDOW));
}


TEST(MouseGestureLifecycleTest, TriggerAndReleasePairingLifecycle) {
    using namespace easy::gesture;

    // 1. 验证各种模式下触发键判定
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::RightOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::RightOnly));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::Both));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::All));

    // 2. 验证按下与抬起事件生命周期严格成对
    EXPECT_EQ(triggerUpFor(MouseEventType::RightDown), MouseEventType::RightUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::MiddleDown), MouseEventType::MiddleUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::X1Down), MouseEventType::X1Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::X2Down), MouseEventType::X2Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::LeftDown), MouseEventType::LeftUp);
}

TEST(MouseGestureLifecycleTest, CancellationAndInterruptionLifecycle) {
    using namespace easy::gesture;

    // 1. 在右键手势追踪过程中，按下左键必须立即取消手势并放行
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftUp, MouseEventType::RightDown));

    // 2. 按下其他未激活的鼠标按键（如中键/侧键）也必须立即取消手势
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::MiddleDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::X1Down, MouseEventType::RightDown));

    // 3. 当前触发键自身的高频事件（噪声）不触发取消，由状态机平滑处理
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::RightDown, MouseEventType::RightDown));
}

TEST(MouseGestureLifecycleTest, ViewportExpansionAndTaskbarSafety) {
    using namespace easy::gesture;

    // 1. 验证手势轨迹视口只扩大、不收缩、不挪原点，避免微型移动时频繁重建 DIB
    int left = 100, top = 100, right = 200, bottom = 200;
    growOverlayRect(left, top, right, bottom, 50, 50, 100, 100);
    EXPECT_EQ(left, 50);
    EXPECT_EQ(top, 50);
    EXPECT_EQ(right, 200);
    EXPECT_EQ(bottom, 200);

    // 2. 验证窗口扩展样式安全性：必须包含 WS_EX_TOOLWINDOW、WS_EX_NOACTIVATE 并剥离 WS_EX_APPWINDOW
    LONG_PTR rawStyle = WS_EX_APPWINDOW;
    LONG_PTR normalized = normalizeGestureOverlayExStyle(rawStyle);
    EXPECT_TRUE(gestureOverlayIsTaskbarSafe(normalized));
    EXPECT_EQ(normalized & WS_EX_APPWINDOW, 0);
}

TEST(MouseGestureLifecycleTest, FadeOutClockAndAlphaDecayLifecycle) {
    using namespace easy::gesture;

    // 1. 验证在首帧未就绪前，淡出时钟严禁提前走完
    EXPECT_FALSE(gestureFrameReadyToFade(false, true, false));
    EXPECT_TRUE(gestureFrameReadyToFade(true, true, true));
    EXPECT_TRUE(gestureFrameReadyToFade(true, false, false));

    // 2. 验证淡出时钟生命周期
    DWORD holdMs = 80;
    DWORD fadeMs = 120;

    // 刚松手 50ms（处于 Hold 驻留期）：Alpha 保持 1.0
    EXPECT_FALSE(gestureFadeShouldFinish(true, 50, holdMs, fadeMs));
    EXPECT_NEAR(gestureFadeAlpha(true, 50, holdMs, fadeMs), 1.0f, 0.001f);

    // 处于淡出期 140ms（80ms + 60ms，即 50% 进度）：Alpha 平滑衰减
    EXPECT_FALSE(gestureFadeShouldFinish(true, 140, holdMs, fadeMs));
    EXPECT_LT(gestureFadeAlpha(true, 140, holdMs, fadeMs), 1.0f);
    EXPECT_GT(gestureFadeAlpha(true, 140, holdMs, fadeMs), 0.0f);

    // 超过总时长 210ms（80ms + 120ms）：淡出彻底完成，Alpha 归零，销毁渲染管线
    EXPECT_TRUE(gestureFadeShouldFinish(true, 210, holdMs, fadeMs));
    EXPECT_NEAR(gestureFadeAlpha(true, 210, holdMs, fadeMs), 0.0f, 0.001f);
}

// -----------------------------------------------------------------------------
// 29. 按键回显全生命周期、高频连击合并与静默释放测试套件
// -----------------------------------------------------------------------------