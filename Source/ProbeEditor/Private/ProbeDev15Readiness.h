#pragma once

#include "ProbeDev14Authoring.h"
#include "ProbeTestPack.h"

// Dev 15: read-only project-wide QA discovery and authoring readiness model.
// Scan is explicitly requested by the author. No assets are modified or executed.
namespace ProbeDev15
{
    enum class EKind : uint8 { Test, Pack };
    enum class EState : uint8 { NotScanned, Ready, Warning, Blocked, Disabled };
    enum class EKindFilter : uint8 { All, Tests, Packs };
    enum class EStateFilter : uint8 { All, Ready, Warning, Blocked, Disabled, NotScanned };

    struct FEntry
    {
        FString Path;
        FString Name;
        FString Folder;
        EKind Kind = EKind::Test;
        EState State = EState::NotScanned;
        int32 StepsOrMembers = 0;
        int32 Blocking = 0;
        int32 Warnings = 0;
        FString Details = TEXT("Not scanned. Select Scan to inspect this asset.");
    };

    struct FSummary
    {
        int32 Total = 0;
        int32 Tests = 0;
        int32 Packs = 0;
        int32 Ready = 0;
        int32 Warnings = 0;
        int32 Blocked = 0;
        int32 Disabled = 0;
        int32 Unscanned = 0;
    };

    inline const TCHAR* StateName(EState State)
    {
        switch (State)
        {
        case EState::Ready: return TEXT("Ready");
        case EState::Warning: return TEXT("Warnings");
        case EState::Blocked: return TEXT("Blocked");
        case EState::Disabled: return TEXT("Disabled");
        default: return TEXT("Not Scanned");
        }
    }

    inline bool Matches(const FEntry& Entry, const FString& Search, EKindFilter Kind, EStateFilter State)
    {
        if (Kind == EKindFilter::Tests && Entry.Kind != EKind::Test) { return false; }
        if (Kind == EKindFilter::Packs && Entry.Kind != EKind::Pack) { return false; }
        if (State != EStateFilter::All && !(
            (State == EStateFilter::Ready && Entry.State == EState::Ready) ||
            (State == EStateFilter::Warning && Entry.State == EState::Warning) ||
            (State == EStateFilter::Blocked && Entry.State == EState::Blocked) ||
            (State == EStateFilter::Disabled && Entry.State == EState::Disabled) ||
            (State == EStateFilter::NotScanned && Entry.State == EState::NotScanned)))
        { return false; }
        const FString Query = Search.TrimStartAndEnd();
        return Query.IsEmpty() || Entry.Name.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Folder.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Path.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Details.Contains(Query, ESearchCase::IgnoreCase);
    }

    inline FSummary Summarize(const TArray<FEntry>& Entries)
    {
        FSummary Summary;
        Summary.Total = Entries.Num();
        for (const FEntry& Entry : Entries)
        {
            if (Entry.Kind == EKind::Test) { ++Summary.Tests; } else { ++Summary.Packs; }
            switch (Entry.State)
            {
            case EState::Ready: ++Summary.Ready; break;
            case EState::Warning: ++Summary.Warnings; break;
            case EState::Blocked: ++Summary.Blocked; break;
            case EState::Disabled: ++Summary.Disabled; break;
            default: ++Summary.Unscanned; break;
            }
        }
        return Summary;
    }

    inline void InspectTest(FEntry& Entry, const UProbeTestAsset* Test)
    {
        Entry.Kind = EKind::Test;
        if (!Test)
        {
            Entry.State = EState::Blocked;
            Entry.Blocking = 1;
            Entry.Details = TEXT("Unable to load the Probe Test Asset.");
            return;
        }
        const ProbeDev14::FReport Report = ProbeDev14::PreflightTest(*Test);
        Entry.StepsOrMembers = Test->Steps.Num();
        Entry.Blocking = Report.Blocking;
        Entry.Warnings = Report.Warnings;
        Entry.State = !Test->bEnabled ? EState::Disabled : Report.Blocking ? EState::Blocked
            : Report.Warnings ? EState::Warning : EState::Ready;
        Entry.Details = FString::Printf(TEXT("TEST | %d step(s) | %s\n%s"),
            Entry.StepsOrMembers, StateName(Entry.State), *Report.Text());
    }

    template <typename TResolver>
    void InspectPack(FEntry& Entry, const UProbeTestPack* Pack, TResolver&& Resolve)
    {
        Entry.Kind = EKind::Pack;
        if (!Pack)
        {
            Entry.State = EState::Blocked;
            Entry.Blocking = 1;
            Entry.Details = TEXT("Unable to load the Probe Pack.");
            return;
        }
        const ProbeDev14::FReport Report = ProbeDev14::PreflightPack(*Pack, Resolve);
        Entry.StepsOrMembers = Pack->Tests.Num();
        Entry.Blocking = Report.Blocking;
        Entry.Warnings = Report.Warnings;
        Entry.State = Report.Blocking ? EState::Blocked : Report.Warnings ? EState::Warning : EState::Ready;
        Entry.Details = FString::Printf(TEXT("PACK | %d member(s) | %s\n%s"),
            Entry.StepsOrMembers, StateName(Entry.State), *Report.Text());
    }

    inline bool StepMatches(const FProbeTestStep& Step, const FString& Search)
    {
        const FString Query = Search.TrimStartAndEnd();
        if (Query.IsEmpty()) { return true; }
        return Step.Description.Contains(Query, ESearchCase::IgnoreCase)
            || Step.GroupName.Contains(Query, ESearchCase::IgnoreCase)
            || Step.Target.Contains(Query, ESearchCase::IgnoreCase)
            || Step.ExpectedValue.Contains(Query, ESearchCase::IgnoreCase)
            || Step.AssertionId.ToString().Contains(Query, ESearchCase::IgnoreCase)
            || FString(Probe::GetAssertionInfo(Step.Type).Name).Contains(Query, ESearchCase::IgnoreCase);
    }

    inline FString ExportCSV(const TArray<FEntry>& Entries, const FString& Search = FString(),
        EKindFilter Kind = EKindFilter::All, EStateFilter State = EStateFilter::All)
    {
        auto Quote = [](const FString& Cell)
        {
            FString Escaped = Cell;
            Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
            // Spreadsheet formula injection: force literal text for user-authored cells.
            if (!Escaped.IsEmpty() && (Escaped[0] == TEXT('=') || Escaped[0] == TEXT('+')
                || Escaped[0] == TEXT('-') || Escaped[0] == TEXT('@')))
            { Escaped = TEXT("'") + Escaped; }
            return TEXT("\"") + Escaped + TEXT("\"");
        };
        FString CSV = TEXT("Kind,Name,Path,State,Count,Blocking,Warnings,Details\r\n");
        for (const FEntry& Entry : Entries)
        {
            if (!Matches(Entry, Search, Kind, State)) { continue; }
            CSV += FString::Printf(TEXT("%s,%s,%s,%s,%d,%d,%d,%s\r\n"),
                *Quote(Entry.Kind == EKind::Test ? TEXT("Test") : TEXT("Pack")),
                *Quote(Entry.Name), *Quote(Entry.Path), *Quote(StateName(Entry.State)),
                Entry.StepsOrMembers, Entry.Blocking, Entry.Warnings, *Quote(Entry.Details));
        }
        return CSV;
    }
}
