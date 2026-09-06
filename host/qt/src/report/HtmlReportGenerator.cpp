#include "report/HtmlReportGenerator.h"

#include "testing/TestCaseTypes.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace oms555tv::report {
namespace {

QString sanitizeText(QString value)
{
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QChar(u'\r'), QChar(u'\n'));
    value.replace(QChar(u'\t'), QStringLiteral("    "));
    for (qsizetype index = 0; index < value.size(); ++index) {
        const ushort code = value.at(index).unicode();
        if ((code < 0x20 && code != 0x0A) || (code >= 0x7F && code <= 0x9F)) {
            value[index] = QChar(0xFFFD);
        }
    }
    return value;
}

QString escapeHtml(QString value)
{
    value = sanitizeText(std::move(value));
    value.replace(QChar(u'&'), QStringLiteral("&amp;"));
    value.replace(QChar(u'<'), QStringLiteral("&lt;"));
    value.replace(QChar(u'>'), QStringLiteral("&gt;"));
    value.replace(QChar(u'\"'), QStringLiteral("&quot;"));
    value.replace(QChar(u'\''), QStringLiteral("&#39;"));
    return value;
}

QString shown(const QString &value, const QString &empty = QStringLiteral("无"))
{
    return escapeHtml(value.isEmpty() ? empty : value);
}

QString optionalText(const std::optional<QString> &value,
                     const QString &missing = QStringLiteral("未采集"))
{
    return value ? shown(*value) : escapeHtml(missing);
}

QString statusName(const testing::TestStatus status)
{
    return testing::testStatusName(status);
}

QString statusClass(const testing::TestStatus status)
{
    switch (status) {
    case testing::TestStatus::Pass: return QStringLiteral("pass");
    case testing::TestStatus::Fail: return QStringLiteral("fail");
    case testing::TestStatus::Error: return QStringLiteral("error");
    case testing::TestStatus::Skipped: return QStringLiteral("skipped");
    case testing::TestStatus::NotRun:
    case testing::TestStatus::Running: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QString statusBadge(const testing::TestStatus status)
{
    return QStringLiteral("<span class=\"status %1\">%2</span>")
        .arg(statusClass(status), escapeHtml(statusName(status)));
}

QString durationText(const std::chrono::milliseconds value)
{
    return QStringLiteral("%1 ms").arg(value.count());
}

QString timestampText(const std::optional<ReportTimestamp> &value)
{
    if (!value) {
        return QStringLiteral("未采集");
    }
    return escapeHtml(QStringLiteral("%1（本地 %2，时区 %3）")
                          .arg(value->utcIso, value->localIso,
                               QString::fromUtf8(value->timeZoneId)));
}

QString row(const QString &label, const QString &safeValue)
{
    return QStringLiteral("<tr><th scope=\"row\">%1</th><td class=\"text\">%2</td></tr>")
        .arg(escapeHtml(label), safeValue);
}

QString numberList(const QVector<qint64> &values)
{
    if (values.isEmpty()) {
        return QStringLiteral("无");
    }
    QStringList output;
    output.reserve(values.size());
    for (const qint64 value : values) {
        output.append(QString::number(value));
    }
    return escapeHtml(output.join(QStringLiteral(", ")));
}

QString unsignedList(const QVector<quint64> &values)
{
    if (values.isEmpty()) {
        return QStringLiteral("无");
    }
    QStringList output;
    output.reserve(values.size());
    for (const quint64 value : values) {
        output.append(QString::number(value));
    }
    return escapeHtml(output.join(QStringLiteral(", ")));
}

QString rawList(const QVector<quint16> &values)
{
    if (values.isEmpty()) {
        return QStringLiteral("无");
    }
    QStringList output;
    output.reserve(values.size());
    for (const quint16 value : values) {
        output.append(QString::number(value));
    }
    return escapeHtml(output.join(QStringLiteral(", ")));
}

QString tagsText(const QStringList &values)
{
    return values.isEmpty() ? QStringLiteral("无") : escapeHtml(values.join(QStringLiteral(", ")));
}

QString requestText(const std::optional<ReportRequestDefinition> &request)
{
    if (!request) {
        return QStringLiteral("未配置");
    }
    return escapeHtml(QStringLiteral("FC=0x%1，地址=%2，数量=%3，原始写入值=%4，写前读取=%5，恢复原值=%6")
                          .arg(request->functionCode, 2, 16, QLatin1Char('0'))
                          .arg(request->address)
                          .arg(request->count)
                          .arg(request->rawValue)
                          .arg(request->readBeforeWrite ? QStringLiteral("是") : QStringLiteral("否"))
                          .arg(request->restoreOriginal ? QStringLiteral("是") : QStringLiteral("否"))
                          .toUpper());
}

QString errorBlock(const std::optional<ReportExecutionError> &error,
                   const QString &empty = QStringLiteral("无"))
{
    if (!error) {
        return escapeHtml(empty);
    }
    QString output = QStringLiteral("<div class=\"error-box text\"><strong>%1</strong>：%2")
                         .arg(shown(error->code), shown(error->diagnostic));
    if (error->communicationCategory) {
        output += QStringLiteral("<br>通信类别：%1").arg(shown(*error->communicationCategory));
    }
    if (error->communicationCode) {
        output += QStringLiteral("<br>通信错误码：%1").arg(shown(*error->communicationCode));
    }
    if (error->exceptionCode) {
        const QString exceptionHex = QStringLiteral("%1")
            .arg(*error->exceptionCode, 2, 16, QLatin1Char('0')).toUpper();
        output += QStringLiteral("<br>Modbus 异常码：0x%1")
                      .arg(exceptionHex);
    }
    output += QStringLiteral("</div>");
    return output;
}

QString frameText(const ReportFrame &frame)
{
    switch (frame.presence) {
    case ReportFramePresence::Missing: return QStringLiteral("未保留");
    case ReportFramePresence::Empty: return QStringLiteral("空帧（0 字节）");
    case ReportFramePresence::Value: return shown(frame.uppercaseHex);
    }
    return QStringLiteral("未保留");
}

QString renderAssertion(const ReportAssertion &assertion)
{
    QString output = QStringLiteral("<details><summary>断言与结构化差异</summary><table><tbody>");
    output += row(QStringLiteral("断言类型"), shown(assertion.type));
    output += row(QStringLiteral("断言状态"), statusBadge(assertion.status));
    output += row(QStringLiteral("预期摘要"), shown(assertion.expectedSummary));
    output += row(QStringLiteral("实际摘要"), shown(assertion.actualSummary));
    output += row(QStringLiteral("单位"), shown(assertion.unit));
    output += row(QStringLiteral("预期值"), numberList(assertion.expectedValues));
    output += row(QStringLiteral("实际值"), numberList(assertion.actualValues));
    output += row(QStringLiteral("实际原始寄存器"), rawList(assertion.actualRawValues));
    output += row(QStringLiteral("预期异常码"), assertion.expectedExceptionCode
        ? QStringLiteral("0x%1").arg(*assertion.expectedExceptionCode, 2, 16, QLatin1Char('0')).toUpper()
        : QStringLiteral("未配置"));
    output += row(QStringLiteral("实际异常码"), assertion.actualExceptionCode
        ? QStringLiteral("0x%1").arg(*assertion.actualExceptionCode, 2, 16, QLatin1Char('0')).toUpper()
        : QStringLiteral("未采集"));
    output += QStringLiteral("</tbody></table>");

    if (!assertion.expectedElements.isEmpty()) {
        output += QStringLiteral("<h5>逐元素预期</h5><table><thead><tr><th>索引</th><th>类型</th><th>表示</th><th>值/范围</th><th>掩码</th><th>小数位</th><th>单位</th></tr></thead><tbody>");
        for (const auto &element : assertion.expectedElements) {
            const QString maskHex = QStringLiteral("%1")
                .arg(element.mask, 4, 16, QLatin1Char('0')).toUpper();
            output += QStringLiteral("<tr><td>%1</td><td class=\"text\">%2</td><td class=\"text\">%3</td><td>%4 / %5..%6</td><td>0x%7</td><td>%8</td><td class=\"text\">%9</td></tr>")
                          .arg(QString::number(element.index), shown(element.type),
                               shown(element.representation), QString::number(element.value),
                               QString::number(element.minimum), QString::number(element.maximum),
                               maskHex, QString::number(element.decimalPlaces), shown(element.unit));
        }
        output += QStringLiteral("</tbody></table>");
    }
    if (assertion.expectedUInt32) {
        const auto &value = *assertion.expectedUInt32;
        output += QStringLiteral("<h5>uint32 预期</h5><p class=\"text\">字序 %1；比较 %2；范围 %3..%4；小数位 %5；单位 %6</p>")
                      .arg(shown(value.wordOrder), shown(value.comparison),
                           QString::number(value.minimum), QString::number(value.maximum),
                           QString::number(value.decimalPlaces), shown(value.unit));
    }
    if (assertion.expectedStability) {
        const auto &value = *assertion.expectedStability;
        output += QStringLiteral("<h5>稳定性预期</h5><p class=\"text\">最少总数 %1；最少成功 %2；最多失败 %3；最多超时 %4；最大失败率 %5 ppm；要求有效 RTT %6；RTT 范围/平均/最大 %7/%8/%9 ms</p>")
                      .arg(value.minimumTotal).arg(value.minimumSuccesses)
                      .arg(value.maximumFailures).arg(value.maximumTimeouts)
                      .arg(value.maximumFailureRatePpm)
                      .arg(value.requireValidRttSamples ? QStringLiteral("是") : QStringLiteral("否"))
                      .arg(value.minimumRttMs).arg(value.maximumAverageRttMs).arg(value.maximumRttMs);
    }
    if (!assertion.actualScaledValues.isEmpty()) {
        QStringList values;
        for (const auto &value : assertion.actualScaledValues) {
            values.append(QStringLiteral("%1/%2").arg(value.numerator).arg(value.denominator));
        }
        output += QStringLiteral("<h5>缩放实际值</h5><p class=\"text\">%1</p>")
                      .arg(escapeHtml(values.join(QStringLiteral(", "))));
    }
    if (!assertion.actualRawSamples.isEmpty()) {
        output += QStringLiteral("<h5>一致性原始样本</h5><ol>");
        for (const auto &sample : assertion.actualRawSamples) {
            output += QStringLiteral("<li class=\"text\">%1</li>").arg(rawList(sample));
        }
        output += QStringLiteral("</ol>");
    }
    if (!assertion.differences.isEmpty()) {
        output += QStringLiteral("<h5>断言差异</h5><table><thead><tr><th>代码</th><th>索引</th><th>预期</th><th>实际</th><th>原始值</th><th>缩放分母</th><th>单位</th><th>原因</th></tr></thead><tbody>");
        for (const auto &difference : assertion.differences) {
            QString expected = QStringLiteral("未采集");
            if (difference.expected) {
                expected = QString::number(*difference.expected);
            } else if (difference.expectedMinimum && difference.expectedMaximum) {
                expected = QStringLiteral("%1..%2").arg(*difference.expectedMinimum).arg(*difference.expectedMaximum);
            }
            output += QStringLiteral("<tr><td class=\"text\">%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td class=\"text\">%7</td><td class=\"text\">%8</td></tr>")
                          .arg(shown(difference.code),
                               difference.index ? QString::number(*difference.index) : QStringLiteral("未采集"),
                               escapeHtml(expected),
                               difference.actual ? QString::number(*difference.actual) : QStringLiteral("未采集"),
                               difference.actualRaw ? QString::number(*difference.actualRaw) : QStringLiteral("未采集"),
                               difference.scaledDenominator ? QString::number(*difference.scaledDenominator) : QStringLiteral("未采集"),
                               shown(difference.unit), shown(difference.reason));
        }
        output += QStringLiteral("</tbody></table>");
    }
    output += QStringLiteral("</details>");
    return output;
}

QString renderAttempt(const ReportTransactionEvidence &attempt)
{
    QString output = QStringLiteral("<details class=\"attempt\"><summary>Attempt #%1 · RequestId %2 · %3</summary><table><tbody>")
                         .arg(attempt.attemptSequence).arg(attempt.requestId)
                         .arg(shown(attempt.requestState));
    output += row(QStringLiteral("用途"), shown(attempt.purpose));
    output += row(QStringLiteral("步骤关联"), escapeHtml(
        QStringLiteral("step=") + (attempt.logicalStepId.isEmpty() ? QStringLiteral("无") : attempt.logicalStepId)
        + QStringLiteral("，index=") + QString::number(attempt.logicalStepIndex)
        + QStringLiteral("，repetition=") + QString::number(attempt.repetition)
        + QStringLiteral("，step attempt=") + QString::number(attempt.stepAttempt)));
    output += row(QStringLiteral("重试"), escapeHtml(QStringLiteral("%1；原因：%2")
        .arg(attempt.retry ? QStringLiteral("是") : QStringLiteral("否"),
             attempt.retryReason.isEmpty() ? QStringLiteral("无") : attempt.retryReason)));
    const QString functionHex = QStringLiteral("%1")
        .arg(attempt.functionCode, 2, 16, QLatin1Char('0')).toUpper();
    output += row(QStringLiteral("请求"), escapeHtml(
        QStringLiteral("Slave=") + QString::number(attempt.serverAddress)
        + QStringLiteral("，FC=0x") + functionHex
        + QStringLiteral("，类型=") + attempt.descriptorKind
        + QStringLiteral("，地址=") + QString::number(attempt.address)
        + QStringLiteral("，数量=") + (attempt.count ? QString::number(*attempt.count) : QStringLiteral("未配置"))
        + QStringLiteral("，原始值=") + (attempt.rawValue ? QString::number(*attempt.rawValue) : QStringLiteral("未配置"))));
    output += row(QStringLiteral("开始"), timestampText(attempt.started));
    output += row(QStringLiteral("结束"), timestampText(attempt.finished));
    output += row(QStringLiteral("耗时"), escapeHtml(durationText(attempt.duration)));
    output += row(QStringLiteral("TX"), frameText(attempt.tx));
    output += row(QStringLiteral("TX CRC"), shown(attempt.txCrcStatus));
    output += row(QStringLiteral("RX"), frameText(attempt.rx));
    output += row(QStringLiteral("RX CRC"), shown(attempt.rxCrcStatus));
    output += row(QStringLiteral("RTT"), attempt.rttMilliseconds && attempt.rttNanoseconds
        ? escapeHtml(QStringLiteral("%1 ms（%2 ns）").arg(*attempt.rttMilliseconds).arg(*attempt.rttNanoseconds))
        : QStringLiteral("未采集"));
    output += row(QStringLiteral("通信错误"), errorBlock(attempt.error));
    output += QStringLiteral("</tbody></table></details>");
    return output;
}

QString renderStep(const ReportCompositeStep &step)
{
    QString output = QStringLiteral("<section class=\"subsection\"><h5>Step %1（index %2，repetition %3）%4</h5><table><tbody>")
                         .arg(shown(step.stepId), QString::number(step.stepIndex),
                              QString::number(step.repetition), statusBadge(step.status));
    output += row(QStringLiteral("类型"), escapeHtml(QStringLiteral("declared=%1，normalized=%2")
        .arg(step.declaredType, step.normalizedType)));
    output += row(QStringLiteral("执行前延迟"), escapeHtml(durationText(step.delayBefore)));
    output += row(QStringLiteral("请求"), requestText(step.request));
    output += row(QStringLiteral("开始/结束"), timestampText(step.started) + QStringLiteral("<br>") + timestampText(step.finished));
    output += row(QStringLiteral("耗时"), escapeHtml(durationText(step.duration)));
    output += row(QStringLiteral("Expected"), shown(step.assertion.expectedSummary));
    output += row(QStringLiteral("Actual"), shown(step.assertion.actualSummary));
    output += row(QStringLiteral("错误"), errorBlock(step.error));
    output += row(QStringLiteral("Attempt sequences"), unsignedList(step.attemptSequences));
    output += QStringLiteral("</tbody></table>%1</section>").arg(renderAssertion(step.assertion));
    return output;
}

QString renderStability(const ReportStabilityStatistics &value)
{
    QString output = QStringLiteral("<section class=\"subsection\"><h5>稳定性聚合统计</h5><table><tbody>");
    output += row(QStringLiteral("总数 / 成功 / 失败 / 超时"),
                  escapeHtml(QStringLiteral("%1 / %2 / %3 / %4")
                      .arg(value.total).arg(value.successes).arg(value.failures).arg(value.timeouts)));
    output += row(QStringLiteral("有效 / 缺失 RTT 样本"),
                  escapeHtml(QStringLiteral("%1 / %2")
                      .arg(value.validRttSamples).arg(value.missingRttSamples)));
    output += row(QStringLiteral("最小 / 平均 / 最大 RTT"),
                  escapeHtml(QStringLiteral("%1 / %2 / %3 ms")
                      .arg(value.minimumRttMs ? QString::number(*value.minimumRttMs) : QStringLiteral("未采集"))
                      .arg(value.averageRttMs ? QString::number(*value.averageRttMs) : QStringLiteral("未采集"))
                      .arg(value.maximumRttMs ? QString::number(*value.maximumRttMs) : QStringLiteral("未采集"))));
    output += QStringLiteral("</tbody></table></section>");
    return output;
}

QString renderGuided(const ReportGuidedRecovery &guided)
{
    QString output = QStringLiteral("<section class=\"subsection\"><h5>人工引导恢复</h5><table><tbody>");
    output += row(QStringLiteral("最终状态"), shown(guided.finalState));
    output += row(QStringLiteral("终态原因"), shown(guided.terminalReason));
    output += row(QStringLiteral("首个成功响应耗时"), guided.toFirstSuccessfulResponse
        ? escapeHtml(durationText(*guided.toFirstSuccessfulResponse)) : QStringLiteral("未采集"));
    output += row(QStringLiteral("稳定恢复耗时"), guided.toStableRecovery
        ? escapeHtml(durationText(*guided.toStableRecovery)) : QStringLiteral("未采集"));
    output += row(QStringLiteral("物理链路已恢复"), guided.physicalLinkRestored ? QStringLiteral("是") : QStringLiteral("否"));
    output += row(QStringLiteral("仍需恢复指引"), guided.recoveryInstructionRequired
        ? shown(guided.recoveryInstruction) : QStringLiteral("否"));
    output += QStringLiteral("</tbody></table>");
    for (const auto &action : guided.operatorActions) {
        output += QStringLiteral("<details><summary>人工 Prompt：%1 / %2</summary><table><tbody>")
                      .arg(shown(action.stepId), shown(action.promptPurpose));
        output += row(QStringLiteral("关联"), escapeHtml(
            QStringLiteral("run=") + QString::number(action.runId)
            + QStringLiteral("，case=") + action.caseId
            + QStringLiteral("，step=") + action.stepId
            + QStringLiteral("，token=") + action.oneTimeToken));
        output += row(QStringLiteral("标题"), shown(action.title));
        output += row(QStringLiteral("操作说明"), shown(action.instruction));
        output += row(QStringLiteral("安全提示"), shown(action.safetyNotice));
        output += row(QStringLiteral("允许动作"), tagsText(action.allowedActions));
        output += row(QStringLiteral("取消恢复指引"), shown(action.cancelRecoveryInstruction));
        output += row(QStringLiteral("实际动作"), optionalText(action.action, QStringLiteral("未操作")));
        output += row(QStringLiteral("Prompt/动作时间"), timestampText(action.promptShown) + QStringLiteral("<br>") + timestampText(action.actionTime));
        output += row(QStringLiteral("等待耗时"), escapeHtml(durationText(action.waitDuration)));
        output += row(QStringLiteral("备注"), shown(action.note));
        output += QStringLiteral("</tbody></table></details>");
    }
    for (const auto &observation : guided.observations) {
        output += QStringLiteral("<details><summary>自动观察：%1 / %2</summary><table><tbody>")
                      .arg(shown(observation.stepId), shown(observation.outcome));
        output += row(QStringLiteral("关联"), escapeHtml(
            QStringLiteral("run=") + QString::number(observation.runId)
            + QStringLiteral("，case=") + observation.caseId
            + QStringLiteral("，step=") + observation.stepId));
        output += row(QStringLiteral("目标"), shown(observation.target));
        output += row(QStringLiteral("探测请求"), requestText(observation.probe));
        output += row(QStringLiteral("间隔 / deadline"), escapeHtml(QStringLiteral("%1 / %2")
            .arg(durationText(observation.interval), durationText(observation.deadline))));
        output += row(QStringLiteral("业务预期"), optionalText(observation.businessExpectedSummary, QStringLiteral("未配置")));
        output += row(QStringLiteral("开始/结束"), timestampText(observation.started) + QStringLiteral("<br>") + timestampText(observation.finished));
        output += row(QStringLiteral("耗时"), escapeHtml(durationText(observation.duration)));
        output += row(QStringLiteral("连续匹配"), escapeHtml(QStringLiteral("要求 %1，达到 %2")
            .arg(observation.requiredConsecutiveMatches).arg(observation.achievedConsecutiveMatches)));
        output += row(QStringLiteral("首个 / 稳定 RequestId"), escapeHtml(QStringLiteral("%1 / %2")
            .arg(observation.firstMatchingRequestId ? QString::number(*observation.firstMatchingRequestId) : QStringLiteral("未采集"))
            .arg(observation.stableMatchingRequestId ? QString::number(*observation.stableMatchingRequestId) : QStringLiteral("未采集"))));
        output += row(QStringLiteral("Attempt sequences"), unsignedList(observation.attemptSequences));
        output += row(QStringLiteral("观察错误"), errorBlock(observation.error));
        output += QStringLiteral("</tbody></table></details>");
    }
    output += QStringLiteral("</section>");
    return output;
}

QString renderEvidenceRetention(const ReportEvidenceRetention &value)
{
    const QString boundary = value.droppedAttempts > 0
        ? QStringLiteral("有界代表证据：本报告仅含内存保留 attempt；完整事务需查阅 SessionLog。")
        : QStringLiteral("完整内存证据：本次所有 attempt 均已保留在报告模型中。");
    QString output = QStringLiteral("<section class=\"evidence-note\"><h5>证据保留摘要</h5><p class=\"text\"><strong>%1</strong></p><table><tbody>")
                         .arg(escapeHtml(boundary));
    output += row(QStringLiteral("策略"), shown(value.policy));
    output += row(QStringLiteral("总数 / 保留 / 丢弃"), escapeHtml(QStringLiteral("%1 / %2 / %3")
        .arg(value.totalAttempts).arg(value.retainedAttempts).arg(value.droppedAttempts)));
    output += row(QStringLiteral("失败总数 / 保留失败"), escapeHtml(QStringLiteral("%1 / %2")
        .arg(value.totalFailures).arg(value.retainedFailures)));
    output += row(QStringLiteral("配置上限"), QString::number(value.configuredLimit));
    output += row(QStringLiteral("说明"), shown(value.description));
    output += QStringLiteral("</tbody></table></section>");
    return output;
}

QString renderCase(const ReportCase &testCase)
{
    QString output = QStringLiteral("<article class=\"case\"><h3>%1 · %2 %3</h3><table><tbody>")
                         .arg(shown(testCase.id), shown(testCase.name), statusBadge(testCase.status));
    output += row(QStringLiteral("类别"), shown(testCase.category));
    output += row(QStringLiteral("类型"), escapeHtml(QStringLiteral("declared=%1，normalized=%2")
        .arg(testCase.declaredType, testCase.normalizedType)));
    output += row(QStringLiteral("执行环境/前置"), shown(testCase.environment));
    output += row(QStringLiteral("描述"), shown(testCase.description));
    output += row(QStringLiteral("Tags"), tagsText(testCase.tags));
    output += row(QStringLiteral("Session ID"), shown(testCase.sessionId, QStringLiteral("未采集")));
    output += row(QStringLiteral("跳过原因"), optionalText(testCase.skipReason, QStringLiteral("不适用")));
    output += row(QStringLiteral("开始/结束"), timestampText(testCase.started) + QStringLiteral("<br>") + timestampText(testCase.finished));
    output += row(QStringLiteral("耗时"), escapeHtml(durationText(testCase.duration)));
    output += row(QStringLiteral("请求定义"), requestText(testCase.request));
    output += row(QStringLiteral("Expected"), shown(testCase.assertion.expectedSummary));
    output += row(QStringLiteral("Actual"), shown(testCase.assertion.actualSummary));
    output += row(QStringLiteral("失败/执行错误"), errorBlock(testCase.error));
    output += row(QStringLiteral("清理错误"), errorBlock(testCase.cleanupError));
    output += QStringLiteral("</tbody></table>");
    output += renderAssertion(testCase.assertion);
    if (!testCase.steps.isEmpty()) {
        output += QStringLiteral("<section class=\"subsection\"><h4>Sequence 步骤</h4>");
        for (const auto &step : testCase.steps) {
            output += renderStep(step);
        }
        output += QStringLiteral("</section>");
    }
    if (testCase.stability) {
        output += renderStability(*testCase.stability);
    }
    if (testCase.guidedRecovery) {
        output += renderGuided(*testCase.guidedRecovery);
    }
    output += renderEvidenceRetention(testCase.evidenceRetention);
    output += QStringLiteral("<details class=\"attempts\"><summary>通信证据（保留 %1 / 总计 %2）</summary>")
                  .arg(testCase.evidenceRetention.retainedAttempts)
                  .arg(testCase.evidenceRetention.totalAttempts);
    if (testCase.attempts.isEmpty()) {
        output += QStringLiteral("<p>无保留 attempt。</p>");
    } else {
        for (const auto &attempt : testCase.attempts) {
            output += renderAttempt(attempt);
        }
    }
    output += QStringLiteral("</details></article>");
    return output;
}

QString metadataValue(const ReportMetadataValue &value)
{
    const QString display = value.available ? shown(value.value) : shown(value.missingDisplay);
    return QStringLiteral("%1 <span class=\"source\">[%2]</span>")
        .arg(display, escapeHtml(metadataSourceName(value.source)));
}

QString passRateText(const ReportSummary &summary)
{
    if (!summary.passRatePpm) {
        return QStringLiteral("N/A（分母为 0）");
    }
    const quint32 ppm = *summary.passRatePpm;
    return QStringLiteral("%1.%2%（%3/%4）")
        .arg(ppm / 10000)
        .arg(ppm % 10000, 4, 10, QLatin1Char('0'))
        .arg(summary.passRateNumerator)
        .arg(summary.passRateDenominator);
}

QString renderHtml(const ReportDocumentModel &model, const QString &generatedAtUtcIso)
{
    QString output;
    output.reserve(32768);
    output += QString::fromLatin1("<!doctype html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'\">\n<title>");
    output += shown(QStringLiteral("OMS555TV 测试报告 · %1").arg(model.suiteName));
    output += QString::fromLatin1(R"(</title>
<style>
:root{color-scheme:light;--ink:#172033;--muted:#5d6678;--line:#cfd5df;--panel:#f7f9fc;--pass:#176b3a;--fail:#a12622;--error:#8b1a4a;--skipped:#595f69}
*{box-sizing:border-box}body{margin:0;background:#eef1f6;color:var(--ink);font:14px/1.55 "Segoe UI","Microsoft YaHei UI",sans-serif}.page{max-width:1180px;margin:24px auto;background:#fff;padding:32px;box-shadow:0 4px 24px #1c2b481c}h1,h2,h3,h4,h5{line-height:1.25;margin:1.15em 0 .55em}h1{font-size:28px;margin-top:0}h2{border-bottom:2px solid #2e5d98;padding-bottom:7px}h3{font-size:18px}.lead{color:var(--muted)}.status{display:inline-block;margin-left:.4em;border:1px solid currentColor;border-radius:999px;padding:.08em .58em;font-weight:700;letter-spacing:.03em}.pass{color:var(--pass)}.fail{color:var(--fail)}.error{color:var(--error)}.skipped{color:var(--skipped)}.other{color:#555}.summary-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(125px,1fr));gap:10px;margin:14px 0}.metric{border:1px solid var(--line);border-radius:7px;padding:11px;background:var(--panel)}.metric strong{display:block;font-size:19px}.case{border:1px solid var(--line);border-radius:8px;padding:16px;margin:16px 0;break-inside:auto}.subsection{margin:14px 0}.evidence-note{border-left:4px solid #b07800;background:#fff9e8;padding:8px 12px;margin:14px 0}table{width:100%;border-collapse:collapse;margin:8px 0 14px}th,td{border:1px solid var(--line);padding:7px 9px;vertical-align:top}th{background:#edf2f8;text-align:left}tbody th{width:220px}.text,pre,code{white-space:pre-wrap;overflow-wrap:anywhere;word-break:break-word}.source{color:var(--muted);font-size:.9em}.error-box{color:var(--error)}details{border:1px solid var(--line);border-radius:5px;margin:9px 0;padding:6px 9px}summary{cursor:pointer;font-weight:650}.attempt{margin-left:12px}.artifact{background:var(--panel);padding:12px;border-radius:7px}.warning{font-weight:650;color:#765100}
@media(max-width:700px){.page{margin:0;padding:16px;box-shadow:none}tbody th{width:34%}table{font-size:12px}}
@media print{@page{size:auto;margin:14mm}body{background:#fff;color:#000;font-size:10pt}.page{max-width:none;margin:0;padding:0;box-shadow:none}h1,h2,h3,h4,h5,.metric,.evidence-note,summary{break-after:avoid;page-break-after:avoid}.case,table{break-inside:auto}.summary-grid{grid-template-columns:repeat(4,1fr)}thead{display:table-header-group}tr{break-inside:avoid;page-break-inside:avoid}details{border-color:#777}details>summary{display:none}details>*{display:block!important}.status{color:#000!important;border-color:#000}.artifact{border:1px solid #777}}
</style>
</head>
<body><main class="page">
)");
    output += QStringLiteral("<header><h1>OMS555TV 自动化测试报告 %1</h1><p class=\"lead text\">Suite：%2（%3）<br>生成时间：%4</p></header>")
                  .arg(statusBadge(model.status), shown(model.suiteName), shown(model.suiteId), escapeHtml(generatedAtUtcIso));

    output += QStringLiteral("<section><h2>基本信息</h2><table><tbody>");
    output += row(QStringLiteral("项目"), metadataValue(model.metadata.projectName));
    output += row(QStringLiteral("测试对象"), metadataValue(model.metadata.testObject));
    output += row(QStringLiteral("设备型号"), metadataValue(model.metadata.deviceModel));
    output += row(QStringLiteral("Firmware 版本"), metadataValue(model.metadata.firmwareVersion));
    output += row(QStringLiteral("Host 版本"), metadataValue(model.metadata.hostVersion));
    output += row(QStringLiteral("源码标识"), metadataValue(model.metadata.sourceRevision));
    output += row(QStringLiteral("测试人员"), metadataValue(model.metadata.tester));
    output += row(QStringLiteral("测试台架"), metadataValue(model.metadata.testBench));
    output += row(QStringLiteral("环境说明"), metadataValue(model.metadata.environmentDescription));
    output += row(QStringLiteral("Suite"), escapeHtml(
        QStringLiteral("ID=") + model.suiteId + QStringLiteral("，名称=") + model.suiteName
        + QStringLiteral("，Schema v") + QString::number(model.schemaVersion)
        + QStringLiteral("，Tags=")
        + (model.suiteTags.isEmpty() ? QStringLiteral("无") : model.suiteTags.join(QStringLiteral(", ")))));
    output += row(QStringLiteral("Suite 描述"), shown(model.suiteDescription));
    output += row(QStringLiteral("Run ID"), QString::number(model.runId));
    output += row(QStringLiteral("Session ID"), metadataValue(model.metadata.sessionId));
    output += row(QStringLiteral("运行开始"), shown(model.started.utcIso) + QStringLiteral("<br>") + shown(model.started.localIso));
    output += row(QStringLiteral("运行结束"), shown(model.finished.utcIso) + QStringLiteral("<br>") + shown(model.finished.localIso));
    output += row(QStringLiteral("运行时区"), shown(QString::fromUtf8(model.started.timeZoneId)));
    output += row(QStringLiteral("已中止"), model.aborted ? QStringLiteral("是") : QStringLiteral("否"));
    if (model.metadata.connection.available) {
        const auto &connection = model.metadata.connection;
        output += row(QStringLiteral("通信参数"), escapeHtml(
            connection.portName + QStringLiteral("，") + QString::number(connection.baudRate)
            + QStringLiteral(" baud，") + QString::number(connection.dataBits)
            + QStringLiteral(" data bits，") + connection.parity + QStringLiteral("，")
            + connection.stopBits + QStringLiteral(" stop bits，") + connection.flowControl
            + QStringLiteral("，Slave ") + QString::number(connection.serverAddress)
            + QStringLiteral("，timeout ") + QString::number(connection.defaultResponseTimeoutMs)
            + QStringLiteral(" ms，最大排队 ") + QString::number(connection.maxPendingRequests)
            + QStringLiteral(" [") + metadataSourceName(connection.source) + QChar(u']')));
    } else {
        output += row(QStringLiteral("通信参数"), shown(model.metadata.connection.missingDisplay)
            + QStringLiteral(" <span class=\"source\">[unavailable]</span>"));
    }
    output += QStringLiteral("</tbody></table><details><summary>完整 Suite Metadata</summary><table><thead><tr><th>Key</th><th>Value</th><th>Source</th></tr></thead><tbody>");
    if (model.metadata.suiteEntries.isEmpty()) {
        output += QStringLiteral("<tr><td colspan=\"3\">无</td></tr>");
    } else {
        for (auto it = model.metadata.suiteEntries.cbegin(); it != model.metadata.suiteEntries.cend(); ++it) {
            output += QStringLiteral("<tr><td class=\"text\">%1</td><td class=\"text\">%2</td><td>%3</td></tr>")
                          .arg(shown(it.key()), metadataValue(it.value()),
                               escapeHtml(metadataSourceName(it.value().source)));
        }
    }
    output += QStringLiteral("</tbody></table></details></section>");

    const auto &summary = model.summary;
    output += QStringLiteral("<section><h2>测试汇总</h2><div class=\"summary-grid\">");
    const QVector<QPair<QString, QString>> metrics{
        {QStringLiteral("总数"), QString::number(summary.total)},
        {QStringLiteral("已执行"), QString::number(summary.executed)},
        {QStringLiteral("PASS"), QString::number(summary.passed)},
        {QStringLiteral("FAIL"), QString::number(summary.failed)},
        {QStringLiteral("ERROR"), QString::number(summary.errors)},
        {QStringLiteral("SKIPPED"), QString::number(summary.skipped)},
        {QStringLiteral("通过率"), passRateText(summary)},
        {QStringLiteral("总耗时"), durationText(summary.duration)},
    };
    for (const auto &metric : metrics) {
        output += QStringLiteral("<div class=\"metric\"><span>%1</span><strong class=\"text\">%2</strong></div>")
                      .arg(escapeHtml(metric.first), escapeHtml(metric.second));
    }
    output += QStringLiteral("</div><p>报告终态：%1</p>").arg(statusBadge(summary.status));
    if (!model.auxiliaryErrors.isEmpty()) {
        output += QStringLiteral("<h3>辅助错误</h3>");
        for (const auto &error : model.auxiliaryErrors) {
            output += errorBlock(std::optional<ReportExecutionError>(error));
        }
    }
    output += QStringLiteral("</section><section><h2>用例明细</h2>");
    if (model.cases.isEmpty()) {
        output += QStringLiteral("<p>无用例。</p>");
    } else {
        for (const auto &testCase : model.cases) {
            output += renderCase(testCase);
        }
    }
    output += QStringLiteral("</section>");

    const auto &artifact = model.sessionLogArtifact;
    output += QStringLiteral("<section><h2>SessionLog 与证据边界</h2><div class=\"artifact\"><p class=\"warning\">HTML 仅包含 ReportDocumentModel 中保留的证据；稳定性有界代表证据不等于全部原始事务。完整事务仅由下列 SessionLog 工件引用，本报告不会读取或嵌入 JSONL。</p><table><tbody>");
    output += row(QStringLiteral("工件状态"), artifact.available ? QStringLiteral("可用") : shown(artifact.missingDisplay));
    output += row(QStringLiteral("Session ID"), shown(artifact.sessionId, QStringLiteral("未采集")));
    output += row(QStringLiteral("路径（纯文本）"), shown(artifact.path, QStringLiteral("未提供")));
    output += row(QStringLiteral("大小"), artifact.available ? escapeHtml(QStringLiteral("%1 bytes").arg(artifact.sizeBytes)) : QStringLiteral("未采集"));
    output += row(QStringLiteral("SHA-256"), shown(artifact.sha256, QStringLiteral("未采集")));
    output += QStringLiteral("</tbody></table></div></section><footer><p class=\"lead\">由 OMS555TV Host 生成。此文件为离线、自包含、无脚本报告。</p></footer></main></body></html>\n");
    return output;
}

ReportError makeError(const ReportErrorCode code, const QString &path,
                      const QString &diagnostic)
{
    return ReportError{code, path, diagnostic};
}

} // namespace

HtmlReportGenerator::HtmlReportGenerator(Clock clock)
    : clock_(clock ? std::move(clock) : Clock([] { return QDateTime::currentDateTimeUtc(); }))
{
}

QString HtmlReportGenerator::suggestFileName(const ReportDocumentModel &model)
{
    QString suite;
    suite.reserve(model.suiteId.size());
    bool separatorPending = false;
    for (const QChar character : model.suiteId) {
        const ushort code = character.unicode();
        const bool asciiLetter = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
        const bool asciiDigit = code >= '0' && code <= '9';
        if (asciiLetter || asciiDigit) {
            if (separatorPending && !suite.isEmpty() && suite.size() < 48) {
                suite.append(QChar(u'-'));
            }
            separatorPending = false;
            if (suite.size() < 48) {
                suite.append(character.toLower());
            }
        } else {
            separatorPending = true;
        }
    }
    while (suite.endsWith(QChar(u'-'))) {
        suite.chop(1);
    }
    if (suite.isEmpty()) {
        suite = QStringLiteral("suite");
    }
    const QString started = model.started.utc.toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz'Z'"));
    return QStringLiteral("%1_%2_run-%3.html").arg(suite, started).arg(model.runId);
}

HtmlReportGenerationResult HtmlReportGenerator::generate(const ReportDocumentModel &model) const
{
    HtmlReportGenerationResult result;
    const QDateTime generatedAt = clock_();
    if (!generatedAt.isValid()) {
        result.errors.append(makeError(ReportErrorCode::InvalidGenerationTime,
                                       QStringLiteral("generatedAtUtc"),
                                       QStringLiteral("报告生成时间无效。")));
        return result;
    }
    const QString generatedAtIso = generatedAt.toUTC().toString(
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
    const QString html = renderHtml(model, generatedAtIso);
    result.document = HtmlReportDocument{html.toUtf8(), suggestFileName(model), generatedAtIso};
    return result;
}

HtmlReportWriteResult HtmlReportGenerator::write(const ReportDocumentModel &model,
                                                 const QString &targetFilePath) const
{
    HtmlReportWriteResult result;
    if (targetFilePath.trimmed().isEmpty() || targetFilePath.contains(QChar(u'\0'))) {
        result.errors.append(makeError(ReportErrorCode::InvalidOutputPath,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("报告目标路径为空或包含空字符。")));
        return result;
    }

    const QFileInfo target(targetFilePath);
    const QFileInfo parent(target.dir().absolutePath());
    if (!parent.exists() || !parent.isDir() || target.fileName().isEmpty() || target.isDir()) {
        result.errors.append(makeError(ReportErrorCode::InvalidOutputPath,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("报告父目录不存在、不是目录，或目标路径不是文件。")));
        return result;
    }
    if (target.exists()) {
        result.errors.append(makeError(ReportErrorCode::TargetAlreadyExists,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("目标报告已存在；生成器不会静默覆盖。")));
        return result;
    }

    const auto generated = generate(model);
    if (!generated.succeeded()) {
        result.errors = generated.errors;
        return result;
    }

    QSaveFile file(target.absoluteFilePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.errors.append(makeError(ReportErrorCode::OutputOpenFailed,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("无法创建报告临时文件：%1").arg(file.errorString())));
        return result;
    }
    const QByteArray &bytes = generated.document->utf8;
    if (file.write(bytes) != bytes.size()) {
        const QString diagnostic = file.errorString();
        file.cancelWriting();
        result.errors.append(makeError(ReportErrorCode::OutputWriteFailed,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("报告内容未能完整写入：%1").arg(diagnostic)));
        return result;
    }
    if (QFileInfo::exists(target.absoluteFilePath())) {
        file.cancelWriting();
        result.errors.append(makeError(ReportErrorCode::TargetAlreadyExists,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("写入期间出现同名目标；生成器未覆盖该文件。")));
        return result;
    }
    if (!file.commit()) {
        result.errors.append(makeError(ReportErrorCode::OutputCommitFailed,
                                       QStringLiteral("targetFilePath"),
                                       QStringLiteral("报告临时文件无法原子提交：%1").arg(file.errorString())));
        return result;
    }
    result.filePath = target.absoluteFilePath();
    return result;
}

} // namespace oms555tv::report
