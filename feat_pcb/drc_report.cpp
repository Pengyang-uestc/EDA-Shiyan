#include "feat_pcb/drc_report.h"

namespace eda {

QVector<DrcIssue> collectDrcIssues(const DrcResult& result)
{
    QVector<DrcIssue> issues;
    issues.reserve(result.violations.size() + result.errors.size());

    for (const DrcViolation& violation : result.violations) {
        DrcIssue issue;
        issue.kind = DrcIssueKind::Clearance;
        issue.code = violation.code;
        issue.message = violation.message;
        issue.refdes = violation.refdes;
        issue.refdesB = violation.refdesB;
        issue.actualMil = violation.actualMil;
        issue.requiredMil = violation.requiredMil;
        issue.padId = violation.padId;
        issue.relatedPadId = violation.relatedPadId;
        issue.trackId = violation.trackIdA;
        issue.relatedTrackId = violation.trackIdB;
        issue.viaId = violation.viaId;
        issue.pourId = violation.pourId;
        issue.relatedPourId = violation.relatedPourId;
        issues.append(issue);
    }

    for (const QString& error : result.errors) {
        DrcIssue issue;
        issue.kind = DrcIssueKind::Resolution;
        issue.code = kDrcResolveError;
        issue.message = error;
        issues.append(issue);
    }

    return issues;
}

QVector<DrcIssue> collectDrcIssues(const Board& board, const FootprintResolver& resolveFootprint,
                                   const DrcRules& rules)
{
    return collectDrcIssues(runDrc(board, resolveFootprint, rules));
}

} // namespace eda